# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check services.
#
# Compile-free source contract for the init-system service files and the
# service-management documentation. The checks describe supervision, stop and
# restart semantics; they never start a service, inspect a live process or call
# an external binary.
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

# Exact-value files (down-signal, timeout-kill, type, ...) must contain one
# line and nothing else. A comment or a longer word must not satisfy them.
function(require_exact_value path expected description)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "${description}: missing file ${path}")
  endif()
  file(STRINGS "${path}" lines)
  list(LENGTH lines count)
  if(NOT count EQUAL 1)
    message(FATAL_ERROR "${description}: expected exactly one line, got ${count}")
  endif()
  list(GET lines 0 value)
  string(STRIP "${value}" value)
  if(NOT value STREQUAL expected)
    message(FATAL_ERROR "${description}: expected '${expected}', got '${value}'")
  endif()
endfunction()

function(require_single_line text token description)
  string(FIND "${text}" "${token}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "${description}: missing '${token}'")
  endif()
  string(REGEX MATCHALL "${token}" matches "${text}")
  list(LENGTH matches count)
  if(NOT count EQUAL 1)
    message(FATAL_ERROR "${description}: '${token}' must appear once, found ${count}")
  endif()
endfunction()

# Compare the daemon argv between the plain s6 and s6-rc definitions. Comments
# and line continuations are normalised so the two trees cannot drift apart.
function(daemon_argv text out_var)
  string(REGEX REPLACE ".*/usr/bin/i2pd" "/usr/bin/i2pd" tail "${text}")
  string(REGEX REPLACE "[ \t\r\n]+" " " tail "${tail}")
  string(REPLACE "\\ " " " tail "${tail}")
  string(STRIP "${tail}" tail)
  set(${out_var} "${tail}" PARENT_SCOPE)
endfunction()

set(systemd_unit "${PROJECT_ROOT}/contrib/i2pd.service")
set(debian_systemd "${PROJECT_ROOT}/debian/i2pd.service")
set(systemd_sysusers "${PROJECT_ROOT}/contrib/sysusers.d/i2pd.conf")
set(systemd_tmpfiles "${PROJECT_ROOT}/contrib/tmpfiles.d/i2pd.conf")
set(debian_tmpfiles "${PROJECT_ROOT}/debian/i2pd.tmpfiles")
set(sysvinit_script "${PROJECT_ROOT}/contrib/sysvinit/i2pd")
set(debian_sysvinit "${PROJECT_ROOT}/debian/i2pd.init")
set(openrc_script "${PROJECT_ROOT}/contrib/openrc/i2pd.openrc")
set(openrc_conf "${PROJECT_ROOT}/contrib/openrc/conf.d/i2pd")
set(runit_run "${PROJECT_ROOT}/contrib/runit/i2pd/run")
set(runit_finish "${PROJECT_ROOT}/contrib/runit/i2pd/finish")
set(runit_log_run "${PROJECT_ROOT}/contrib/runit/i2pd/log/run")
set(runit_log_config "${PROJECT_ROOT}/contrib/runit/i2pd/log/config")
set(s6_run "${PROJECT_ROOT}/contrib/s6/i2pd/run")
set(s6_finish "${PROJECT_ROOT}/contrib/s6/i2pd/finish")
set(s6_down_signal "${PROJECT_ROOT}/contrib/s6/i2pd/down-signal")
set(s6_timeout_kill "${PROJECT_ROOT}/contrib/s6/i2pd/timeout-kill")
set(s6_timeout_finish "${PROJECT_ROOT}/contrib/s6/i2pd/timeout-finish")
set(s6rc_type "${PROJECT_ROOT}/contrib/s6-rc/source/i2pd/type")
set(s6rc_run "${PROJECT_ROOT}/contrib/s6-rc/source/i2pd/run")
set(s6rc_finish "${PROJECT_ROOT}/contrib/s6-rc/source/i2pd/finish")
set(s6rc_deps "${PROJECT_ROOT}/contrib/s6-rc/source/i2pd/dependencies.d")
set(s6rc_down_signal "${PROJECT_ROOT}/contrib/s6-rc/source/i2pd/down-signal")
set(s6rc_timeout_kill "${PROJECT_ROOT}/contrib/s6-rc/source/i2pd/timeout-kill")
set(s6rc_timeout_finish "${PROJECT_ROOT}/contrib/s6-rc/source/i2pd/timeout-finish")
set(shepherd_script "${PROJECT_ROOT}/contrib/shepherd/i2pd.scm")
set(upstart_job "${PROJECT_ROOT}/contrib/upstart/i2pd.upstart")
set(dinit_service "${PROJECT_ROOT}/contrib/dinit/i2pd")
set(openbsd_rc "${PROJECT_ROOT}/contrib/openbsd/i2pd.rc")
set(openbsd_login "${PROJECT_ROOT}/contrib/openbsd/i2pd.login")
set(openbsd_conf "${PROJECT_ROOT}/contrib/openbsd/i2pd.conf")
set(openbsd_port_make "${PROJECT_ROOT}/contrib/openbsd/port/Makefile")
set(openbsd_plist "${PROJECT_ROOT}/contrib/openbsd/port/PLIST")
set(openbsd_descr "${PROJECT_ROOT}/contrib/openbsd/port/pkg/DESCR")
set(service_doc "${PROJECT_ROOT}/docs/service-management.md")

read_required("${systemd_unit}" systemd_text)
read_required("${debian_systemd}" debian_systemd_text)
read_required("${systemd_sysusers}" systemd_sysusers_text)
read_required("${systemd_tmpfiles}" systemd_tmpfiles_text)
read_required("${debian_tmpfiles}" debian_tmpfiles_text)
read_required("${sysvinit_script}" sysvinit_text)
read_required("${debian_sysvinit}" debian_sysvinit_text)
read_required("${openrc_script}" openrc_text)
read_required("${openrc_conf}" openrc_conf_text)
read_required("${runit_run}" runit_run_text)
read_required("${runit_finish}" runit_finish_text)
read_required("${runit_log_run}" runit_log_text)
read_required("${runit_log_config}" runit_log_config_text)
read_required("${s6_run}" s6_run_text)
read_required("${s6_finish}" s6_finish_text)
read_required("${s6rc_type}" s6rc_type_text)
read_required("${s6rc_run}" s6rc_run_text)
read_required("${s6rc_finish}" s6rc_finish_text)
read_required("${shepherd_script}" shepherd_text)
read_required("${upstart_job}" upstart_text)
read_required("${dinit_service}" dinit_text)
read_required("${openbsd_rc}" openbsd_rc_text)
read_required("${openbsd_login}" openbsd_login_text)
read_required("${openbsd_conf}" openbsd_conf_text)
read_required("${openbsd_port_make}" openbsd_port_make_text)
read_required("${openbsd_plist}" openbsd_plist_text)
read_required("${openbsd_descr}" openbsd_descr_text)
read_required("${service_doc}" service_doc_text)

# No service file may request the removed self-daemonising mode, claim
# notification readiness or assert full privilege separation. Those properties
# stay with the supervisor/service manager and the native IPC workers.
foreach(service_name IN ITEMS
    "systemd unit" "sysvinit script" "OpenRC script" "OpenRC conf.d"
    "runit run" "runit finish" "runit log/run" "s6 run" "s6 finish"
    "s6-rc type" "s6-rc finish" "shepherd script" "upstart job"
    "dinit service" "openbsd rc.d")
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
  elseif(service_name STREQUAL "runit finish")
    set(service_text "${runit_finish_text}")
  elseif(service_name STREQUAL "runit log/run")
    set(service_text "${runit_log_text}")
  elseif(service_name STREQUAL "s6 run")
    set(service_text "${s6_run_text}")
  elseif(service_name STREQUAL "s6 finish")
    set(service_text "${s6_finish_text}")
  elseif(service_name STREQUAL "s6-rc type")
    set(service_text "${s6rc_type_text}")
  elseif(service_name STREQUAL "s6-rc finish")
    set(service_text "${s6rc_finish_text}")
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
      "NOTIFY_SOCKET"
      "Type=notify"
      "READY=1"
      "notification-fd"
      "full privilege separation"
      "fully privilege-separated"
      "complete privilege separation")
    forbid_text("${service_text}" "${forbidden}" "${service_name}")
  endforeach()
endforeach()

# systemd: foreground Type=exec with journal-backed output, a bounded stop
# window and a strict but writable sandbox. Directory ownership is delegated to
# State/Runtime/LogsDirectory so the unit does not depend on tmpfiles order.
foreach(required IN ITEMS
    "Type=exec"
    "ExecStart="
    "ExecReload=/bin/kill -HUP $MAINPID"
    "Documentation=man:i2pd(1)"
    "Wants=network-online.target"
    "After=network-online.target"
    "User=i2pd"
    "Group=i2pd"
    "StateDirectory=i2pd"
    "StateDirectoryMode=0700"
    "RuntimeDirectory=i2pd"
    "RuntimeDirectoryMode=0700"
    "LogsDirectory=i2pd"
    "LogsDirectoryMode=0700"
    "WorkingDirectory=/var/lib/i2pd"
    "UMask=0077"
    "NoNewPrivileges=yes"
    "ProtectSystem=strict"
    "ReadWritePaths=/var/lib/i2pd /run/i2pd"
    "ProtectHome=yes"
    "PrivateTmp=yes"
    "RestrictNamespaces=yes"
    "CapabilityBoundingSet="
    "Restart=on-failure"
    "RestartSec=5s"
    "StartLimitIntervalSec=300"
    "StartLimitBurst=5"
    "LimitNOFILE=8192"
    "StandardOutput=journal"
    "StandardError=journal"
    "SyslogIdentifier=open-i2pd"
    "KillSignal=SIGINT"
    "KillMode=mixed"
    "TimeoutStopSec=660"
    "SendSIGKILL=yes"
    "WantedBy=multi-user.target"
    "--service"
    "--foreground"
    "--pidfile=/run/i2pd/i2pd.pid"
    "--log=stdout")
  require_text("${systemd_text}" "${required}" "systemd unit")
endforeach()
require_single_line("${systemd_text}" "Type=exec" "systemd unit")
require_single_line("${systemd_text}" "ExecStart=" "systemd unit")
require_single_line("${systemd_text}" "CapabilityBoundingSet=" "systemd unit")
foreach(forbidden IN ITEMS "Type=forking" "Type=notify" "Type=simple" "Alias="
    "KillMode=control-group" "KillMode=process")
  forbid_text("${systemd_text}" "${forbidden}" "systemd unit")
endforeach()
require_text("${systemd_sysusers_text}" "u i2pd" "systemd sysusers")
require_text("${systemd_sysusers_text}" "/usr/sbin/nologin" "systemd sysusers")
foreach(directory IN ITEMS "/var/lib/i2pd" "/run/i2pd" "/var/log/i2pd")
  require_text("${systemd_tmpfiles_text}" "${directory}" "systemd tmpfiles")
endforeach()
require_text("${debian_tmpfiles_text}" "/var/lib/i2pd 0700 i2pd i2pd" "Debian tmpfiles")
# The packaged unit and the Debian copy must not drift.
if(NOT systemd_text STREQUAL debian_systemd_text)
  message(FATAL_ERROR "debian/i2pd.service differs from contrib/i2pd.service")
endif()

# SysV: LSB metadata, start-stop-daemon privilege dropping, an identity-checked
# pidfile, a verified stop and a real force-reload. The script must expose the
# full action set expected by policy tooling.
foreach(required IN ITEMS
    "### BEGIN INIT INFO"
    "### END INIT INFO"
    "# Provides:          i2pd"
    "# Required-Start:    $local_fs $remote_fs $network"
    "# Should-Start:      $syslog"
    "# Default-Start:     2 3 4 5"
    "# Default-Stop:      0 1 6"
    "start-stop-daemon"
    "--background"
    "--chuid"
    "--oknodo"
    "--retry=INT/660/KILL/5"
    "umask 0077"
    "ulimit -n"
    "pid_matches_daemon"
    "/proc/$_pid/exe"
    "set -f"
    "set +f"
    "log_daemon_msg()"
    "force-reload)"
    "status)"
    "--foreground"
    "--service"
    "--pidfile=\"$I2PD_PIDFILE\"")
  require_text("${sysvinit_text}" "${required}" "sysvinit script")
endforeach()
foreach(action IN ITEMS "start)" "stop)" "restart)" "status)" "reload)")
  require_text("${sysvinit_text}" "${action}" "sysvinit script")
endforeach()
require_text("${sysvinit_text}" "1|true|yes) I2PD_ENABLED=yes" "sysvinit script")
require_text("${sysvinit_text}" [=[''|*[!0-9]*|0*) return 1 ;;]=]
  "sysvinit read_pid must reject zero and non-canonical PID strings")
require_text("${sysvinit_text}" [=[_daemon_real=$(readlink -f "$DAEMON" 2>/dev/null) || return 1]=]
  "sysvinit executable resolution must fail closed")
forbid_text("${sysvinit_text}" [=[_daemon_real=$(readlink -f "$DAEMON" 2>/dev/null) || return 0]=]
  "sysvinit must not accept an unresolved daemon path")
require_text("${sysvinit_text}" "0) log_action_msg" "sysvinit script")
require_text("${sysvinit_text}" "1) log_failure_msg" "sysvinit script")
require_text("${sysvinit_text}" "3) log_action_msg" "sysvinit script")
if(NOT sysvinit_text STREQUAL debian_sysvinit_text)
  message(FATAL_ERROR "debian/i2pd.init differs from contrib/sysvinit/i2pd")
endif()

# OpenRC: supervise-daemon with privilege dropping, a checked directory setup,
# a parsed retry grammar and a respawn budget matching systemd.
foreach(required IN ITEMS
    "openrc-run"
    "supervise-daemon"
    "command_user=\"\${I2PD_USER}:\${I2PD_GROUP}\""
    "directory=\"\${I2PD_DATADIR}\""
    "respawn_delay=5"
    "respawn_max=5"
    "respawn_period=300"
    "rc_ulimit:=-n 8192"
    "retry:=SIGINT/660"
    "stopsig:=SIGTERM"
    "umask=0077"
    "required_files=\"\${I2PD_CONF} \${I2PD_TUNCONF}\""
    "required_dirs=\"\${I2PD_TUNNELSDIR}\""
    "checkpath -d -m 0700"
    "checkpath -f -m 0640"
    "output_log="
    "error_log="
    "need localmount"
    "use net"
    "use logger"
    "supervise-daemon --signal HUP"
    "supervise-daemon --signal USR1"
    "--foreground"
    "--service")
  require_text("${openrc_text}" "${required}" "OpenRC script")
endforeach()
foreach(forbidden IN ITEMS "command_background=true")
  forbid_text("${openrc_text}" "${forbidden}" "OpenRC script")
endforeach()
foreach(required IN ITEMS "I2PD_USER=" "I2PD_DATADIR=" "I2PD_PIDFILE="
    "I2PD_LOG=" "stopsig")
  require_text("${openrc_conf_text}" "${required}" "OpenRC conf.d")
endforeach()

# runit: a foreground chpst service with a bounded descriptor limit, an account
# and configuration precondition, a backoff finish script and an svlogd policy.
foreach(required IN ITEMS
    "#!/bin/sh"
    "set -eu"
    "umask 077"
    "id -u i2pd"
    "/etc/i2pd/i2pd.conf"
    "/usr/bin/install -d -m 0700"
    "exec 2>&1"
    "/usr/bin/chpst -o 8192 -u i2pd:i2pd"
    "--foreground"
    "--service"
    "--pidfile=/run/i2pd/i2pd.pid"
    "--log=stdout")
  require_text("${runit_run_text}" "${required}" "runit run")
endforeach()
require_text("${runit_finish_text}" "/bin/sleep 5" "runit finish")
forbid_text("${runit_finish_text}" "rm " "runit finish")
require_text("${runit_log_text}" "svlogd -tt" "runit log/run")
require_text("${runit_log_text}" "chpst" "runit log/run")
require_text("${runit_log_config_text}" "s1000000" "runit log config")
require_text("${runit_log_config_text}" "n10" "runit log config")
# Stock runsv has no per-service stop timeout or down-signal file. Adding one
# would imply a KILL window the supervisor does not implement.
if(EXISTS "${PROJECT_ROOT}/contrib/runit/i2pd/down-signal")
  message(FATAL_ERROR "runit run has no down-signal support; do not ship one")
endif()

# s6: execline run script with a descriptor limit, s6-setuidgid and the same
# stop/restart limits as the other managers. A finish script provides backoff.
foreach(required IN ITEMS
    "execlineb"
    "foreground { /usr/bin/install -d -m 0700"
    "umask 0077"
    "s6-softlimit -o 8192"
    "s6-setuidgid i2pd"
    "--foreground"
    "--service"
    "--pidfile=/run/i2pd/i2pd.pid"
    "--log=syslog")
  require_text("${s6_run_text}" "${required}" "s6 run")
endforeach()
require_text("${s6_finish_text}" "/bin/sleep 5" "s6 finish")
require_exact_value("${s6_down_signal}" "INT" "s6 down-signal")
require_exact_value("${s6_timeout_kill}" "660000" "s6 timeout-kill")
require_exact_value("${s6_timeout_finish}" "10000" "s6 timeout-finish")
if(EXISTS "${PROJECT_ROOT}/contrib/s6/i2pd/notification-fd")
  message(FATAL_ERROR "s6 service must not claim a readiness notification")
endif()

# s6-rc: one longrun, one declared dependency marker, identical stop and
# argv semantics, and the same backoff.
require_exact_value("${s6rc_type}" "longrun" "s6-rc type")
require_text("${s6rc_run_text}" "--foreground" "s6-rc run")
require_text("${s6rc_run_text}" "s6-softlimit -o 8192" "s6-rc run")
require_text("${s6rc_finish_text}" "/bin/sleep 5" "s6-rc finish")
require_exact_value("${s6rc_down_signal}" "INT" "s6-rc down-signal")
require_exact_value("${s6rc_timeout_kill}" "660000" "s6-rc timeout-kill")
require_exact_value("${s6rc_timeout_finish}" "10000" "s6-rc timeout-finish")
if(NOT IS_DIRECTORY "${s6rc_deps}")
  message(FATAL_ERROR "s6-rc service has no dependencies.d directory: ${s6rc_deps}")
endif()
file(GLOB s6rc_dependency_files "${s6rc_deps}/*")
list(LENGTH s6rc_dependency_files s6rc_dependency_count)
if(NOT s6rc_dependency_count EQUAL 1)
  message(FATAL_ERROR
    "s6-rc must declare exactly the 'base' dependency, found ${s6rc_dependency_count}")
endif()
if(NOT EXISTS "${s6rc_deps}/base")
  message(FATAL_ERROR "s6-rc dependency must be the conventional 'base' bundle")
endif()
file(SIZE "${s6rc_deps}/base" s6rc_base_size)
if(NOT s6rc_base_size EQUAL 0)
  message(FATAL_ERROR "s6-rc dependency marker 'base' must be empty")
endif()
daemon_argv("${s6_run_text}" s6_argv)
daemon_argv("${s6rc_run_text}" s6rc_argv)
if(NOT s6_argv STREQUAL s6rc_argv)
  message(FATAL_ERROR
    "s6 and s6-rc daemon argv differ:\n  s6:    ${s6_argv}\n  s6-rc: ${s6rc_argv}")
endif()

# Upstart: foreground main process, restricted credentials, explicit console
# capture, private umask and a SIGINT stop with the same bounded escalation.
foreach(required IN ITEMS
    "description \"openi2pd router\""
    "start on runlevel [2345]"
    "stop on runlevel [016]"
    "console log"
    "respawn"
    "respawn limit 5 60"
    "setuid i2pd"
    "setgid i2pd"
    "chdir /var/lib/i2pd"
    "umask 0077"
    "limit nofile 8192 8192"
    "kill signal INT"
    "kill timeout 660"
    "--foreground"
    "--service"
    "--pidfile=/var/lib/i2pd/i2pd.pid"
    "--log=syslog")
  require_text("${upstart_text}" "${required}" "upstart job")
endforeach()

# dinit: foreground process supervision, a restricted account, a private log
# file and the same SIGINT/660-second stop policy. It remains an unselected
# sample, so a missing /var/log/i2pd is a documented precondition.
foreach(required IN ITEMS
    "type = process"
    "run-as = i2pd"
    "working-dir = /var/lib/i2pd"
    "log-type = file"
    "logfile = /var/log/i2pd/i2pd.log"
    "logfile-uid = i2pd"
    "logfile-gid = i2pd"
    "logfile-permissions = 0600"
    "restart = on-failure"
    "smooth-recovery = true"
    "restart-delay = 5"
    "restart-limit-count = 5"
    "restart-limit-interval = 300"
    "term-signal = INT"
    "stop-timeout = 660"
    "rlimit-nofile = 8192"
    "after = ntpd"
    "--foreground"
    "--service")
  require_text("${dinit_text}" "${required}" "dinit service")
endforeach()

# OpenBSD: rc.subr foreground/background semantics, the login class, packaged
# paths and a stop signal that reaches the daemon's drain. The port's SUBST
# must preserve these variables in the rendered rc.d file.
foreach(required IN ITEMS
    "daemon=\"\${TRUEPREFIX}/bin/i2pd\""
    "daemon_user=\"_i2pd\""
    "daemon_execdir=\"\${LOCALSTATEDIR}/i2pd\""
    "pidfile=\"\${LOCALSTATEDIR}/i2pd/i2pd.pid\""
    "pexp=\"\${TRUEPREFIX}/bin/i2pd\""
    "rc_bg=YES"
    "rc_stop_signal=INT"
    "daemon_logger="
    "daemon_timeout=660"
    ". /etc/rc.d/rc.subr"
    "rc_cmd \"$1\""
    "--foreground"
    "--service"
    "--datadir=\${LOCALSTATEDIR}/i2pd"
    "--pidfile=\${LOCALSTATEDIR}/i2pd/i2pd.pid"
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

# OpenBSD port template: one service authority, @sample trees that actually
# resolve, a private state directory and the mandatory notices.
require_text("${openbsd_port_make_text}" "-DI2PD_SERVICE_MANAGER=none" "OpenBSD port")
require_text("${openbsd_port_make_text}" "-DI2PD_INSTALL_DOCS=OFF" "OpenBSD port")
require_text("${openbsd_port_make_text}" "share/examples/etc/i2pd" "OpenBSD port samples")
require_text("${openbsd_port_make_text}" "share/examples/var/i2pd" "OpenBSD port state sample")
require_text("${openbsd_port_make_text}" "share/doc/i2pd/NOTICE" "OpenBSD port notice")
foreach(required IN ITEMS "@sample \${LOCALSTATEDIR}/i2pd/" "@mode 0700"
    "share/examples/etc/i2pd/i2pd.conf" "share/examples/var/i2pd"
    "share/doc/i2pd/NOTICE" "share/doc/i2pd/LICENSE.i2pd-tools")
  require_text("${openbsd_plist_text}" "${required}" "OpenBSD PLIST")
endforeach()
foreach(forbidden IN ITEMS "@sample \${LOCALSTATEDIR}/i2pd/certificates/"
    "share/examples/i2pd/")
  forbid_text("${openbsd_plist_text}" "${forbidden}" "OpenBSD PLIST")
endforeach()
forbid_text("${openbsd_plist_text}"
  "@mode 0640\n@owner _i2pd\n@group _i2pd\nshare/examples/etc/i2pd/i2pd.conf"
  "OpenBSD PLIST")
require_text("${openbsd_plist_text}" "@newuser _i2pd:838:838:i2pd:0:0:i2pd"
  "OpenBSD PLIST login class")
require_text("${openbsd_plist_text}" "share/examples/var/i2pd/subscriptions.txt"
  "OpenBSD PLIST subscriptions sample")
forbid_text("${openbsd_descr_text}" "native tools" "OpenBSD DESCR")

# Shepherd: Guile constructors, a restricted account and directory, an explicit
# process-group note, a 660-second grace period and no shell readiness loop.
foreach(required IN ITEMS
    "(use-modules (shepherd service))"
    "(service"
    "'(i2pd)"
    "make-forkexec-constructor"
    "make-kill-destructor SIGINT"
    "#:grace-period 660"
    "#:user \"i2pd\""
    "#:group \"i2pd\""
    "#:directory \"/var/lib/i2pd\""
    "#:environment-variables"
    "HOME=/var/lib/i2pd"
    "PATH=/usr/bin:/bin"
    "LC_ALL=C"
    "#:respawn? #t"
    "#:respawn-delay 5"
    "#:requirement '()"
    "#:documentation"
    "register-services"
    "process group"
    "--foreground"
    "--service"
    "--pidfile=/var/lib/i2pd/i2pd.pid"
    "--log=syslog")
  require_text("${shepherd_text}" "${required}" "shepherd script")
endforeach()
foreach(forbidden IN ITEMS "while " "until " "sleep ")
  forbid_text("${shepherd_text}" "${forbidden}" "shepherd script")
endforeach()

# Cross-manager parity: every Linux definition that can own /run uses the same
# pidfile and the foreground flag, and no definition claims readiness.
foreach(manager IN ITEMS systemd sysvinit openrc runit s6 s6-rc)
  if(manager STREQUAL "systemd")
    set(manager_text "${systemd_text}")
  elseif(manager STREQUAL "sysvinit")
    set(manager_text "${sysvinit_text}")
  elseif(manager STREQUAL "openrc")
    set(manager_text "${openrc_text}")
  elseif(manager STREQUAL "runit")
    set(manager_text "${runit_run_text}")
  elseif(manager STREQUAL "s6")
    set(manager_text "${s6_run_text}")
  else()
    set(manager_text "${s6rc_run_text}")
  endif()
  require_any("${manager_text}" "${manager} pidfile"
    "/run/i2pd/i2pd.pid" "I2PD_RUNDIR:=/run/i2pd")
  require_text("${manager_text}" "--foreground" "${manager} foreground")
endforeach()

# Documentation: the manager matrix, the exit-code contract and the per-manager
# caveats are part of the service contract, not optional prose.
foreach(required IN ITEMS
    "systemd" "GNU Shepherd" "OpenRC" "runit" "s6 and s6-rc" "SysVinit"
    "OpenBSD" "dinit" "Upstart"
    "KillMode=mixed" "rc_stop_signal=INT" "SIGTERM" "SIGINT"
    "660" "600 s" "no readiness" "open-i2pd" "<datadir>/i2pd.pid"
    "There is no readiness protocol" "runit has no per-service stop timeout"
    "process group" "exit code" "SIGHUP" "SIGUSR1" "SIGABRT")
  require_text("${service_doc_text}" "${required}" "service-management documentation")
endforeach()

message(STATUS "services-static: OK")
