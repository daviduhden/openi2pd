/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Reload-local destination reuse follows key-loading semantics.
 */

#ifndef OPENI2PD_TUNNEL_DESTINATION_CACHE_H
#define OPENI2PD_TUNNEL_DESTINATION_CACHE_H

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace i2p::client
{
/// Match the complete family accepted by the transient key loader.
constexpr bool UsesTransientTunnelKeys (std::string_view name) noexcept
{
	return name.starts_with ("transient");
}

/*
	 * Reuse file-backed identities within one configuration reload.
	 *
	 * This cache owns retained destinations and is confined to the reload
	 * thread. Transient requests always miss and are never retained: two
	 * sections using the same marker must still receive separate keys.
	 * Returned pointers retain their owner after the cache is destroyed.
	 */
template <typename Destination> class TunnelDestinationCache
{
public:
	std::shared_ptr<Destination> Find (std::string_view name) const
	{
		if (UsesTransientTunnelKeys (name)) return { };
		auto found = m_Destinations.find (name);
		if (found == m_Destinations.end ()) return { };
		return found->second;
	}

	/// Keep only usable file-backed owners; failed creation stays a miss.
	void Remember (std::string_view name, std::shared_ptr<Destination> destination)
	{
		if (!UsesTransientTunnelKeys (name) && destination)
			m_Destinations.insert_or_assign (std::string (name), std::move (destination));
	}

private:
	std::map<std::string, std::shared_ptr<Destination>, std::less<>> m_Destinations;
};
}

#endif
