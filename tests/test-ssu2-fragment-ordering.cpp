/*
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This file is part of openi2pd and licensed under BSD-3-Clause
 *
 * See full license text in LICENSE file at top of project tree
 *
 * Regression checks for SSU2 follow-on fragment ordering. Java
 * InboundMessageState.receiveFragment() rejects a second last fragment and
 * any new fragment at or above a known last fragment; otherwise reassembly
 * would silently truncate at the first "last" marker.
 *
 * Register as a CTest target (tests/CMakeLists.txt is owned elsewhere):
 *   test-ssu2-fragment-ordering
 */

#include <cassert>
#include <cstdint>
#include <memory>

#include "SSU2Session.h"

namespace
{
std::shared_ptr<i2p::transport::SSU2IncompleteMessage::Fragment> MakeFragment (int number, bool isLast)
{
	const uint8_t data[1] = { 0 };
	return std::make_shared<i2p::transport::SSU2IncompleteMessage::Fragment> (data, sizeof (data), number, isLast);
}
}

int main ()
{
	auto message = std::make_shared<i2p::transport::SSU2IncompleteMessage> ();
	message->nextFragmentNum = 0;

	// Last fragment 3 arrives first.
	auto fragment3 = MakeFragment (3, true);
	message->AddOutOfSequenceFragment (fragment3);
	assert (message->lastFragmentNum == 3);
	assert (message->outOfSequenceFragments == fragment3);

	// A fragment above the last one must not be queued.
	message->AddOutOfSequenceFragment (MakeFragment (4, false));
	assert (message->lastFragmentNum == 3);
	assert (message->outOfSequenceFragments == fragment3);
	assert (!fragment3->next);

	// A second last marker must not replace the first one.
	message->AddOutOfSequenceFragment (MakeFragment (2, true));
	assert (message->lastFragmentNum == 3);
	assert (message->outOfSequenceFragments == fragment3);

	// Missing fragments below the last one are still accepted in order.
	auto fragment2 = MakeFragment (2, false);
	message->AddOutOfSequenceFragment (fragment2);
	assert (message->outOfSequenceFragments == fragment2);
	assert (fragment2->next == fragment3);
	assert (!fragment3->next);

	// A duplicate last fragment is ignored.
	auto duplicate3 = MakeFragment (3, true);
	message->AddOutOfSequenceFragment (duplicate3);
	assert (message->outOfSequenceFragments == fragment2);
	assert (fragment2->next == fragment3);
	assert (!fragment3->next);

	return 0;
}
