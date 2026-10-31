# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check binary version.
#
# Bounded driver for the "the built router answers --version" smoke test.
# It runs the executable directly (never through a shell), waits a fixed
# budget and reports the child's exit status and captured output. The
# process exit status is authoritative: a failing --version implementation
# must never turn green, and a windowed build that cannot publish the
# banner is not allowed to fail on that alone.
#

if(NOT BINARY)
  message(FATAL_ERROR "BINARY is required")
endif()
if(NOT EXISTS "${BINARY}")
  message(FATAL_ERROR "binary not found: ${BINARY}")
endif()

# The budget is shorter than the CTest TIMEOUT so a hang is reported as a
# hang, with the output that was captured before the process was killed,
# instead of as a bare timeout with no diagnostics.
if(NOT VERSION_TIMEOUT)
  set(VERSION_TIMEOUT 20)
endif()

execute_process(
  COMMAND "${BINARY}" --version
  RESULT_VARIABLE version_result
  OUTPUT_VARIABLE version_output
  ERROR_VARIABLE version_error
  TIMEOUT ${VERSION_TIMEOUT})
set(captured "${version_output}${version_error}")

# execute_process() reports a killed child with a message instead of an
# exit code; only a plain integer status is a normal exit. A GUI-subsystem
# executable on Windows is the case to watch: it has no console, and a
# modal fatal-error or loader dialog has nobody to dismiss it on a headless
# runner, so the child neither prints nor exits. That is a product or
# toolchain defect to fix in the daemon, not a reason to raise the timeout.
if(NOT version_result MATCHES "^[0-9]+$")
  message(FATAL_ERROR
    "${BINARY} --version did not terminate within ${VERSION_TIMEOUT} s "
    "(${version_result}). The router must answer --version and exit without "
    "opening a modal dialog. Captured output:\n${captured}")
endif()

if(NOT version_result EQUAL 0)
  message(FATAL_ERROR
    "${BINARY} --version exited with status ${version_result}. "
    "Captured output:\n${captured}")
endif()

if(NOT captured MATCHES "openi2pd version")
  # On Windows the router is a GUI-subsystem executable (WIN32_APP): it has
  # no console of its own and its standard handles are not guaranteed to be
  # the inherited capture pipes, so the banner can be unobservable while the
  # process is perfectly healthy. The exit status above is the contract
  # there; on every console platform the banner is required.
  if(WIN32)
    message(STATUS
      "i2pd --version exited 0 without an observable banner "
      "(windowed build); the exit status is authoritative")
  else()
    message(FATAL_ERROR
      "${BINARY} --version exited 0 without printing the version banner. "
      "Captured output:\n${captured}")
  endif()
endif()

message(STATUS "i2pd-version: ${BINARY} reports its version")
