/*
* Copyright (c) 2026, The PurpleI2P Project
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
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
		// Linux: close_range is preferred; fall back to a bounded close
		// loop. BSD closefrom(3) covers the whole descriptor table and is
		// usable in the post-fork child.
#if defined(__linux__) && defined(SYS_close_range)
		if (syscall (SYS_close_range, static_cast<unsigned int> (lowfd), ~0U, 0) == 0) return;
#endif
		if (lowfd < 0) lowfd = 0;
		// Callers pass a floor above the preserved worker descriptor, so
		// no descriptor is special-cased here.
		for (int fd = lowfd; fd < 1024; ++fd) close (fd);
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
	const char * path = getexecname ();
	if (path && *path)
	{
		char resolved[4096];
		if (realpath (path, resolved)) return resolved;
		return path;
	}
#elif defined(__APPLE__)
	// Not a supported platform for this fork.
#endif
	const char * argv0 = getenv ("I2PD_EXECUTABLE");
	if (argv0 && *argv0) return argv0;
	return "/proc/self/exe";
}

ChildProcess::~ChildProcess ()
{
	Stop ();
}

ChildProcess::ChildProcess (ChildProcess && other) noexcept
	: m_Pid (other.m_Pid)
	, m_PidFd (other.m_PidFd)
	, m_Channel (std::move (other.m_Channel))
{
	other.m_Pid = -1;
	other.m_PidFd = -1;
}

ChildProcess & ChildProcess::operator= (ChildProcess && other) noexcept
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

void ChildProcess::ClosePidFd ()
{
	if (m_PidFd >= 0)
	{
		close (m_PidFd);
		m_PidFd = -1;
	}
}

bool ChildProcess::ReapNoHang ()
{
	if (m_Pid <= 0) return true;
	int status = 0;
	pid_t result;
	do result = waitpid (m_Pid, &status, WNOHANG);
	while (result < 0 && errno == EINTR);
	if (result == m_Pid || (result < 0 && errno == ECHILD))
	{
		// Exactly one path reaps the child; a second waitpid can therefore
		// never observe a recycled PID.
		m_Pid = -1;
		ClosePidFd ();
		return true;
	}
	return false;
}

bool ChildProcess::IsRunning ()
{
	if (m_Pid <= 0) return false;
#if defined(__linux__) && defined(SYS_pidfd_open)
	if (m_PidFd < 0) m_PidFd = static_cast<int> (syscall (SYS_pidfd_open, m_Pid, 0));
	if (m_PidFd >= 0)
	{
		pollfd pfd { m_PidFd, POLLIN, 0 };
		int result;
		do result = poll (&pfd, 1, 0);
		while (result < 0 && errno == EINTR);
		if (result == 0) return true;
		if (result > 0 && (pfd.revents & POLLIN))
		{
			// The pidfd reports exit. Reap immediately so a restart never
			// leaves a zombie and never races waitpid from another path.
			ReapNoHang ();
			return m_Pid > 0;
		}
	}
#endif
	return !ReapNoHang ();
}

void ChildProcess::Stop (int timeoutMs)
{
	if (m_Channel.IsOpen ())
	{
		Message shutdown { MessageType::Shutdown, 0, { } };
		(void)m_Channel.Send (shutdown, 500);
		m_Channel.Close ();
	}
	if (m_Pid > 0)
	{
		auto waitFor = [this] (int ms)
		{
			const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (ms);
			while (true)
			{
				if (ReapNoHang ()) return true;
				if (std::chrono::steady_clock::now () >= deadline) return false;
				std::this_thread::sleep_for (std::chrono::milliseconds (20));
			}
		};
		if (!waitFor (timeoutMs > 0 ? timeoutMs : 2000))
		{
			kill (m_Pid, SIGTERM);
			if (!waitFor (500))
			{
				// SIGKILL cannot be ignored; a blocking wait here is the
				// only way to guarantee the PID is not reused while we
				// still hold it.
				kill (m_Pid, SIGKILL);
				int status = 0;
				pid_t result;
				do result = waitpid (m_Pid, &status, 0);
				while (result < 0 && errno == EINTR);
				if (result == m_Pid || (result < 0 && errno == ECHILD)) m_Pid = -1;
			}
		}
	}
	ClosePidFd ();
}

i2p::expected<ChildProcess, std::string> SpawnLogWorker ()
{
	const std::string executable = ExecutablePath ();
	auto pair = Channel::CreatePair ();
	if (!pair) return i2p::unexpected (pair.error ());
	auto & parent = pair->first;
	auto & child = pair->second;
	pid_t pid = fork ();
	if (pid < 0) return i2p::unexpected (std::string ("fork: ") + strerror (errno));
	if (pid == 0)
	{
		// Restarts fork from the multi-threaded router. The child may only
		// use async-signal-safe operations until execv: no allocation, no
		// setenv, no stdio and no C++ objects with locks.
		sigset_t empty;
		sigemptyset (&empty);
		sigprocmask (SIG_SETMASK, &empty, nullptr);
		int fd = child.Fd ();
		if (fd != 3)
		{
			if (dup2 (fd, 3) < 0) _exit (127);
			close (fd);
		}
		int flags = fcntl (3, F_GETFD);
		if (flags < 0 || fcntl (3, F_SETFD, flags & ~FD_CLOEXEC) < 0) _exit (127);
		CloseFrom (4);
		// Parent death is detected through EOF on the channel; a
		// PR_SET_PDEATHSIG would be tied to the forking thread and could
		// kill a restarted worker when that thread is joined.
		char fdText[] = "3";
		char * const argv[] = { const_cast<char *> ("i2pd"), const_cast<char *> ("--privsep-worker"),
			const_cast<char *> ("log"), fdText, nullptr };
		execv (executable.c_str (), argv);
		_exit (127);
	}
	// Parent
	child.Close ();
	ChildProcess process;
	process.m_Pid = pid;
	process.m_Channel = std::move (parent);
#if defined(__linux__) && defined(SYS_pidfd_open)
	// Open eagerly so a restart decision can never be confused by PID
	// reuse after the child has exited.
	auto pidFd = syscall (SYS_pidfd_open, pid, 0);
	if (pidFd >= 0) process.m_PidFd = static_cast<int> (pidFd);
#endif
	return process;
}
}

#endif
