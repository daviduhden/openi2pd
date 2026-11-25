# See the LICENSE file at the top of the project tree for copyright and
# license details.

# A child can report a sanitizer failure while its parent exits zero.
# Match report headers, since tools also print sanitizer names at startup.
set(I2PD_SANITIZER_REPORT
  "(ERROR|WARNING|SUMMARY): (Address|Leak|Thread|UndefinedBehavior)Sanitizer:"
  "AddressSanitizer:DEADLYSIGNAL"
  ":[0-9]+:[0-9]+: runtime error:")

# Pass the variable name so captured operands never enter diagnostics.
function(i2pd_require_clean_stderr variable)
  foreach(pattern IN LISTS I2PD_SANITIZER_REPORT)
    if(${variable} MATCHES "${pattern}")
      message(FATAL_ERROR "Native child emitted a sanitizer diagnostic")
    endif()
  endforeach()
endfunction()
