# See the LICENSE file at the top of the project tree for copyright and
# license details.

function(i2pd_add_windows_manual version output)
  if(NOT CMAKE_HOST_WIN32)
    message(FATAL_ERROR "Microsoft HTML Help Workshop requires a Windows build host")
  endif()

  find_package(Perl REQUIRED)
  find_program(I2PD_MANDOC NAMES mandoc REQUIRED)
  find_program(I2PD_HHC NAMES hhc HINTS
    "$ENV{ProgramFiles}/HTML Help Workshop"
    "$ENV{ProgramFiles\(x86\)}/HTML Help Workshop" REQUIRED)
  find_program(I2PD_7ZIP NAMES 7z 7zz HINTS "$ENV{ProgramFiles}/7-Zip" REQUIRED)

  set(manual_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../man")
  set(generator "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../windows-manual.pl")
  set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/windows-manual")
  set(chm "${output_dir}/openi2pd.chm")
  file(GLOB_RECURSE manuals CONFIGURE_DEPENDS LIST_DIRECTORIES false "${manual_root}/*")
  # A removed page changes the inventory even when all remaining input
  # timestamps predate the CHM.
  set(inventory "${CMAKE_CURRENT_BINARY_DIR}/windows-manual-inputs.txt")
  string(REPLACE ";" "\n" inventory_content "${manuals}")
  file(GENERATE OUTPUT "${inventory}" CONTENT "${inventory_content}\n")
  add_custom_command(OUTPUT "${chm}"
    COMMAND "${PERL_EXECUTABLE}" "${generator}" "${manual_root}"
      "${version}" "${output_dir}" "${I2PD_MANDOC}" "${I2PD_HHC}" "${I2PD_7ZIP}"
    DEPENDS "${generator}" "${manuals}" "${inventory}" "${manual_root}/../libi2pd/version.h"
      "${I2PD_MANDOC}" "${I2PD_HHC}" "${I2PD_7ZIP}"
    COMMENT "Rendering and verifying the openi2pd Windows manual"
    VERBATIM)
  add_custom_target(windows-manual ALL DEPENDS "${chm}")
  set("${output}" "${chm}" PARENT_SCOPE)
endfunction()
