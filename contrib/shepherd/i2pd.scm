;; See the LICENSE file at the top of the project tree for copyright
;; and license details.
;;
;; openi2pd: i2pd.scm.
;;
;; Native GNU Shepherd service definition for the openi2pd router. Shepherd
;; starts the foreground process as the i2pd account, supervises it and
;; respawns it with a delay. The router logs through syslog so that Shepherd
;; never creates a root-owned log file before dropping privileges.
;;

(use-modules (shepherd service))

(define i2pd-service
  (service
   '(i2pd)
   #:start
   (make-forkexec-constructor
    (list "/usr/bin/i2pd"
          "--service"
          "--foreground"
          "--conf=/etc/i2pd/i2pd.conf"
          "--tunconf=/etc/i2pd/tunnels.conf"
          "--tunnelsdir=/etc/i2pd/tunnels.conf.d"
          "--datadir=/var/lib/i2pd"
          "--certsdir=/usr/share/i2pd/certificates"
          "--pidfile=/var/lib/i2pd/i2pd.pid"
          "--log=syslog")
    #:user "i2pd"
    #:group "i2pd"
    #:directory "/var/lib/i2pd"
    ;; LC_ALL=C fixes the process locale. The router pins LC_NUMERIC itself, and
    ;; the UI catalog comes from --lang or the `lang` option; when both are unset
    ;; the catalog falls back to this environment value, which means English.
    #:environment-variables
    (list "HOME=/var/lib/i2pd"
          "PATH=/usr/bin:/bin"
          "LC_ALL=C"))
   ;; make-kill-destructor sends SIGINT to the service *process group* and
   ;; escalates to SIGKILL after the grace period. The default five seconds is
   ;; far shorter than the router's ten-minute graceful drain, so allow the
   ;; documented 600-second drain plus the same 60-second shutdown margin used
   ;; by the systemd/OpenRC/SysV units (660 s). Shepherd owns the group signal;
   ;; the daemon still stops its own IPC workers during the ordered stop.
   ;; Socket/storage workers receive the group signal too, so this manager is
   ;; not the best choice when a strict main-process-only drain is required.
   ;; The pidfile is under the data directory because Shepherd has no pre-start
   ;; tmpfiles hook.
   #:stop (make-kill-destructor SIGINT #:grace-period 660)
   #:respawn? #t
   ;; Restart a crashed router after a five-second delay instead of immediately.
   ;; The delay is fixed, so a permanently failing configuration retries at that
   ;; pace and repeats the failure in the log pending operator intervention.
   #:respawn-delay 5
   ;; No dependency is declared. The native keyword is #:requirement;
   ;; #:requires from older examples is not accepted by current Shepherd.
   ;; Native Shepherd does not provide a portable "networking" service, and
   ;; naming one that is not registered prevents the service from starting. If
   ;; the site does define a real prerequisite, add it here after checking its
   ;; provision name, for example:
   ;;   #:requirement '(network)
   #:requirement '()
   #:documentation "openi2pd anonymity network router"))

(register-services (list i2pd-service))
