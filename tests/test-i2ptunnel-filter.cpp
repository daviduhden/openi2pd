/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: server-tunnel access filter regression.
 *
 * Exercises the `filterDefinition` grammar (default/explicit/file/record),
 * first-match-wins ordering, the record threshold, bounded file reloading,
 * invalid-grammar rejection and the legacy accesslist upgrade. No sockets,
 * threads or timers are used.
 *
 */

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

#include "I2PTunnelAccessFilter.h"
#include "Identity.h"

namespace
{
	using i2p::client::FilterDefinition;
	using i2p::client::TunnelAccessFilter;

	i2p::data::IdentHash MakeIdentity (uint8_t first, uint8_t second, uint8_t third)
	{
		i2p::data::IdentHash identity;
		identity.Fill (0);
		uint8_t * bytes = identity;
		bytes[0] = first;
		bytes[1] = second;
		bytes[2] = third;
		bytes[3] = 0x11;
		return identity;
	}

	std::string MakeDefinition (const std::string & text, bool expectValid = true)
	{
		std::string error;
		auto definition = FilterDefinition::Parse (text, error);
		if (expectValid)
		{
			assert (definition);
			assert (error.empty ());
		}
		else
		{
			assert (!definition);
			assert (!error.empty ());
		}
		return error;
	}

	bool Allows (TunnelAccessFilter & filter, const i2p::data::IdentHash & peer, uint64_t now)
	{
		return filter.Evaluate (peer, now).allow;
	}
} // namespace

int main ()
{
	const auto allowed = MakeIdentity (5, 0, 0x2a);
	const auto other = MakeIdentity (9, 9, 9);
	const uint64_t now = 1000;

	// default=allow / default=deny with no rules.
	{
		std::string error;
		auto allowAll = FilterDefinition::Parse ("default=allow", error);
		assert (allowAll && allowAll->IsDefaultAllow () && allowAll->GetNumElements () == 0);
		TunnelAccessFilter filter (allowAll);
		assert (filter.Evaluate (other, now).allow);
		assert (!filter.Evaluate (other, now).matched);

		auto denyAll = FilterDefinition::Parse ("default=deny", error);
		assert (denyAll && !denyAll->IsDefaultAllow ());
		TunnelAccessFilter denyFilter (denyAll);
		assert (!denyFilter.Evaluate (other, now).allow);
	}

	// Explicit destination entries and first-match-wins ordering.
	{
		std::string error;
		auto definition = FilterDefinition::Parse (
			"default=deny;explicit allow " + allowed.ToBase32 () + ";explicit deny " + other.ToBase32 (), error);
		assert (definition);
		TunnelAccessFilter filter (definition);
		assert (Allows (filter, allowed, now));
		assert (!Allows (filter, other, now));
		auto unknown = MakeIdentity (1, 2, 3);
		assert (!Allows (filter, unknown, now)); // the default decides
	}

	// A deny rule before an allow-all rule wins for its destination.
	{
		std::string error;
		auto definition = FilterDefinition::Parse (
			"default=allow;explicit deny " + allowed.ToBase32 () + ";explicit allow *", error);
		assert (definition && definition->GetNumElements () == 2);
		TunnelAccessFilter filter (definition);
		assert (!Allows (filter, allowed, now));
		assert (Allows (filter, other, now));
	}

	// Masked address entries use the 127.<hash byte> mapping.
	{
		std::string error;
		auto definition = FilterDefinition::Parse ("default=deny;explicit allow 127.5.0.*", error);
		assert (definition);
		TunnelAccessFilter filter (definition);
		assert (Allows (filter, allowed, now)); // 127.5.0.42
		assert (!Allows (filter, other, now)); // 127.9.9.9
	}

	// record allow N: the first N connections match, later ones fall through to
	// the default.
	{
		std::string error;
		auto definition = FilterDefinition::Parse ("default=deny;record allow 2", error);
		assert (definition);
		TunnelAccessFilter filter (definition);
		assert (Allows (filter, allowed, now)); // count 1
		assert (Allows (filter, allowed, now)); // count 2
		assert (!Allows (filter, allowed, now)); // count 3 falls through
		assert (filter.GetNumRecords () == 1);
		// An unknown peer is counted on its first evaluation and matches the
		// threshold from its own count 1.
		assert (Allows (filter, other, now));
		assert (filter.GetNumRecords () == 2);
		filter.ClearRecords ();
		assert (filter.GetNumRecords () == 0);
	}

	// record deny 1 denies the first connection only.
	{
		std::string error;
		auto definition = FilterDefinition::Parse ("default=allow;record deny 1", error);
		assert (definition);
		TunnelAccessFilter filter (definition);
		assert (!Allows (filter, allowed, now));
		assert (Allows (filter, allowed, now));
	}

	// File entries: parsed, cached and reloaded when the size/mtime changes.
	{
		auto path = std::filesystem::temp_directory_path () / "openi2pd-filter-test.txt";
		{
			std::ofstream out (path, std::ofstream::binary | std::ofstream::trunc);
			out << allowed.ToBase32 () << "\n";
			out << "# comment\n";
			out << "not-a-valid-target\n";
		}
		const auto specification = std::string ("default=deny;file allow ") + path.string ();
		std::string error;
		auto definition = FilterDefinition::Parse (specification, error);
		assert (definition && definition->GetNumElements () == 1);
		TunnelAccessFilter filter (definition);
		filter.SetFileCacheSeconds (0); // stat on every evaluation
		assert (Allows (filter, allowed, now));
		assert (!Allows (filter, other, now));

		// A longer file changes the size, so the reload does not depend on the
		// filesystem timestamp resolution.
		{
			std::ofstream out (path, std::ofstream::binary | std::ofstream::trunc);
			out << other.ToBase32 () << "\n";
			out << allowed.ToBase32 () << "\n";
		}
		assert (Allows (filter, other, now));
		std::filesystem::remove (path);
		// A missing file deactivates its entries instead of matching everyone.
		assert (!Allows (filter, other, now));
	}

	// Legacy inline accesslist upgrade: listed destinations only.
	{
		auto definition = FilterDefinition::FromAccessList ({ allowed, other });
		assert (definition && !definition->IsDefaultAllow ());
		TunnelAccessFilter filter (definition);
		assert (Allows (filter, allowed, now));
		assert (Allows (filter, other, now));
		assert (!Allows (filter, MakeIdentity (7, 7, 7), now));

		auto empty = FilterDefinition::FromAccessList ({ });
		TunnelAccessFilter denyFilter (empty);
		assert (!Allows (denyFilter, allowed, now));
	}

	// Invalid grammar is rejected at parse time without a definition.
	{
		MakeDefinition ("default=maybe", false);
		MakeDefinition ("boo allow 1.2.3.4", false);
		MakeDefinition ("explicit maybe 1.2.3.4", false);
		MakeDefinition ("explicit allow not-a-destination", false);
		MakeDefinition ("explicit allow", false);
		MakeDefinition ("default=allow;default=deny", false);
		MakeDefinition ("file allow relative/path.txt", false);
		MakeDefinition ("record allow abc", false);
		MakeDefinition ("record allow 4294967296", false);
		MakeDefinition ("default deny", false);
		MakeDefinition ("explicit allow 127.5.0.300", false);
		// Comments, whitespace and newline separators are accepted.
		MakeDefinition ("default=allow;\n  # comment\n explicit deny 127.5.0.* ");
	}

	// Peek never consumes a record count and never matches an unknown peer.
	{
		std::string error;
		auto definition = FilterDefinition::Parse ("default=deny;record allow 1", error);
		assert (definition);
		TunnelAccessFilter filter (definition);
		assert (!filter.Peek (other, now).allow);
		assert (filter.GetNumRecords () == 0);
		assert (Allows (filter, other, now));
		assert (filter.Peek (other, now).allow);
		assert (filter.GetNumRecords () == 1);
	}

	return 0;
}
