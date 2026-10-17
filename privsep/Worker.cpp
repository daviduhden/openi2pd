/*
* Copyright (c) 2026, The PurpleI2P Project
*
* This file is part of Open I2Pd and licensed under BSD3
*/

#include "Worker.h"
#include "LogWorker.h"

#ifndef _WIN32

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <cstdio>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#if defined(__linux__)
#include <sys/syscall.h>
#endif

namespace i2p::privsep
{
	namespace
	{
		void CloseFrom (int lowfd)
		{
#if defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__APPLE__)
			closefrom (lowfd);
#else
			// Linux: close_range is preferred; fall back to /proc/self/fd.
#if defined(__linux__) && defined(SYS_close_range)
			if (syscall (SYS_close_range, static_cast<unsigned int> (lowfd), ~0U, 0) == 0) return;
#endif
			if (lowfd < 0) lowfd = 0;
			for (int fd = lowfd; fd < 1024; ++fd)
			{
				if (fd == 3) continue;
				close (fd);
			}
#endif
		}
	}

	std::string ExecutablePath ()
	{
#if defined(__linux__)
		char path[4096];
		auto length = readlink ("/proc/self/exe", path, sizeof (path) - 1);
		if (length > 0)
		{
			path[length] = 0;
			return path;
		}
#elif defined(__OpenBSD__)
		const char *path = getexecname ();
		if (path && *path)
		{
			char resolved[4096];
			if (realpath (path, resolved)) return resolved;
			return path;
		}
#elif defined(__APPLE__)
		// Not a supported platform for this fork.
#endif
		const char *argv0 = getenv ("I2PD_EXECUTABLE");
		if (argv0 && *argv0) return argv0;
		return "/proc/self/exe";
	}

	ChildProcess::~ChildProcess ()
	{
		Stop ();
	}

	ChildProcess::ChildProcess (ChildProcess&& other) noexcept:
		m_Pid (other.m_Pid), m_PidFd (other.m_PidFd), m_Channel (std::move (other.m_Channel))
	{
		other.m_Pid = -1;
		other.m_PidFd = -1;
	}

	ChildProcess& ChildProcess::operator= (ChildProcess&& other) noexcept
	{
		if (this != &other)
		{
			Stop ();
			m_Pid = other.m_Pid;
			m_PidFd = other.m_PidFd;
			m_Channel = std::move (other.m_Channel);
			other.m_Pid = -1;
			other.m_PidFd = -1;
		}
		return *this;
	}

	bool ChildProcess::IsRunning ()
	{
		if (m_Pid <= 0) return false;
#if defined(__linux__) && defined(SYS_pidfd_open)
		if (m_PidFd < 0) m_PidFd = static_cast<int> (syscall (SYS_pidfd_open, m_Pid, 0));
		if (m_PidFd >= 0)
		{
			pollfd pfd{m_PidFd, POLLIN, 0};
			int result;
			do result = poll (&pfd, 1, 0); while (result < 0 && errno == EINTR);
			return result == 0;
		}
#endif
		int status = 0;
		auto result = waitpid (m_Pid, &status, WNOHANG);
		if (result == 0) return true;
		if (result == m_Pid) m_Pid = -1;
		return false;
	}

	void ChildProcess::Stop (int timeoutMs)
	{
		if (m_Pid > 0)
		{
			if (m_Channel.IsOpen ())
			{
				Message shutdown{MessageType::Shutdown, 0, {}};
				(void) m_Channel.Send (shutdown, 500);
				m_Channel.Close ();
			}
			const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (timeoutMs);
			while (std::chrono::steady_clock::now () < deadline)
			{
				int status = 0;
				auto result = waitpid (m_Pid, &status, WNOHANG);
				if (result == m_Pid || (result < 0 && errno == ECHILD))
				{
					m_Pid = -1;
					break;
				}
				std::this_thread::sleep_for (std::chrono::milliseconds (20));
			}
			if (m_Pid > 0)
			{
				kill (m_Pid, SIGTERM);
				waitpid (m_Pid, nullptr, 0);
				m_Pid = -1;
			}
		}
		if (m_PidFd >= 0)
		{
			close (m_PidFd);
			m_PidFd = -1;
		}
	}

	std::expected<ChildProcess, std::string> SpawnLogWorker ()
	{
		const std::string executable = ExecutablePath ();
		auto pair = Channel::CreatePair ();
		if (!pair) return std::unexpected (pair.error ());
		auto& parent = pair->first;
		auto& child = pair->second;
		pid_t pid = fork ();
		if (pid < 0) return std::unexpected (std::string ("fork: ") + strerror (errno));
		if (pid == 0)
		{
			// Child: no threads exist yet (the supervisor starts before logger
			// and transports), use only async-signal-safe operations until exec.
			int fd = child.Fd ();
			if (fd != 3)
			{
				if (dup2 (fd, 3) < 0) _exit (127);
				close (fd);
			}
			int flags = fcntl (3, F_GETFD);
			if (flags >= 0) fcntl (3, F_SETFD, flags & ~FD_CLOEXEC);
			CloseFrom (4);
			char fdText[16];
			snprintf (fdText, sizeof (fdText), "%d", 3);
			setenv (LOG_WORKER_ENV_FD, fdText, 1);
			char *const argv[] = {const_cast<char *> ("i2pd"), const_cast<char *> ("--privsep-worker"), const_cast<char *> ("log"), nullptr};
			execv (executable.c_str (), argv);
			_exit (127);
		}
		// Parent
		child.Close ();
		ChildProcess process;
		process.m_Pid = pid;
		process.m_Channel = std::move (parent);
		return process;
	}
}

#endif
