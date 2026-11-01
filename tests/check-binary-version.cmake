# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check binary version.
#
# Bounded driver for "the built router answers --version and --help".
# Each probe runs the executable directly (never through a shell), waits a
# fixed budget and reports the child's exit status and captured output, so
# a hang is reported as a hang with diagnostics instead of as a bare CTest
# timeout.
#
# The two budgets must sum to less than the registered CTest TIMEOUT (30 s)
# so the driver, not CTest, produces the diagnostic when a probe hangs.
#
# The Windows router is a GUI-subsystem image: it has no console of its
# own and its standard streams are inherited. The daemon must therefore
# publish the banner through the inherited handle (a pipe here) and never
# through a modal dialog, which is exactly what these probes observe. The
# only case this driver cannot exercise is a start with neither a usable
# standard-output handle nor a parent console (double-click from Explorer):
# there the exit status is the sole signal, which the second half of each
# check still requires.
#

if(NOT BINARY)
  message(FATAL_ERROR "BINARY is required")
endif()
if(NOT EXISTS "${BINARY}")
  message(FATAL_ERROR "binary not found: ${BINARY}")
endif()

# Budgets are shorter than the CTest TIMEOUT so a hang is reported as a
# hang, with the output captured before the process was killed, instead of
# as a bare timeout with no diagnostics. A healthy router answers in
# milliseconds on every platform.
if(NOT VERSION_TIMEOUT)
  set(VERSION_TIMEOUT 12)
endif()
if(NOT HELP_TIMEOUT)
  set(HELP_TIMEOUT 8)
endif()

set(version_budget ${VERSION_TIMEOUT})
set(help_budget ${HELP_TIMEOUT})
math(EXPR total_budget "${version_budget} + ${help_budget}")
if(total_budget GREATER 29)
  message(WARNING
    "The --version and --help budgets total ${total_budget} s, which leaves "
    "no room for the driver itself inside the 30 s CTest TIMEOUT; a hang "
    "would then be reported without diagnostics.")
endif()

# Run one informational probe. execute_process() reports a killed child
# with a message instead of an exit code, so only a plain integer status is
# a normal exit: a GUI-subsystem executable that blocks before or during
# the handler (console attach, code-page change, a modal dialog, a
# teardown wait) neither prints nor exits. That is a product defect to fix
# in the daemon, not a reason to raise the budget.
function(i2pd_informational_probe flag budget out_output)
  execute_process(
    COMMAND "${BINARY}" ${flag}
    RESULT_VARIABLE probe_result
    OUTPUT_VARIABLE probe_output
    ERROR_VARIABLE probe_error
    TIMEOUT ${budget})
  set(captured "${probe_output}${probe_error}")
  if(NOT probe_result MATCHES "^[0-9]+$")
    message(FATAL_ERROR
      "${BINARY} ${flag} did not terminate within ${budget} s "
      "(${probe_result}). The router must answer ${flag} and exit without "
      "opening a modal dialog or starting a router. Captured output:\n"
      "${captured}")
  endif()
  if(NOT probe_result EQUAL 0)
    message(FATAL_ERROR
      "${BINARY} ${flag} exited with status ${probe_result} instead of 0. "
      "Captured output:\n${captured}")
  endif()
  set(${out_output} "${captured}" PARENT_SCOPE)
endfunction()

i2pd_informational_probe(--version "${version_budget}" version_output)

# The banner is required on every platform. A GUI-subsystem build still
# inherits the driver's stdout pipe, so an unobservable banner means the
# daemon wrote to a dead console handle instead of to the handle it was
# given, or never reached the handler at all.
if(NOT version_output MATCHES "openi2pd version [0-9]+\\.[0-9]+")
  message(FATAL_ERROR
    "${BINARY} --version exited 0 without printing the version banner. "
    "Captured output:\n${version_output}")
endif()
# The library versions are part of the answer; their absence means a
# truncated write rather than a healthy quiet build.
if(NOT version_output MATCHES "Boost version")
  message(FATAL_ERROR
    "${BINARY} --version printed a banner without the Boost version. "
    "Captured output:\n${version_output}")
endif()

i2pd_informational_probe(--help "${help_budget}" help_output)

# --help renders the same banner plus the registered option table, so it
# also proves the option registration exists before any configuration is
# read.
if(NOT help_output MATCHES "openi2pd version [0-9]+\\.[0-9]+")
  message(FATAL_ERROR
    "${BINARY} --help exited 0 without printing the version banner. "
    "Captured output:\n${help_output}")
endif()
if(NOT help_output MATCHES "Show this message")
  message(FATAL_ERROR
    "${BINARY} --help did not render the option table (the 'help' option "
    "description is missing). Captured output:\n${help_output}")
endif()

message(STATUS "i2pd: --version and --help terminate with an observable banner")
