# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# Pin the queued administration re-arm's revocable lifetime authority.
# This source contract does not validate the separate timer callbacks.

if(NOT DEFINED SOURCE_DIR)
  get_filename_component(SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()
if(NOT EXISTS "${SOURCE_DIR}/libi2pd_client/AddressBook.cpp" OR
   NOT EXISTS "${SOURCE_DIR}/libi2pd_client/AddressBook.h")
  message(FATAL_ERROR
    "SOURCE_DIR must name the repository root, not the build/ CMake entry")
endif()
file(READ "${SOURCE_DIR}/libi2pd_client/AddressBook.cpp" source)
file(READ "${SOURCE_DIR}/libi2pd_client/AddressBook.h" header)

function(section begin end output)
  string(FIND "${source}" "${begin}" first)
  string(FIND "${source}" "${end}" last)
  if(first LESS 0 OR last LESS_EQUAL first)
    message(FATAL_ERROR "Missing address-book lifetime review boundary")
  endif()
  math(EXPR length "${last} - ${first}")
  string(SUBSTRING "${source}" ${first} ${length} body)
  set(${output} "${body}" PARENT_SCOPE)
endfunction()

function(require_order body first second)
  string(FIND "${body}" "${first}" before)
  string(FIND "${body}" "${second}" after)
  if(before LESS 0 OR after LESS_EQUAL before)
    message(FATAL_ERROR "Address-book authority order: ${first} / ${second}")
  endif()
endfunction()

section("void AddressBook::Start ()" "void AddressBook::StartResolvers ()" start)
section("void AddressBook::Stop ()"
  "std::shared_ptr<const Address> AddressBook::GetAddress (" stop)
section("void AddressBook::PostSubscriptionsRearm ("
  "void AddressBook::RearmSubscriptionsTimer (" post)

foreach(body IN ITEMS start stop)
  require_order("${${body}}" "admission (m_RearmAdmission->mutex)"
    "l (m_SubscriptionsMutex)")
endforeach()
require_order("${start}" "m_ReloadAlive->store (false"
  "m_ReloadAlive = std::make_shared<std::atomic<bool>> (true)")
require_order("${start}" "m_ReloadAlive = std::make_shared"
  "m_RearmAdmission->owner = this")
require_order("${stop}" "m_RearmAdmission->owner = nullptr"
  "m_SubscriptionsUpdateTimer->cancel ()")
require_order("${stop}" "m_ReloadAlive->store (false"
  "m_SubscriptionsUpdateTimer->cancel ()")
require_order("${post}" "[admission, token, delay]"
  "lock (admission->mutex)")
require_order("${post}" "lock (admission->mutex)"
  "if (!admission->owner) return")
require_order("${post}" "if (!token->load (std::memory_order_acquire)) return"
  "admission->owner->RearmSubscriptionsTimer (delay)")
if(post MATCHES "\\[this[,\\]]" OR
   NOT header MATCHES "const std::shared_ptr<RearmAdmission> m_RearmAdmission")
  message(FATAL_ERROR "Posted re-arm must own only immutable admission state")
endif()
