# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check services.
#
# Compile-free source contract for the init-system service files. The checks
# describe the required supervision semantics; they never start a service,
# inspect a live process or call an external binary.
#

if(NOT PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

function(read_required path out_var)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "Missing canonical service file: ${path}")
  endif()
  file(READ "${path}" contents)
  set(${out_var} "${contents}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "${description}: missing '${needle}'")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR "${description}: forbidden '${needle}'")
  endif()
endfunction()

function(require_any text description)
  foreach(needle IN LISTS ARGN)
    string(FIND "${text}" "${needle}" position)
    if(NOT position EQUAL -1)
      return()
    endif()
  endforeach()
  message(FATAL_ERROR "${description}: none of '${ARGN}' found")
endfunction()

set(systemd_unit "${PROJECT_ROOT}/contrib/i2pd.service")
set(sysvinit_script "${PROJECT_ROOT}/contrib/sysvinit/i2pd")
set(openrc_script "${PROJECT_ROOT}/contrib/openrc/i2pd.openrc")
set(openrc_conf "${PROJECT_ROOT}/contrib/openrc/conf.d/i2pd")
set(runit_run "${PROJECT_ROOT}/contrib/runit/i2pd/run")
set(runit_log_run "${PROJECT_ROOT}/contrib/runit/i2pd/log/run")
set(s6_run "${PROJECT_ROOT}/contrib/s6/i2pd/run")
set(s6rc_type "${PROJECT_ROOT}/contrib/s6-rc/source/i2pd/type")
set(s6rc_run "${PROJECT_ROOT}/contrib/s6-rc/source/i2pd/run")
set(s6rc_deps "${PROJECT_ROOT}/contrib/s6-rc/source/i2pd/dependencies.d")
set(shepherd_script "${PROJECT_ROOT}/contrib/shepherd/i2pd.scm")

read_required("${systemd_unit}" systemd_text)
read_required("${sysvinit_script}" sysvinit_text)
read_required("${openrc_script}" openrc_text)
read_required("${openrc_conf}" openrc_conf_text)
read_required("${runit_run}" runit_run_text)
read_required("${runit_log_run}" runit_log_text)
read_required("${s6_run}" s6_run_text)
read_required("${s6rc_type}" s6rc_type_text)
read_required("${s6rc_run}" s6rc_run_text)
read_required("${shepherd_script}" shepherd_text)

# No service file may request the removed self-daemonising mode, claim
# notification readiness or assert full privilege separation. Those properties
# stay with the supervisor/service manager and the native IPC workers.
foreach(service_name IN ITEMS
    "systemd unit" "sysvinit script" "OpenRC script" "OpenRC conf.d"
    "runit run" "runit log/run" "s6 run" "s6-rc type" "shepherd script")
  if(service_name STREQUAL "systemd unit")
    set(service_text "${systemd_text}")
  elseif(service_name STREQUAL "sysvinit script")
    set(service_text "${sysvinit_text}")
  elseif(service_name STREQUAL "OpenRC script")
    set(service_text "${openrc_text}")
  elseif(service_name STREQUAL "OpenRC conf.d")
    set(service_text "${openrc_conf_text}")
  elseif(service_name STREQUAL "runit run")
    set(service_text "${runit_run_text}")
  elseif(service_name STREQUAL "runit log/run")
    set(service_text "${runit_log_text}")
  elseif(service_name STREQUAL "s6 run")
    set(service_text "${s6_run_text}")
  elseif(service_name STREQUAL "s6-rc type")
    set(service_text "${s6rc_type_text}")
  else()
    set(service_text "${shepherd_text}")
  endif()
  foreach(forbidden IN ITEMS
      "--daemon"
      "sd_notify"
      "Type=notify"
      "READY=1"
      "full privilege separation"
      "fully privilege-separated"
      "complete privilege separation")
    forbid_text("${service_text}" "${forbidden}" "${service_name}")
  endforeach()
endforeach()

# systemd: foreground Type=exec with journal-backed output and a strict but
# writable sandbox. Directory ownership is delegated to State/RuntimeDirectory.
foreach(required IN ITEMS
    "Type=exec"
    "ExecStart="
    "User=i2pd"
    "Group=i2pd"
    "StateDirectory="
    "RuntimeDirectory="
    "NoNewPrivileges=yes"
    "ProtectSystem=strict"
    "ReadWritePaths="
    "Restart=on-failure"
    "StandardOutput=journal"
    "StandardError=journal"
    "--foreground")
  require_text("${systemd_text}" "${required}" "systemd unit")
endforeach()
forbid_text("${systemd_text}" "Type=forking" "systemd unit")

# SysV: LSB init metadata plus start-stop-daemon backgrounding and privilege
# dropping. The script must expose the full action set expected by policy
# tooling.
foreach(required IN ITEMS
    "### BEGIN INIT INFO"
    "### END INIT INFO"
    "start-stop-daemon"
    "--background"
    "--chuid")
  require_text("${sysvinit_text}" "${required}" "sysvinit script")
endforeach()
foreach(action IN ITEMS "start)" "stop)" "restart)" "status)" "force-reload")
  require_text("${sysvinit_text}" "${action}" "sysvinit script")
endforeach()
require_text("${sysvinit_text}" "--foreground" "sysvinit script")

# OpenRC: the native runscript, with either supervise-daemon or the
# start-stop-daemon/chuid equivalent for privilege dropping.
require_text("${openrc_text}" "openrc-run" "OpenRC script")
require_text("${openrc_text}" "--foreground" "OpenRC script")
require_any("${openrc_text}" "OpenRC script"
  "supervise-daemon" "command_user" "--chuid")

# runit: a foreground run script using chpst, plus a separate svlogd logger.
require_text("${runit_run_text}" "exec" "runit run")
require_text("${runit_run_text}" "chpst" "runit run")
require_text("${runit_run_text}" "--foreground" "runit run")
require_text("${runit_log_text}" "svlogd" "runit log/run")

# s6: execline run script with s6-setuidgid; s6-rc declares a longrun with at
# least one dependency edge.
require_text("${s6_run_text}" "execlineb" "s6 run")
require_text("${s6_run_text}" "s6-setuidgid" "s6 run")
require_text("${s6_run_text}" "--foreground" "s6 run")
require_text("${s6rc_type_text}" "longrun" "s6-rc type")
require_text("${s6rc_run_text}" "--foreground" "s6-rc run")
if(NOT IS_DIRECTORY "${s6rc_deps}")
  message(FATAL_ERROR "s6-rc service has no dependencies.d directory: ${s6rc_deps}")
endif()
file(GLOB s6rc_dependency_files "${s6rc_deps}/*")
list(LENGTH s6rc_dependency_files s6rc_dependency_count)
if(s6rc_dependency_count EQUAL 0)
  message(FATAL_ERROR "s6-rc service declares no dependencies in ${s6rc_deps}")
endif()

# Shepherd: Guile service constructors, an explicit i2pd user and no shell
# readiness loop.
foreach(required IN ITEMS
    "make-forkexec-constructor"
    "make-kill-destructor"
    "#:user"
    "\"i2pd\""
    "--foreground")
  require_text("${shepherd_text}" "${required}" "shepherd script")
endforeach()
foreach(forbidden IN ITEMS "while " "until " "sleep ")
  forbid_text("${shepherd_text}" "${forbidden}" "shepherd script")
endforeach()

message(STATUS "services-static: OK")
