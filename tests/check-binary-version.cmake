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
# Termination and exit status are mandatory on every platform: a child that
# neither prints nor exits has blocked before or inside the answer, which is
# the defect this driver exists to catch.
#
# The banner is mandatory whenever output was captured. The Windows router
# is a GUI-subsystem image with no console of its own and no guaranteed CRT
# argument vector, so it publishes through the standard handles it
# inherited: the driver's pipes when it runs under CTest, which is the case
# this lane covers. A GUI start with no inherited handle at all (double-click
# from Explorer) can only report an exit status, so an empty answer there is
# recorded as a limitation instead of a failure; a *partial* answer is still
# a defect. Truncation is never tolerated.
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
# a normal exit: a child that blocks before or during the handler (loader
# error dialog, console attach, code-page change, a modal dialog, a teardown
# wait) neither prints nor exits. A router start does the same, because its
# default log level prints nothing and its message loop never returns. That
# is a product defect to fix in the daemon, not a reason to raise the
# budget.
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
      "${captured}\n"
      "On Windows, distinguish a block before the entry point (a loader "
      "error dialog: check the PE subsystem and the runtime DLL set) from "
      "a router start (the informational flag was never seen). Raising the "
      "budget does not fix either.")
  endif()
  if(NOT probe_result EQUAL 0)
    message(FATAL_ERROR
      "${BINARY} ${flag} exited with status ${probe_result} instead of 0. "
      "Captured output:\n${captured}")
  endif()
  set(${out_output} "${captured}" PARENT_SCOPE)
endfunction()

# A GUI-subsystem Windows image can start with no standard handle it could
# write to. An empty answer is then a documented observability limit; any
# non-empty answer has to be complete.
set(captured_banner_optional FALSE)
if(WIN32)
  set(captured_banner_optional TRUE)
endif()

function(i2pd_require_banner output flag optional)
  if("${output}" STREQUAL "")
    if(optional)
      message(STATUS
        "i2pd: ${flag} exited 0 without a readable banner (the process had no "
        "inherited standard handle); termination and exit status are the "
        "contract on this platform")
      return()
    endif()
    message(FATAL_ERROR
      "${BINARY} ${flag} exited 0 without printing the version banner. "
      "Captured output:\n${output}")
  endif()
  if(NOT output MATCHES "openi2pd version [0-9]+\\.[0-9]+")
    message(FATAL_ERROR
      "${BINARY} ${flag} printed an answer without the version banner (a "
      "truncated write). Captured output:\n${output}")
  endif()
endfunction()

i2pd_informational_probe(--version "${version_budget}" version_output)
i2pd_require_banner("${version_output}" --version ${captured_banner_optional})

# The library versions are part of the answer; their absence means a
# truncated write rather than a healthy quiet build. They are only required
# from an answer that exists at all.
if(NOT version_output STREQUAL "" AND NOT version_output MATCHES "Boost version")
  message(FATAL_ERROR
    "${BINARY} --version printed a banner without the Boost version. "
    "Captured output:\n${version_output}")
endif()

i2pd_informational_probe(--help "${help_budget}" help_output)
i2pd_require_banner("${help_output}" --help ${captured_banner_optional})

# --help renders the same banner plus the registered option table, so it
# also proves the option registration exists before any configuration is
# read.
if(NOT help_output STREQUAL "" AND NOT help_output MATCHES "Show this message")
  message(FATAL_ERROR
    "${BINARY} --help did not render the option table (the 'help' option "
    "description is missing). Captured output:\n${help_output}")
endif()

message(STATUS "i2pd: --version and --help terminated with a successful exit status")
if(version_output MATCHES "openi2pd version" AND help_output MATCHES "Show this message")
  message(STATUS "i2pd: the version banner and the option table were captured")
endif()
