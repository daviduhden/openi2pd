/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Daemon.
 *
 * Declares the platform-neutral Daemon_Singleton lifecycle (init, start, run
 * and stop), the per-platform daemon singletons and the optional
 * privilege-separated log supervisor. The private implementation owns the
 * TUI, I2PControl, UPnP and NTP helpers.
 *
 */

#ifndef DAEMON_H__
#define DAEMON_H__

#include <memory>
#include <string>
#include <string_view>
#include <ostream>
#include <sstream>

namespace i2p
{
namespace util
{
	class Daemon_Singleton_Private;
	class Daemon_Singleton
	{
	public:
		virtual bool init (int argc, char * argv[], std::shared_ptr<std::ostream> logstream);
		virtual bool init (int argc, char * argv[]);
		virtual bool start ();
		virtual bool stop ();
		virtual void run () { };

		virtual int GetGracefulShutdownInterval () const { return 0; };
		void setDataDir (std::string_view path);

	public:
		bool isDaemon;
		bool running;

	protected:
		Daemon_Singleton ();
		virtual ~Daemon_Singleton ();

		bool IsService () const;

		/**
		 * Spawn privilege-separated workers before the parent runtime sandbox
		 * is installed. On OpenBSD fork/exec must happen while the startup
		 * pledge still contains proc/exec; the runtime policy then drops both.
		 */
		bool PreparePrivsepWorkers ();
		/** Stop prepared workers during a startup rollback. */
		void StopPrivsepWorkers ();

		// d-pointer for the optional IPC service and UPnP/NTP helpers
		class Daemon_Singleton_Private;
		std::unique_ptr<Daemon_Singleton_Private> d;

	private:
#if !defined(_WIN32) && !defined(I2PD_NO_PRIVSEP)
		// Start the separate supervisor that owns the log worker, and replace
		// it with bounded backoff if the supervisor process dies.
		bool StartPrivsepLogWorker ();
		void StopPrivsepLogWorker ();
		void RunPrivsepLogSupervisor ();
#endif

		std::string DaemonDataDir;
	};

	void PrintMainWindowText (std::stringstream & s); // for GUI

#if defined(QT_GUI_LIB) // check if QT
#define Daemon i2p::util::DaemonQT::Instance ()
	// dummy, invoked from RunQT
	class DaemonQT : public i2p::util::Daemon_Singleton
	{
	public:
		static DaemonQT & Instance ()
		{
			static DaemonQT instance;
			return instance;
		}
	};

#elif defined(_WIN32)
#define Daemon i2p::util::DaemonWin32::Instance ()
	class DaemonWin32 : public Daemon_Singleton
	{
	public:
		static DaemonWin32 & Instance ()
		{
			static DaemonWin32 instance;
			return instance;
		}

		bool init (int argc, char * argv[]);
		bool start ();
		bool stop ();
		void run ();

		int GetGracefulShutdownInterval () const;

	public:
		bool isGraceful;

	private:
		DaemonWin32 ()
			: isGraceful (false)
		{
		}
	};
#elif (defined(ANDROID) && !defined(ANDROID_BINARY))
#define Daemon i2p::util::DaemonAndroid::Instance ()
	// dummy, invoked from android/jni/DaemonAndroid.*
	class DaemonAndroid : public i2p::util::Daemon_Singleton
	{
	public:
		static DaemonAndroid & Instance ()
		{
			static DaemonAndroid instance;
			return instance;
		}
	};
#else // Unix-like systems, including Linux
	class DaemonUnix : public Daemon_Singleton
	{
	public:
		static DaemonUnix & Instance ()
		{
			static DaemonUnix instance;
			return instance;
		}

		bool start ();
		bool stop ();
		void run ();

		int GetGracefulShutdownInterval () const { return gracefulShutdownInterval; };

	private:
		std::string pidfile;
		int pidFH = -1;

	public:
		int gracefulShutdownInterval = 0; // in seconds
	};
#define Daemon i2p::util::DaemonUnix::Instance ()

#endif
}
}

#endif // DAEMON_H__
