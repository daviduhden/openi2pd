# See the LICENSE file at the top of the project tree for copyright and
# license details.
#
# openi2pd: compile-free view/session authority for Android dialogs.

if(NOT PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()
set(java "${PROJECT_ROOT}/android/app/src/main/java/org/openi2pd/router")
file(READ "${java}/UiDialogEpoch.java" epoch)
foreach(required IN ITEMS "MAX_DIALOGS = 8;" "Token current;"
    "token == current" "visible.size() >= MAX_DIALOGS" "current = null;"
    "visible.clear();" "close(previous);")
  string(FIND "${epoch}" "${required}" found)
  if(found LESS 0)
    message(FATAL_ERROR "Android dialog authority missing: ${required}")
  endif()
endforeach()
string(FIND "${epoch}" "void retireView()" begin)
string(SUBSTRING "${epoch}" ${begin} -1 retirement)
string(FIND "${retirement}" "current = null;" revoke)
string(FIND "${retirement}" "close(previous);" dismiss)
if(revoke LESS 0 OR dismiss LESS revoke)
  message(FATAL_ERROR "Dialog authority must retire before dismissal")
endif()
foreach(fragment IN ITEMS AddressBookFragment RuntimeAdminFragment SettingsFragment TorrentsFragment)
  file(READ "${java}/${fragment}.java" source)
  foreach(required IN ITEMS "UiDialogEpoch dialogs" "dialogs.openView();" "dialogs.retireView();")
    string(FIND "${source}" "${required}" found)
    if(found LESS 0)
      message(FATAL_ERROR "${fragment} must own and revoke its dialog view")
    endif()
  endforeach()
  string(FIND "${source}" "public void onDestroyView()" begin)
  string(SUBSTRING "${source}" ${begin} -1 teardown)
  string(FIND "${teardown}" "dialogs.retireView();" revoke)
  string(FIND "${teardown}" "super.onDestroyView();" parent)
  if(revoke LESS 0 OR parent LESS revoke)
    message(FATAL_ERROR "${fragment} must revoke before view teardown")
  endif()
endforeach()
file(READ "${java}/TorrentsFragment.java" torrent)
foreach(required IN ITEMS "extends MaterialAlertDialogBuilder"
    "viewToken = dialogs.token();" "selectedTunnel = tunnel;"
    "selectedName = currentTunnelName;" "selectedTunnel == tunnel"
    "selectedName.equals(currentTunnelName)" "if (ready())"
    "dialogs.nest(owner.viewToken, dialog::dismiss)" "dialogOwners.clear();")
  string(FIND "${torrent}" "${required}" found)
  if(found LESS 0)
    message(FATAL_ERROR "Torrent dialogs must retain captured authority: ${required}")
  endif()
endforeach()
if(torrent MATCHES "new MaterialAlertDialogBuilder")
  message(FATAL_ERROR "Torrent dialogs must use their owned builder")
endif()

# Activity confirmations carry foreground/session authority.
file(READ "${java}/UiConfirmationAuthority.java" confirmation)
file(READ "${java}/MainActivity.java" activity)
foreach(required IN ITEMS "Ticket current;" "ticket != current"
    "ticket.foreground != foreground" "ticket.session != session"
    "boolean consume(" "void sessionChanged()" "void stop()")
  string(FIND "${confirmation}" "${required}" found)
  if(found LESS 0)
    message(FATAL_ERROR "Activity confirmation authority missing: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS "confirmations.start();" "confirmations.stop();"
    "confirmations.sessionChanged();"
    "confirmations.register(dialog::dismiss)")
  string(FIND "${activity}" "${required}" found)
  if(found LESS 0)
    message(FATAL_ERROR "Activity confirmation lifecycle missing: ${required}")
  endif()
endforeach()
# A formatter may wrap after consume's opening parenthesis. Match only
# that call boundary; retain the captured ticket and admission operands.
function(has_confirmation_consumption text output)
  string(REGEX MATCH
    "confirmations[.]consume[(][ \t\r\n]*ticket,[ \t\r\n]*confirmationReady[(][)][ \t]*&&[ \t]*admitted[.]getAsBoolean[(][)][)]"
    consumption "${text}")
  if(consumption)
    set(${output} TRUE PARENT_SCOPE)
  else()
    set(${output} FALSE PARENT_SCOPE)
  endif()
endfunction()
has_confirmation_consumption("${activity}" consumes_captured_ticket)
if(NOT consumes_captured_ticket)
  message(FATAL_ERROR "Activity confirmation must consume its captured ticket with current admission")
endif()
# Negative controls must change real source before checking refusal.
foreach(mutation IN ITEMS ticket admission)
  if(mutation STREQUAL ticket)
    string(REPLACE "ticket, confirmationReady()" "otherTicket, confirmationReady()" candidate "${activity}")
  else()
    string(REPLACE "confirmationReady() && admitted.getAsBoolean()" "true" candidate "${activity}")
  endif()
  if(candidate STREQUAL activity)
    message(FATAL_ERROR "Confirmation authority mutation did not change its input: ${mutation}")
  endif()
  has_confirmation_consumption("${candidate}" accepts_mutation)
  if(accepts_mutation)
    message(FATAL_ERROR "Confirmation authority accepted mutated ${mutation}")
  endif()
endforeach()
foreach(test_path IN ITEMS
    "android/app/src/test/java/org/openi2pd/router/UiConfirmationAuthorityTest.java"
    "android/app/src/androidTest/java/org/openi2pd/router/MainActivityConfirmationTest.java")
  if(NOT EXISTS "${PROJECT_ROOT}/${test_path}")
    message(FATAL_ERROR "Missing confirmation regression: ${test_path}")
  endif()
endforeach()
