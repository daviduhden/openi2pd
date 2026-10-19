/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Supervisor.
 *
 * Implements the privilege-separated supervisor: verifies the control peer's
 * UID, applies its platform sandbox and starts or restarts a log worker while
 * forwarding decoded records. It proxies log configuration without opening
 * the file, answers pings and exits on shutdown or malformed frames.
 *
 */

#include "Supervisor.h"

#ifndef _WIN32

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>

#include "Channel.h"
#include "LinuxSandbox.h"
#include "LogWorker.h"
#include "OpenBSDSandbox.h"
#include "Protocol.h"
#include "Worker.h"

namespace i2p::privsep
{
namespace
{
	bool ReadLogConfig (Channel & channel, bool & useSyslog, std::string & path, std::string & tag)
	{
		while (true)
		{
			auto received = channel.ReceiveWithFd (1000);
			if (!received)
			{
				std::cerr << "privsep supervisor: control channel: " << received.error () << std::endl;
				return false;
			}
			if (!received->has_value ()) continue;
			auto message = std::move ((*received)->message);
			if ((*received)->fd >= 0) close ((*received)->fd);
			if (message.type == MessageType::Ping)
			{
				(void)channel.Send (Message { MessageType::Pong, message.requestId, { } }, 500);
				continue;
			}
			if (message.type != MessageType::ConfigureLog && message.type != MessageType::ConfigureLogFd)
			{
				std::cerr << "privsep supervisor: expected ConfigureLog" << std::endl;
				return false;
			}
			if ((*received)->fd >= 0)
			{
				std::cerr << "privsep supervisor: unexpected log descriptor; "
						  << "the log worker owns the file capability" << std::endl;
				return false;
			}
			auto decoded = DecodeLogConfig (message.payload);
			if (!decoded)
			{
				std::cerr << "privsep supervisor: invalid log configuration" << std::endl;
				return false;
			}
			useSyslog = std::get<0> (*decoded);
			path = std::move (std::get<1> (*decoded));
			tag = std::move (std::get<2> (*decoded));
			return true;
		}
	}
}

int RunSupervisor (int fd)
{
	Channel control = Channel::Adopt (fd);
	auto peer = control.PeerUid ();
	if (!peer || *peer != geteuid ())
	{
		std::cerr << "privsep supervisor: rejecting peer with unexpected uid" << std::endl;
		return 1;
	}

	bool useSyslog = false;
	std::string path, tag;
	if (!ReadLogConfig (control, useSyslog, path, tag)) return 1;

	std::string workerExecutable;
#if defined(__OpenBSD__)
	// Resolve the executable before installing the supervisor sandbox. The
	// supervisor only needs proc/exec for bounded log-worker replacement; it
	// never reads or writes a log path itself and has no external network.
	workerExecutable = ExecutablePath ();
	if (workerExecutable.empty ())
	{
		std::cerr << "privsep supervisor: cannot resolve executable before sandbox" << std::endl;
		return 1;
	}
	OpenBSDPolicyConfig policyConfig;
	// Do not lock unveil here: the supervisor must exec the pre-resolved log
	// worker after applying this policy. pledge("stdio unix proc exec") already
	// denies pathname reads/writes; the child installs its own unveil policy
	// after execve.
	policyConfig.useUnveil = false;
	auto policy = BuildOpenBSDPolicy (OpenBSDWorkerRole::Supervisor, policyConfig);
	if (!policy)
	{
		std::cerr << "privsep supervisor: invalid OpenBSD policy: " << policy.error () << std::endl;
		return 1;
	}
	auto applied = ApplyOpenBSDPolicy (*policy);
	if (!applied)
	{
		std::cerr << "privsep supervisor: OpenBSD sandbox: " << applied.error () << std::endl;
		return 1;
	}
#endif

#if defined(__linux__)
	// The supervisor may fork/exec a replacement log worker later; the filter
	// therefore permits process creation and AF_UNIX socketpairs. It denies
	// AF_INET/AF_INET6, drops capabilities and (where available) restricts
	// pathname writes without blocking exec of the child.
	auto sandboxProfile = GetLinuxSandboxProfile (WorkerClass::Supervisor);
	sandboxProfile.requireLandlock = false;
	auto sandbox = ApplyLinuxSandbox (sandboxProfile);
	if (!sandbox)
	{
		std::cerr << "privsep supervisor: Linux sandbox: " << sandbox.error () << std::endl;
		return 1;
	}
#endif

	std::unique_ptr<ChildProcess> worker;
	std::unique_ptr<LogSink> sink;
	auto startWorker = [&] () -> bool
	{
		auto spawned = SpawnLogWorker (workerExecutable);
		if (!spawned)
		{
			std::cerr << "privsep supervisor: cannot spawn log worker: " << spawned.error () << std::endl;
			return false;
		}
		worker = std::make_unique<ChildProcess> (std::move (*spawned));
		auto channel = std::move (worker->ChannelFd ());
		if (!sink)
			sink = std::make_unique<LogSink> (std::move (channel));
		else if (!sink->ReattachProxy (std::move (channel)))
			return false;
		if (!sink->ConfigureProxy (useSyslog, path, tag))
		{
			// ConfigureProxy is only valid once; on restart ReattachProxy
			// already sent the same configuration.
			if (!sink->IsOpen ()) return false;
		}
		return sink->IsOpen ();
	};

	if (!startWorker ()) return 1;

	while (true)
	{
		if (!worker || !worker->IsRunning () || !sink || !sink->IsOpen ())
		{
			if (worker) worker->Stop (500);
			sink.reset ();
			std::this_thread::sleep_for (std::chrono::milliseconds (250));
			if (!startWorker ()) return 1;
			continue;
		}

		auto received = control.Receive (500);
		if (!received)
		{
			std::cerr << "privsep supervisor: control channel closed" << std::endl;
			break;
		}
		if (!received->has_value ()) continue;
		const auto & message = **received;
		if (message.type == MessageType::Shutdown) break;
		if (message.type == MessageType::Ping)
		{
			(void)control.Send (Message { MessageType::Pong, message.requestId, { } }, 500);
			continue;
		}
		if (message.type != MessageType::LogRecord)
		{
			std::cerr << "privsep supervisor: unsupported message type" << std::endl;
			break;
		}
		auto record = DecodeLogRecord (message.payload);
		if (!record)
		{
			std::cerr << "privsep supervisor: malformed log record" << std::endl;
			break;
		}
		sink->Write (std::get<0> (*record), static_cast<std::time_t> (std::get<1> (*record)), std::get<2> (*record));
		if (!sink->IsOpen ())
		{
			// The next loop starts a replacement and reattaches the sink.
			continue;
		}
	}

	if (sink) (void)sink->Shutdown ();
	if (worker) worker->Stop (500);
	return 0;
}
}

#endif
