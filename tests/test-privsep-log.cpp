#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "privsep/Channel.h"
#include "privsep/LogWorker.h"
#include "privsep/Protocol.h"

using namespace i2p::privsep;

int main ()
{
	char dirTemplate[] = "/tmp/i2pd-privsep-log-XXXXXX";
	char *dir = mkdtemp (dirTemplate);
	assert (dir);
	const std::string path = std::string (dir) + "/router.log";

	auto pair = Channel::CreatePair ();
	assert (pair.has_value ());
	pid_t pid = fork ();
	assert (pid >= 0);
	if (pid == 0)
	{
		pair->first.Close ();
		_exit (RunLogWorker (pair->second.Fd ()));
	}
	pair->second.Close ();

	Message configure{MessageType::ConfigureLog, 1, EncodeLogConfig (false, path, "test")};
	assert (pair->first.Send (configure).has_value ());
	Message record{MessageType::LogRecord, 2, EncodeLogRecord (4, 12345, "hello from worker")};
	assert (pair->first.Send (record).has_value ());
	Message shutdown{MessageType::Shutdown, 3, {}};
	assert (pair->first.Send (shutdown).has_value ());
	pair->first.Close ();

	int status = 0;
	assert (waitpid (pid, &status, 0) == pid);
	assert (WIFEXITED (status));
	assert (WEXITSTATUS (status) == 0);

	std::ifstream file (path);
	std::string line;
	std::getline (file, line);
	assert (line.find ("hello from worker") != std::string::npos);
	file.close ();
	unlink (path.c_str ());
	rmdir (dir);
	return 0;
}
