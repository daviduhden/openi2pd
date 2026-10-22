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
set(s6_down_signal "${PROJECT_ROOT}/contrib/s6/i2pd/down-signal")
set(s6_timeout_kill "${PROJECT_ROOT}/contrib/s6/i2pd/timeout-kill")
set(s6rc_type "${PROJECT_ROOT}/contrib/s6-rc/source/i2pd/type")
set(s6rc_run "${PROJECT_ROOT}/contrib/s6-rc/source/i2pd/run")
set(s6rc_deps "${PROJECT_ROOT}/contrib/s6-rc/source/i2pd/dependencies.d")
set(s6rc_down_signal "${PROJECT_ROOT}/contrib/s6-rc/source/i2pd/down-signal")
set(s6rc_timeout_kill "${PROJECT_ROOT}/contrib/s6-rc/source/i2pd/timeout-kill")
set(shepherd_script "${PROJECT_ROOT}/contrib/shepherd/i2pd.scm")
set(upstart_job "${PROJECT_ROOT}/contrib/upstart/i2pd.upstart")
set(dinit_service "${PROJECT_ROOT}/contrib/dinit/i2pd")
set(openbsd_rc "${PROJECT_ROOT}/contrib/openbsd/i2pd.rc")
set(openbsd_login "${PROJECT_ROOT}/contrib/openbsd/i2pd.login")
set(openbsd_conf "${PROJECT_ROOT}/contrib/openbsd/i2pd.conf")

read_required("${systemd_unit}" systemd_text)
read_required("${PROJECT_ROOT}/contrib/sysusers.d/i2pd.conf" systemd_sysusers_text)
read_required("${PROJECT_ROOT}/contrib/tmpfiles.d/i2pd.conf" systemd_tmpfiles_text)
read_required("${sysvinit_script}" sysvinit_text)
read_required("${openrc_script}" openrc_text)
read_required("${openrc_conf}" openrc_conf_text)
read_required("${runit_run}" runit_run_text)
read_required("${runit_log_run}" runit_log_text)
read_required("${s6_run}" s6_run_text)
read_required("${s6_down_signal}" s6_down_signal_text)
read_required("${s6_timeout_kill}" s6_timeout_kill_text)
read_required("${s6rc_type}" s6rc_type_text)
read_required("${s6rc_run}" s6rc_run_text)
read_required("${s6rc_down_signal}" s6rc_down_signal_text)
read_required("${s6rc_timeout_kill}" s6rc_timeout_kill_text)
read_required("${shepherd_script}" shepherd_text)
read_required("${upstart_job}" upstart_text)
read_required("${dinit_service}" dinit_text)
read_required("${openbsd_rc}" openbsd_rc_text)
read_required("${openbsd_login}" openbsd_login_text)
read_required("${openbsd_conf}" openbsd_conf_text)

# No service file may request the removed self-daemonising mode, claim
# notification readiness or assert full privilege separation. Those properties
# stay with the supervisor/service manager and the native IPC workers.
foreach(service_name IN ITEMS
    "systemd unit" "sysvinit script" "OpenRC script" "OpenRC conf.d"
    "runit run" "runit log/run" "s6 run" "s6-rc type" "shepherd script"
    "upstart job" "dinit service" "openbsd rc.d")
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
  elseif(service_name STREQUAL "upstart job")
    set(service_text "${upstart_text}")
  elseif(service_name STREQUAL "dinit service")
    set(service_text "${dinit_text}")
  elseif(service_name STREQUAL "openbsd rc.d")
    set(service_text "${openbsd_rc_text}")
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
    "TimeoutStopSec=660"
    "SyslogIdentifier=open-i2pd"
    "--foreground")
  require_text("${systemd_text}" "${required}" "systemd unit")
endforeach()
forbid_text("${systemd_text}" "Type=forking" "systemd unit")
forbid_text("${systemd_text}" "Alias=" "systemd unit")
require_text("${systemd_sysusers_text}" "u i2pd" "systemd sysusers")
require_text("${systemd_tmpfiles_text}" "/run/i2pd" "systemd tmpfiles")
require_text("${systemd_tmpfiles_text}" "/var/lib/i2pd" "systemd tmpfiles")

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
require_text("${openrc_text}" "stopsig" "OpenRC script")
require_text("${openrc_text}" "umask=0077" "OpenRC script")
require_text("${openrc_text}" "dirname" "OpenRC script")
require_text("${openrc_text}" "I2PD_PIDFILE" "OpenRC script")
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
require_text("${s6_down_signal_text}" "INT" "s6 down-signal")
require_text("${s6_timeout_kill_text}" "660000" "s6 timeout-kill")
require_text("${s6rc_down_signal_text}" "INT" "s6-rc down-signal")
require_text("${s6rc_timeout_kill_text}" "660000" "s6-rc timeout-kill")
if(NOT IS_DIRECTORY "${s6rc_deps}")
  message(FATAL_ERROR "s6-rc service has no dependencies.d directory: ${s6rc_deps}")
endif()
file(GLOB s6rc_dependency_files "${s6rc_deps}/*")
list(LENGTH s6rc_dependency_files s6rc_dependency_count)
if(s6rc_dependency_count EQUAL 0)
  message(FATAL_ERROR "s6-rc service declares no dependencies in ${s6rc_deps}")
endif()

# Upstart: foreground main process, restricted credentials, private umask and
# a SIGINT stop with the same bounded escalation as the other definitions.
foreach(required IN ITEMS
    "respawn"
    "setuid i2pd"
    "setgid i2pd"
    "umask 0077"
    "limit nofile 8192 8192"
    "--foreground"
    "kill signal INT"
    "kill timeout 660")
  require_text("${upstart_text}" "${required}" "upstart job")
endforeach()

# dinit: foreground process supervision, a restricted account and the same
# SIGINT/660-second stop policy. It remains an unselected sample.
foreach(required IN ITEMS
    "type = process"
    "run-as = i2pd"
    "--foreground"
    "term-signal = INT"
    "stop-timeout = 660")
  require_text("${dinit_text}" "${required}" "dinit service")
endforeach()

# OpenBSD: rc.subr foreground/background semantics, packaged paths, login
# class and the packaged configuration. The port's SUBST must preserve these
# variables in the rendered rc.d file.
foreach(required IN ITEMS
    "daemon_user=\"_i2pd\""
    "rc_bg=YES"
    "daemon_logger="
    "daemon_timeout=660"
    "--foreground"
    "tunnels.conf.d"
    "share/i2pd/certificates"
    "--log=syslog")
  require_text("${openbsd_rc_text}" "${required}" "openbsd rc.d")
endforeach()
require_text("${openbsd_login_text}" "openfiles-cur=8192" "openbsd login class")
require_text("${openbsd_login_text}" "umask=0077" "openbsd login class")
require_text("${openbsd_conf_text}" "tunnelsdir = /etc/i2pd/tunnels.conf.d" "openbsd config")
require_text("${openbsd_conf_text}" "certsdir = /usr/local/share/i2pd/certificates" "openbsd config")
require_text("${openbsd_conf_text}" "log = syslog" "openbsd config")

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
