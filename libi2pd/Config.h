/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Config.
 *
 * Declares the Boost.Program_options-based configuration layer for
 * command-line and configuration-file options. Callers initialize the
 * option set, parse inputs, finalize defaults, then read or update
 * typed values by name.
 *
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>

/**
 * Functions to parse and store i2pd parameters
 *
 * General usage flow:
 *   Init() -- early as possible
 *   ParseCmdline() -- somewhere close to main()
 *   ParseConfig()  -- after detecting path to config
 *   Finalize()     -- right after all Parse*() functions called
 *   GetOption()    -- may be called after Finalize()
 */

namespace i2p
{
namespace config
{
	extern boost::program_options::variables_map m_Options;

	/**
	 * @brief Initialize list of acceptable parameters
	 *
	 * Should be called before any Parse* functions.
	 */
	void Init ();

	/// Informational command-line request answered without starting a
	/// router, proxy or other service.
	enum class CmdlineOutcome
	{
		/// No informational flag: the caller continues with startup.
		Proceed,
		/// ``version`` was given: print the version banner.
		Version,
		/// ``help`` or ``h`` was given: print the banner and the option
		/// table.
		Help
	};

	/**
	 * @brief Answer a command-line request for version or help text.
	 * @param outcome request to render, never @c CmdlineOutcome::Proceed
	 * @return 0 after the response was published, non-zero when @a outcome
	 *         is @c CmdlineOutcome::Proceed (a caller error)
	 *
	 * The text is rendered from the same option table that ParseCmdline()
	 * parses, so the two entry points cannot drift apart, and it is
	 * published with one synchronous write. On Windows the router is a
	 * GUI-subsystem executable: an inherited standard-output handle (a
	 * pipe from a test driver or a shell redirection) is used as is, and a
	 * start without one borrows the parent console through
	 * AttachConsole(ATTACH_PARENT_PROCESS) for this banner only. No
	 * console window is allocated and no dialog is opened; when neither
	 * exists the status is the only signal, so a headless caller must not
	 * rely on reading the text.
	 *
	 * This function does not change configuration state and never starts
	 * a service, so it is safe to call before any daemon initialization.
	 */
	int PublishInformationalResponse (CmdlineOutcome outcome);

	/**
	 * @brief Detect a version or help request without side effects.
	 * @param argc Cmdline arguments count, should be passed from main().
	 * @param argv Cmdline parameters array, should be passed from main()
	 * @return the requested informational response, or
	 *         @c CmdlineOutcome::Proceed
	 *
	 * The probe shares the option table and the parser style with
	 * ParseCmdline(), but it stores into a local option map and reports
	 * nothing. A malformed, unknown or otherwise rejected command line
	 * therefore returns Proceed and the normal parse produces the
	 * platform-appropriate diagnostic (on Windows the GUI build keeps its
	 * fatal-error dialog). An entry point uses this to answer
	 * ``--version``/``--help`` before it installs any console, locale,
	 * GUI or service state.
	 */
	CmdlineOutcome ClassifyInformationalRequest (int argc, char * argv[]);

	/**
	 * @brief Parse cmdline parameters, and show help if requested
	 * @param argc Cmdline arguments count, should be passed from main().
	 * @param argv Cmdline parameters array, should be passed from main()
	 * @param ignoreUnknown when true, unregistered options are accepted
	 * and kept in the option map instead of failing the parse; used by
	 * front-ends that forward arguments to another component
	 *
	 * If --help is given in parameters, shows its list with description
	 * and terminates the program with exitcode 0.
	 *
	 * An informational request is answered through
	 * PublishInformationalResponse() and terminated with std::_Exit():
	 * nothing has been acquired at that point, so exit-time teardown of
	 * router globals would only be able to wait on state this process
	 * never started.
	 *
	 * In case of parameter misuse boost throws an exception.
	 * We internally handle type boost::program_options::unknown_option,
	 * and then terminate the program with exitcode 1.
	 *
	 * Other exceptions will be passed to higher level.
	 */
	void ParseCmdline (int argc, char * argv[], bool ignoreUnknown = false);

	/**
	 * @brief Load and parse given config file
	 * @param path Path to config file
	 *
	 * If error occurred when opening file path is points to,
	 * we show the error message and terminate program.
	 *
	 * In case of parameter misuse boost throws an exception.
	 * We internally handle type boost::program_options::unknown_option,
	 * and then terminate program with exitcode 1.
	 *
	 * Other exceptions will be passed to higher level.
	 */
	void ParseConfig (const std::string & path);

	/**
	 * @brief Used to combine options from cmdline, config and default
	 *        values
	 */
	void Finalize ();

	/**
	 * @brief Accessor to parameters by name
	 * @param name Name of the requested parameter
	 * @param value Variable where to store option
	 * @return this function returns false if parameter not found
	 *
	 * Example: uint16_t port; GetOption("sam.port", port);
	 */
	template <typename T> bool GetOption (const char * name, T & value)
	{
		if (!m_Options.count (name))
		{
			value = T ();
			return false;
		}
		value = m_Options[name].as<T> ();
		return true;
	}

	template <typename T> bool GetOption (const std::string & name, T & value)
	{
		return GetOption (name.c_str (), value);
	}

	bool GetOptionAsAny (const char * name, boost::any & value);
	bool GetOptionAsAny (const std::string & name, boost::any & value);

	/**
	 * @brief Set value of given parameter
	 * @param name Name of settable parameter
	 * @param value New parameter value
	 * @return true if value set up successful, false otherwise
	 *
	 * Example: uint16_t port = 2827; SetOption("bob.port", port);
	 */
	template <typename T> bool SetOption (const char * name, const T & value)
	{
		if (!m_Options.count (name)) return false;
		m_Options.at (name).value () = value;
		notify (m_Options);
		return true;
	}

	/**
	 * @brief Check is value explicitly given or default
	 * @param name Name of checked parameter
	 * @return true if value set to default, false otherwise
	 */
	bool IsDefault (const char * name);
}
}

#endif // CONFIG_H
