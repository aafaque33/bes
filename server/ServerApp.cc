// ServerApp.cc

// This file is part of bes, A C++ back-end server implementation framework
// for the OPeNDAP Data Access Protocol.

// Copyright (c) 2004-2009 University Corporation for Atmospheric Research
// Author: Patrick West <pwest@ucar.edu> and Jose Garcia <jgarcia@ucar.edu>
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
//
// You can contact University Corporation for Atmospheric Research at
// 3080 Center Green Drive, Boulder, CO 80301

// (c) COPYRIGHT University Corporation for Atmospheric Research 2004-2005
// Please read the full copyright statement in the file COPYRIGHT_UCAR.
//
// Authors:
//      pwest       Patrick West <pwest@ucar.edu>
//      jgarcia     Jose Garcia <jgarcia@ucar.edu>

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h> // for wait
#include <sys/types.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <cerrno>

using std::cout;
using std::cerr;
using std::endl;
using std::ios;
using std::ostringstream;
using std::ofstream;

#include "config.h"

#include "ServerApp.h"
#include "ServerExitConditions.h"
#include "TheBESKeys.h"
#include "BESLog.h"
#include "SocketListener.h"
#include "TcpSocket.h"
#include "UnixSocket.h"
#include "BESServerHandler.h"
#include "BESError.h"
#include "PPTServer.h"
#include "BESMemoryManager.h"
#include "BESDebug.h"
#include "BESCatalogUtils.h"
#include "BESServerUtils.h"

#include "BESDefaultModule.h"
#include "BESXMLDefaultCommands.h"
#include "BESDaemonConstants.h"

static int session_id = 0;

// These are set to 1 by their respective handlers and then processed in the
// signal processing loop.
static volatile sig_atomic_t sigchild = 0;
static volatile sig_atomic_t sigpipe = 0;
static volatile sig_atomic_t sigterm = 0;
static volatile sig_atomic_t sighup = 0;

static string bes_exit_message(int cpid, int stat)
{
	ostringstream oss;
	oss << "beslistener child pid: " << cpid;
	if (WIFEXITED(stat)) { // exited via exit()?
		oss << " exited with status: " << WEXITSTATUS(stat);
	}
	else if (WIFSIGNALED(stat)) { // exited via a signal?
		oss << " exited with signal: " << WTERMSIG(stat);
#ifdef WCOREDUMP
		if (WCOREDUMP(stat))
			oss << " and a core dump!";
#endif
	}
	else {
		oss << " exited, but I have no clue as to why";
	}

	return oss.str();
}

// These two functions duplicate code in daemon.cc
static void block_signals()
{
    sigset_t set;
    sigemptyset (&set);
    sigaddset(&set, SIGCHLD);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGPIPE);

    if (sigprocmask(SIG_BLOCK, &set, 0) < 0) {
    	throw BESInternalError(string("sigprocmask error: ") + strerror(errno) + " while trying to block signals.", __FILE__, __LINE__);
    }
}

static void unblock_signals()
{
    sigset_t set;
    sigemptyset (&set);
    sigaddset(&set, SIGCHLD);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGPIPE);

    if (sigprocmask(SIG_UNBLOCK, &set, 0) < 0) {
    	throw BESInternalError(string("sigprocmask error: ") + strerror(errno) + " while trying to unblock signals.", __FILE__, __LINE__);
    }
}

// I moved the signal handlers here so that signal processing would be simpler
// and no library calls would be made to functions that are not 'asynch safe'.
// This was the fix for ticket 2025 and friends (the zombie process problem).
// jhrg 3/3/14

// This is needed so that the master bes listener will get the exit status of
// all of the child bes listeners (preventing them from becoming zombies).
static void CatchSigChild(int sig)
{
	if (sig == SIGCHLD) {
		sigchild = 1;
	}
}

// If the HUP signal is sent to the master beslistener, it should exit and
// return a value indicating to the besdaemon that it should be restarted.
// This also has the side-affect of re-reading the configuration file.
static void CatchSigHup(int sig)
{
	if (sig == SIGHUP) {
		sighup = 1;
	}
}

static void CatchSigPipe(int sig)
{
	if (sig == SIGPIPE) {
		sigpipe = 1;
	}
}

// This is the default signal sent by 'kill'; when the master beslistener gets
// this signal it should stop. besdaemon should not try to start a new
// master beslistener.
static void CatchSigTerm(int sig)
{
	if (sig == SIGTERM) {
		sigterm = 1;
	}
}

/** Register the signal handlers. This registers handlers for HUP, TERM and
 *  CHLD. For each, if this OS supports restarting 'slow' system calls, enable
 *  that. For the TERM and HUP handlers, block SIGCHLD for the duration of
 *  the handler (we call stop_all_beslisteners() in those handlers and that
 *  function uses wait() to collect the exit status of the child processes).
 *  This ensure that our signal handlers (TERM and HUP) don't themselves get
 *  interrupted.
 */
static void register_signal_handlers()
{
	struct sigaction act;
	sigemptyset(&act.sa_mask);
	sigaddset(&act.sa_mask, SIGCHLD);
	sigaddset(&act.sa_mask, SIGPIPE);
	sigaddset(&act.sa_mask, SIGTERM);
	sigaddset(&act.sa_mask, SIGHUP);
	act.sa_flags = 0;
#ifdef SA_RESTART
	BESDEBUG("beslistener", "beslistener: setting restart for sigchld." << endl);
	act.sa_flags |= SA_RESTART;
#endif

	BESDEBUG("beslistener", "beslistener: Registering signal handlers ... " << endl);

	act.sa_handler = CatchSigChild;
	if (sigaction(SIGCHLD, &act, 0))
		throw BESInternalFatalError("Could not register a handler to catch beslistener child process status.", __FILE__,
				__LINE__);

	act.sa_handler = CatchSigPipe;
	if (sigaction(SIGPIPE, &act, 0) < 0)
		throw BESInternalFatalError("Could not register a handler to catch beslistener pipe signal.", __FILE__,
				__LINE__);

	act.sa_handler = CatchSigTerm;
	if (sigaction(SIGTERM, &act, 0) < 0)
		throw BESInternalFatalError("Could not register a handler to catch beslistener terminate signal.", __FILE__,
				__LINE__);

	act.sa_handler = CatchSigHup;
	if (sigaction(SIGHUP, &act, 0) < 0)
		throw BESInternalFatalError("Could not register a handler to catch beslistener hup signal.", __FILE__,
				__LINE__);

	BESDEBUG("beslistener", "beslistener: OK" << endl);
}

ServerApp::ServerApp() :
		BESModuleApp(), _portVal(0), _gotPort(false), _unixSocket(""), _secure(false), _mypid(0), _ts(0), _us(0), _ps(0)
{
	_mypid = getpid();
}

ServerApp::~ServerApp()
{
	delete TheBESKeys::TheKeys();

	BESCatalogUtils::delete_all_catalogs();
}

int ServerApp::initialize(int argc, char **argv)
{
	int c = 0;
	bool needhelp = false;
	string dashi;
	string dashc;
	string dashd = "";

	// If you change the getopt statement below, be sure to make the
	// corresponding change in daemon.cc and besctl.in
	while ((c = getopt(argc, argv, "hvsd:c:p:u:i:r:")) != -1) {
		switch (c) {
		case 'i':
			dashi = optarg;
			break;
		case 'c':
			dashc = optarg;
			break;
		case 'r':
			break; // we can ignore the /var/run directory option here
		case 'p':
			_portVal = atoi(optarg);
			_gotPort = true;
			break;
		case 'u':
			_unixSocket = optarg;
			break;
		case 'd':
			dashd = optarg;
			// BESDebug::SetUp(optarg);
			break;
		case 'v':
			BESServerUtils::show_version(BESApp::TheApplication()->appName());
			break;
		case 's':
			_secure = true;
			break;
		case 'h':
		case '?':
		default:
			needhelp = true;
			break;
		}
	}

	// before we can do any processing, log any messages, initialize any
	// modules, do anything, we need to determine where the BES
	// configuration file lives. From here we get the name of the log
	// file, group and user id, and information that the modules will
	// need to run properly.

	// If the -c option was passed, set the config file name in TheBESKeys
	if (!dashc.empty()) {
		TheBESKeys::ConfigFile = dashc;
	}

	// If the -c option was not passed, but the -i option
	// was passed, then use the -i option to construct
	// the path to the config file
	if (dashc.empty() && !dashi.empty()) {
		if (dashi[dashi.length() - 1] != '/') {
			dashi += '/';
		}
		string conf_file = dashi + "etc/bes/bes.conf";
		TheBESKeys::ConfigFile = conf_file;
	}

	if (!dashd.empty()) BESDebug::SetUp(dashd);

	// register the two debug context for the server and ppt. The
	// Default Module will register the bes context.
	BESDebug::Register("server");
	BESDebug::Register("ppt");

	// Because we are now running as the user specified in the
	// configuration file, we won't be able to listen on system ports.
	// If this is a problem, we may need to move this code above setting
	// the user and group ids.
	bool found = false;
	string port_key = "BES.ServerPort";
	if (!_gotPort) {
		string sPort;
		try {
			TheBESKeys::TheKeys()->get_value(port_key, sPort, found);
		}
		catch (BESError &e) {
			BESDEBUG("beslistener", "beslistener: FAILED" << endl);
			string err = (string) "FAILED: " + e.get_message();
			cerr << err << endl;
			(*BESLog::TheLog()) << err << endl;
			exit(SERVER_EXIT_FATAL_CANNOT_START);
		}
		if (found) {
			_portVal = atoi(sPort.c_str());
			if (_portVal != 0) {
				_gotPort = true;
			}
		}
	}

	found = false;
	string socket_key = "BES.ServerUnixSocket";
	if (_unixSocket == "") {
		try {
			TheBESKeys::TheKeys()->get_value(socket_key, _unixSocket, found);
		}
		catch (BESError &e) {
			BESDEBUG("server", "beslistener: FAILED" << endl);
			string err = (string) "FAILED: " + e.get_message();
			cerr << err << endl;
			(*BESLog::TheLog()) << err << endl;
			exit(SERVER_EXIT_FATAL_CANNOT_START);
		}
	}

	if (!_gotPort && _unixSocket == "") {
		string msg = "Must specify a tcp port or a unix socket or both\n";
		msg += "Please specify on the command line with -p <port>";
		msg += " and/or -u <unix_socket>\n";
		msg += "Or specify in the bes configuration file with " + port_key + " and/or " + socket_key + "\n";
		cout << endl << msg;
		(*BESLog::TheLog()) << msg << endl;
		BESServerUtils::show_usage(BESApp::TheApplication()->appName());
	}

	found = false;
	if (_secure == false) {
		string key = "BES.ServerSecure";
		string isSecure;
		try {
			TheBESKeys::TheKeys()->get_value(key, isSecure, found);
		}
		catch (BESError &e) {
			BESDEBUG("server", "beslistener: FAILED" << endl);
			string err = (string) "FAILED: " + e.get_message();
			cerr << err << endl;
			(*BESLog::TheLog()) << err << endl;
			exit(SERVER_EXIT_FATAL_CANNOT_START);
		}
		if (isSecure == "Yes" || isSecure == "YES" || isSecure == "yes") {
			_secure = true;
		}
	}

	BESDEBUG("beslistener", "beslistener: initializing default module ... " << endl);
	BESDefaultModule::initialize(argc, argv);
	BESDEBUG("beslistener", "beslistener: done initializing default module" << endl);

	BESDEBUG("beslistener", "beslistener: initializing default commands ... " << endl);
	BESXMLDefaultCommands::initialize(argc, argv);
	BESDEBUG("beslistener", "beslistener: done initializing default commands" << endl);

	// This will load and initialize all of the modules
	BESDEBUG("beslistener", "beslistener: initializing loaded modules ... " << endl);
	int ret = BESModuleApp::initialize(argc, argv);
	BESDEBUG("beslistener", "beslistener: done initializing loaded modules" << endl);

	BESDEBUG("beslistener", "beslistener: initialized settings:" << *this);

	if (needhelp) {
		BESServerUtils::show_usage(BESApp::TheApplication()->appName());
	}

	// This sets the process group to be ID of this process. All children
	// will get this GID. Then use killpg() to send a signal to this process
	// and all of the children.
	session_id = setsid();
	BESDEBUG("beslistener", "beslistener: The master beslistener session id (group id): " << session_id << endl);

	return ret;
}

int ServerApp::run()
{
	try {
		BESDEBUG("beslistener", "beslistener: initializing memory pool ... " << endl);
		BESMemoryManager::initialize_memory_pool();
		BESDEBUG("beslistener", "OK" << endl);

		SocketListener listener;
		if (_portVal) {
			_ts = new TcpSocket(_portVal);
			listener.listen(_ts);

			BESDEBUG("beslistener", "beslistener: listening on port (" << _portVal << ")" << endl);

			BESDEBUG("beslistener", "beslistener: about to write status (4)" << endl);
			// Write to stdout works because the besdaemon is listening on the
			// other end of a pipe where the pipe fd[1] has been dup2'd to
			// stdout. See daemon.cc:start_master_beslistener.
			// NB BESLISTENER_PIPE_FD is 1 (stdout)
			int status = BESLISTENER_RUNNING;
			int res = write(BESLISTENER_PIPE_FD, &status, sizeof(status));

			BESDEBUG("beslistener", "beslistener: wrote status (" << res << ")" << endl);
		}

		if (!_unixSocket.empty()) {
			_us = new UnixSocket(_unixSocket);
			listener.listen(_us);
			BESDEBUG("beslistener", "beslistener: listening on unix socket (" << _unixSocket << ")" << endl);
		}

		BESServerHandler handler;

		_ps = new PPTServer(&handler, &listener, _secure);

		register_signal_handlers();

		// Loop forever, processing signals and running the code in PPTServer::initConnection().
		// NB: The code in initConnection() used to loop forever, but I move that out to here
		// so the signal handlers could be in this class. The PPTServer::initConnection() method
		// is also used by daemon.cc but this class (ServerApp; the beslistener) and the besdaemon
		// need to do different things for the signals like HUP and TERM, so they cannot share
		// the signal processing code. One fix for the problem described in ticket 2025 was to
		// move the signal handlers into PPTServer. Changing how the 'forever' loops are organized
		// and keeping the signal processing code here (and in daemon.cc) is another solution that
		// preserves the correct behavior of the besdaemon, too. jhrg 3/5/14
		while (true) {
			block_signals();

			if (sigterm | sighup | sigchild | sigpipe) {
				int stat;
				pid_t cpid;
				while ((cpid = wait4(0 /*any child in the process group*/, &stat, WNOHANG, 0/*no rusage*/)) > 0) {
					_ps->decr_num_children();
					if (sigpipe) (*BESLog::TheLog()) << "Master listener caught SISPIPE from child: " << cpid << endl;

					BESDEBUG("ppt2", bes_exit_message(cpid, stat) << "; num children: " << _ps->get_num_children() << endl);
				}
			}

			if (sighup) {
				BESDEBUG("ppt2", "Master listener caught SIGHUP, exiting with SERVER_EXIT_RESTART" << endl);

				(*BESLog::TheLog()) << "Master listener caught SIGHUP, exiting with SERVER_EXIT_RESTART" << endl;
				::exit(SERVER_EXIT_RESTART);
			}

			if (sigterm) {
				BESDEBUG("ppt2", "Master listener caught SIGTERM, exiting with SERVER_NORMAL_SHUTDOWN" << endl);

				(*BESLog::TheLog()) << "Master listener caught SIGTERM, exiting with SERVER_NORMAL_SHUTDOWN" << endl;
				::exit(SERVER_EXIT_NORMAL_SHUTDOWN);
			}

			sigchild = 0; sigpipe = 0; // No need to reset sigterm or sighup.
			unblock_signals();

			_ps->initConnection();
		}

		_ps->closeConnection();
	}
	catch (BESError &se) {
		BESDEBUG("beslistener", "beslistener: caught BESError (" << se.get_message() << ")" << endl);

		(*BESLog::TheLog()) << se.get_message() << endl;
		int status = SERVER_EXIT_FATAL_CANNOT_START;
		write(BESLISTENER_PIPE_FD, &status, sizeof(status));
		close(BESLISTENER_PIPE_FD);
		return 1;
	}
	catch (...) {
		(*BESLog::TheLog()) << "caught unknown exception initializing sockets" << endl;
		int status = SERVER_EXIT_FATAL_CANNOT_START;
		write(BESLISTENER_PIPE_FD, &status, sizeof(status));
		close(BESLISTENER_PIPE_FD);
		return 1;
	}

	close(BESLISTENER_PIPE_FD);
	return 0;
}

int ServerApp::terminate(int sig)
{
	pid_t apppid = getpid();
	if (apppid == _mypid) {
		// These are all safe to call in a signalhandler
		if (_ps) {
			_ps->closeConnection();
			delete _ps;
		}
		if (_ts) {
			_ts->close();
			delete _ts;
		}
		if (_us) {
			_us->close();
			delete _us;
		}

		// Do this in the reverse order that it was initialized. So
		// terminate the loaded modules first, then the default
		// commands, then the default module.

		// These are not safe to call in a signal handler
		BESDEBUG("beslistener", "beslistener: terminating loaded modules ...  " << endl);
		BESModuleApp::terminate(sig);
		BESDEBUG("beslistener", "beslistener: done terminating loaded modules" << endl);

		BESDEBUG("beslistener", "beslistener: terminating default commands ...  " << endl);
		BESXMLDefaultCommands::terminate();
		BESDEBUG("beslistener", "beslistener: done terminating default commands ...  " << endl);

		BESDEBUG("beslistener", "beslistener: terminating default module ... " << endl);
		BESDefaultModule::terminate();
		BESDEBUG("beslistener", "beslistener: done terminating default module ... " << endl);
	}
	return sig;
}

/** @brief dumps information about this object
 *
 * Displays the pointer value of this instance
 *
 * @param strm C++ i/o stream to dump the information to
 */
void ServerApp::dump(ostream &strm) const
{
	strm << BESIndent::LMarg << "ServerApp::dump - (" << (void *) this << ")" << endl;
	BESIndent::Indent();
	strm << BESIndent::LMarg << "got port? " << _gotPort << endl;
	strm << BESIndent::LMarg << "port: " << _portVal << endl;
	strm << BESIndent::LMarg << "unix socket: " << _unixSocket << endl;
	strm << BESIndent::LMarg << "is secure? " << _secure << endl;
	strm << BESIndent::LMarg << "pid: " << _mypid << endl;
	if (_ts) {
		strm << BESIndent::LMarg << "tcp socket:" << endl;
		BESIndent::Indent();
		_ts->dump(strm);
		BESIndent::UnIndent();
	}
	else {
		strm << BESIndent::LMarg << "tcp socket: null" << endl;
	}
	if (_us) {
		strm << BESIndent::LMarg << "unix socket:" << endl;
		BESIndent::Indent();
		_us->dump(strm);
		BESIndent::UnIndent();
	}
	else {
		strm << BESIndent::LMarg << "unix socket: null" << endl;
	}
	if (_ps) {
		strm << BESIndent::LMarg << "ppt server:" << endl;
		BESIndent::Indent();
		_ps->dump(strm);
		BESIndent::UnIndent();
	}
	else {
		strm << BESIndent::LMarg << "ppt server: null" << endl;
	}
	BESModuleApp::dump(strm);
	BESIndent::UnIndent();
}

int main(int argc, char **argv)
{
	try {
		ServerApp app;
		return app.main(argc, argv);
	}
	catch (BESError &e) {
		cerr << "Caught unhandled exception: " << endl;
		cerr << e.get_message() << endl;
		return 1;
	}
	catch (...) {
		cerr << "Caught unhandled, unknown exception" << endl;
		return 1;
	}
	return 0;
}

