/*
 * CConsoleHandler.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CConsoleHandler.h"
#include "CConfigHandler.h"

#include "CThreadHelper.h"
#include "VCMIDirs.h"

#include <boost/filesystem.hpp>
#include <boost/stacktrace.hpp>

#if defined(NDEBUG) && !defined(VCMI_ANDROID)
#define USE_ON_TERMINATE
#endif

#if defined(NDEBUG) && defined(VCMI_WINDOWS)
#define USE_UNHANDLED_EXCEPTION_FILTER
#define CREATE_MEMORY_DUMP
#endif

#ifndef VCMI_WINDOWS
constexpr const char * CONSOLE_GREEN = "\x1b[1;32m";
constexpr const char * CONSOLE_RED = "\x1b[1;31m";
constexpr const char * CONSOLE_MAGENTA = "\x1b[1;35m";
constexpr const char * CONSOLE_YELLOW = "\x1b[1;33m";
constexpr const char * CONSOLE_WHITE = "\x1b[1;37m";
constexpr const char * CONSOLE_GRAY = "\x1b[1;30m";
constexpr const char * CONSOLE_TEAL = "\x1b[1;36m";
#else
#include <windows.h>
#include <dbghelp.h>
#include <csignal>
#include <cstdlib>
#ifndef __MINGW32__
	#pragma comment(lib, "dbghelp.lib")
#endif
HANDLE handleIn;
HANDLE handleOut;
HANDLE handleErr;
constexpr int32_t CONSOLE_GREEN = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
constexpr int32_t CONSOLE_RED = FOREGROUND_RED | FOREGROUND_INTENSITY;
constexpr int32_t CONSOLE_MAGENTA = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
constexpr int32_t CONSOLE_YELLOW = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
constexpr int32_t CONSOLE_WHITE = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
constexpr int32_t CONSOLE_GRAY = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
constexpr int32_t CONSOLE_TEAL = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
#endif

VCMI_LIB_NAMESPACE_BEGIN

#ifdef CREATE_MEMORY_DUMP

/// Keeps the newest few crash dumps of this program in the logs folder, so crashes over weeks do not fill the disk
static void pruneOldDumps(const boost::filesystem::path & folder, const std::string & prefix, size_t keep)
{
	boost::system::error_code ec;
	std::vector<std::pair<std::time_t, boost::filesystem::path>> dumps;
	for(boost::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
	{
		const auto name = it->path().filename().string();
		if(name.rfind(prefix, 0) == 0 && it->path().extension() == ".dmp")
			dumps.emplace_back(boost::filesystem::last_write_time(it->path(), ec), it->path());
	}
	std::sort(dumps.begin(), dumps.end());
	for(size_t i = 0; i + keep < dumps.size(); ++i)
		boost::filesystem::remove(dumps[i].second, ec);
}

static void createMemoryDump(MINIDUMP_EXCEPTION_INFORMATION * meinfo)
{
	// The dump goes into the logs folder beside the game's log (Documents\My Games\Dead Man's Boots\logs), named by
	// program, time and thread: the program's own folder is often not writable for the player (an installer puts it
	// under an administrator-owned folder), so the old place, next to the program, got no dump at all (October 6th 2026)
	char buffer[MAX_PATH + 1] = {};
	GetModuleFileNameA(nullptr, buffer, MAX_PATH);
	const char * program = strrchr(buffer, '\\');
	program = program ? program + 1 : buffer;

	SYSTEMTIME now;
	GetLocalTime(&now);
	char stamp[64];
	snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d_%02d-%02d-%02d_thread%lu", now.wYear, now.wMonth, now.wDay,
		now.wHour, now.wMinute, now.wSecond, static_cast<unsigned long>(GetCurrentThreadId()));

	boost::system::error_code ec;
	boost::filesystem::path folder = VCMIDirs::get().userLogsPath();
	boost::filesystem::create_directories(folder, ec);
	const std::string prefix = std::string(program) + "_crash_";
	const boost::filesystem::path dumpPath = folder / (prefix + stamp + ".dmp");

	HANDLE dfile = CreateFileW(dumpPath.c_str(), GENERIC_READ|GENERIC_WRITE, FILE_SHARE_WRITE|FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
	if(dfile == INVALID_HANDLE_VALUE)
	{
		logGlobal->error("Crash info could not be written to %s (error %lu)", dumpPath.string(), static_cast<unsigned long>(GetLastError()));
		return;
	}
	logGlobal->error("Crash info will be put in %s", dumpPath.string());

	auto dumpType = MiniDumpWithDataSegs;

	if(settings["general"]["extraDump"].Bool())
	{
		dumpType = (MINIDUMP_TYPE)(
			MiniDumpWithFullMemory
			| MiniDumpWithFullMemoryInfo
			| MiniDumpWithHandleData
			| MiniDumpWithUnloadedModules
			| MiniDumpWithThreadInfo);
	}

	MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dfile, dumpType, meinfo, nullptr, nullptr);
	CloseHandle(dfile);
	pruneOldDumps(folder, prefix, 5);
	// DMB (K, October 9th: "dmb also reports 'VCMI has crashed' we want to update that"): the game's own name, and where the files are, since
	// a player is asked to send them
	// K, October 10th ("you can't see crashes you should"): the launcher is started at once, with no window, to build the report (the logs, the settings, the Mods
	// listing and this dump) and send it; the player is told, and can add what they were doing with F12 or Help, Report a bug
	const std::string launcher = (VCMIDirs::get().clientPath().parent_path() / "VCMI_launcher.exe").string();
	std::string commandLine = "\"" + launcher + "\" --report-crash";
	STARTUPINFOA startup = {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION started = {};
	const bool sending = CreateProcessA(launcher.c_str(), commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &started);
	if(sending)
	{
		CloseHandle(started.hThread);
		CloseHandle(started.hProcess);
	}
	const std::string crashMessage = std::string("Dead Man's Boots has crashed. Sorry about that.\n\n")
		+ (sending ? "A report with the game's logs was sent to the developers by itself (your Windows user name and computer name are removed from it). When the game is running again, "
			"F12 lets you add what you were doing.\n\n" : "")
		+ "A file about the problem was saved here:\n" + dumpPath.string();
	MessageBoxA(0, crashMessage.c_str(), "Dead Man's Boots crash report", MB_OK | MB_ICONERROR);
}

#endif

#ifdef USE_UNHANDLED_EXCEPTION_FILTER
const char* exceptionName(DWORD exc)
{
#define EXC_CASE(EXC)	case EXCEPTION_##EXC : return "EXCEPTION_" #EXC
	switch (exc)
	{
		EXC_CASE(ACCESS_VIOLATION);
		EXC_CASE(DATATYPE_MISALIGNMENT);
		EXC_CASE(BREAKPOINT);
		EXC_CASE(SINGLE_STEP);
		EXC_CASE(ARRAY_BOUNDS_EXCEEDED);
		EXC_CASE(FLT_DENORMAL_OPERAND);
		EXC_CASE(FLT_DIVIDE_BY_ZERO);
		EXC_CASE(FLT_INEXACT_RESULT);
		EXC_CASE(FLT_INVALID_OPERATION);
		EXC_CASE(FLT_OVERFLOW);
		EXC_CASE(FLT_STACK_CHECK);
		EXC_CASE(FLT_UNDERFLOW);
		EXC_CASE(INT_DIVIDE_BY_ZERO);
		EXC_CASE(INT_OVERFLOW);
		EXC_CASE(PRIV_INSTRUCTION);
		EXC_CASE(IN_PAGE_ERROR);
		EXC_CASE(ILLEGAL_INSTRUCTION);
		EXC_CASE(NONCONTINUABLE_EXCEPTION);
		EXC_CASE(STACK_OVERFLOW);
		EXC_CASE(INVALID_DISPOSITION);
		EXC_CASE(GUARD_PAGE);
		EXC_CASE(INVALID_HANDLE);
	case 0xE06D7363: return "C++ EXCEPTION";
	default:
		return "UNKNOWN EXCEPTION";
	}
#undef EXC_CASE
}

/// "VCMI_lib.dll+0x1a2b3c" plus the function name when a matching .pdb sits beside the program
static std::string describeAddress(HANDLE process, const void * address)
{
	std::string text = "?";
	HMODULE module = nullptr;
	char file[MAX_PATH + 1] = {};
	if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCSTR>(address), &module)
		&& GetModuleFileNameA(module, file, MAX_PATH))
	{
		const char * name = strrchr(file, '\\');
		char offset[40];
		snprintf(offset, sizeof(offset), "+0x%llx", static_cast<unsigned long long>(reinterpret_cast<const char *>(address) - reinterpret_cast<const char *>(module)));
		text = std::string(name ? name + 1 : file) + offset;
	}

	alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + 256] = {};
	auto * symbol = reinterpret_cast<SYMBOL_INFO *>(symbolBuffer);
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	symbol->MaxNameLen = 255;
	DWORD64 displacement = 0;
	if(SymFromAddr(process, reinterpret_cast<DWORD64>(address), &displacement, symbol))
		text += std::string(" ") + symbol->Name + "+0x" + std::to_string(displacement);
	return text;
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* exception)
{
	logGlobal->error("Disaster happened.");

	PEXCEPTION_RECORD einfo = exception->ExceptionRecord;

	// which exception, and where: before October 7th 2026 only an access violation was named, so a crash by any other
	// kind (a division by zero, say) left nothing to go on in the log
	const HANDLE process = GetCurrentProcess();
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS);
	SymInitialize(process, nullptr, TRUE);
	logGlobal->error("Exception %s (0x%08lx) at %s", exceptionName(einfo->ExceptionCode), static_cast<unsigned long>(einfo->ExceptionCode),
		describeAddress(process, einfo->ExceptionAddress));

	if (einfo->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
	{
		logGlobal->error("Attempt to %s 0x%8x", (einfo->ExceptionInformation[0] == 1 ? "write to" : "read from"), (void*)einfo->ExceptionInformation[1]);
	}

	void * frames[48] = {};
	const USHORT frameCount = CaptureStackBackTrace(0, 48, frames, nullptr);
	for(USHORT i = 0; i < frameCount; ++i)
		logGlobal->error("  frame %d: %s", static_cast<int>(i), describeAddress(process, frames[i]));

	const DWORD threadId = ::GetCurrentThreadId();
	logGlobal->error("Thread ID: %d", threadId);

	//exception info to be placed in the dump
	MINIDUMP_EXCEPTION_INFORMATION meinfo = {threadId, exception, TRUE};

#ifdef CREATE_MEMORY_DUMP
	createMemoryDump(&meinfo);
#endif

	return EXCEPTION_EXECUTE_HANDLER;
}
#endif

#ifdef USE_UNHANDLED_EXCEPTION_FILTER
/// every other way the program can be ended by itself (K, October 10th: "you can't see crashes you should"): a failed runtime check, a pure virtual call, abort():
/// the same log lines and dump as an exception, then the program ends
[[noreturn]] static void onFatal(const char * what)
{
	logGlobal->error("Disaster happened. %s", what);
	const HANDLE process = GetCurrentProcess();
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS);
	SymInitialize(process, nullptr, TRUE);
	void * frames[48] = {};
	const USHORT frameCount = CaptureStackBackTrace(0, 48, frames, nullptr);
	for(USHORT i = 0; i < frameCount; ++i)
		logGlobal->error("  frame %d: %s", static_cast<int>(i), describeAddress(process, frames[i]));
	logGlobal->error("Thread ID: %d", static_cast<int>(::GetCurrentThreadId()));
	createMemoryDump(nullptr);
	TerminateProcess(process, 3);
	std::abort();
}

static void onInvalidParameter(const wchar_t *, const wchar_t *, const wchar_t *, unsigned int, uintptr_t)
{
	onFatal("A function of the C runtime was given an invalid argument.");
}

static void onPureCall()
{
	onFatal("A pure virtual function was called.");
}

static void onAbortSignal(int)
{
	onFatal("abort() was called.");
}
#endif

#ifdef USE_ON_TERMINATE
[[noreturn]] static void onTerminate()
{
	logGlobal->error("Disaster happened.");
	try
	{
		std::exception_ptr eptr{std::current_exception()};
		if (eptr)
		{
			std::rethrow_exception(eptr);
		}
		else
		{
			logGlobal->error("...but no current exception found!");
		}
	}
	catch (const std::exception& exc)
	{
		logGlobal->error("Reason: %s", exc.what());
	}
	catch (...)
	{
		logGlobal->error("Reason: unknown exception!");
	}

	logGlobal->error("Call stack information:");
	std::stringstream stream;
	stream << boost::stacktrace::stacktrace();
	logGlobal->error("%s", stream.str());

#ifdef CREATE_MEMORY_DUMP
	const DWORD threadId = ::GetCurrentThreadId();
	logGlobal->error("Thread ID: %d", threadId);

	createMemoryDump(nullptr);
#endif
	std::abort();
}
#endif

void CConsoleHandler::setColor(EConsoleTextColor color)
{
	TColor colorCode;
	switch(color)
	{
	case EConsoleTextColor::DEFAULT:
		colorCode = defColor;
		break;
	case EConsoleTextColor::GREEN:
		colorCode = CONSOLE_GREEN;
		break;
	case EConsoleTextColor::RED:
		colorCode = CONSOLE_RED;
		break;
	case EConsoleTextColor::MAGENTA:
		colorCode = CONSOLE_MAGENTA;
		break;
	case EConsoleTextColor::YELLOW:
		colorCode = CONSOLE_YELLOW;
		break;
	case EConsoleTextColor::WHITE:
		colorCode = CONSOLE_WHITE;
		break;
	case EConsoleTextColor::GRAY:
		colorCode = CONSOLE_GRAY;
		break;
	case EConsoleTextColor::TEAL:
		colorCode = CONSOLE_TEAL;
		break;
	default:
		colorCode = defColor;
		break;
	}
#ifdef VCMI_WINDOWS
	SetConsoleTextAttribute(handleOut, colorCode);
	if (color == EConsoleTextColor::DEFAULT)
		colorCode = defErrColor;
	SetConsoleTextAttribute(handleErr, colorCode);
#else
	std::cout << colorCode;
#endif
}

int CConsoleHandler::run()
{
	setThreadName("consoleHandler");
	//disabling sync to make in_avail() work (othervice always returns 0)
	{
		std::lock_guard guard(smx);
		std::ios::sync_with_stdio(false);
	}
	std::string buffer;

	while ( std::cin.good() )
	{
#ifndef _MSC_VER
		//check if we have some unreaded symbols
		if (std::cin.rdbuf()->in_avail())
		{
			if ( getline(std::cin, buffer).good() )
				if ( cb )
					cb(buffer, false);
		}

		std::unique_lock guard(shutdownMutex);
		shutdownVariable.wait_for(guard, std::chrono::seconds(1));

		if (shutdownPending)
			return -1;
#else
		std::getline(std::cin, buffer);
		if ( cb )
			cb(buffer, false);
#endif
	}
	return -1;
}

CConsoleHandler::CConsoleHandler()
	:CConsoleHandler(std::function<void(const std::string &, bool)>{})
{}

CConsoleHandler::CConsoleHandler(const std::function<void(const std::string &, bool)> & callback)
	:cb(callback)
{
#ifdef VCMI_WINDOWS
	handleIn = GetStdHandle(STD_INPUT_HANDLE);
	handleOut = GetStdHandle(STD_OUTPUT_HANDLE);
	handleErr = GetStdHandle(STD_ERROR_HANDLE);

	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(handleOut,&csbi);
	defColor = csbi.wAttributes;

	GetConsoleScreenBufferInfo(handleErr, &csbi);
	defErrColor = csbi.wAttributes;
#else
	defColor = "\x1b[0m";
#endif

#ifdef USE_UNHANDLED_EXCEPTION_FILTER
	SetUnhandledExceptionFilter(onUnhandledException);
	_set_invalid_parameter_handler(onInvalidParameter);
	_set_purecall_handler(onPureCall);
	std::signal(SIGABRT, onAbortSignal);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT); // the program's own handler above, not the runtime's message box
#endif

#ifdef USE_ON_TERMINATE
	std::set_terminate(onTerminate);
#endif
}

CConsoleHandler::~CConsoleHandler()
{
	logGlobal->info("Killing console...");
	end();
	logGlobal->info("Killing console... done!");
}
void CConsoleHandler::end()
{
	if (thread.joinable())
	{
#ifndef _MSC_VER
		shutdownPending = true;
		shutdownVariable.notify_all();
#else
		TerminateThread(thread.native_handle(),0);
#endif
		thread.join();
	}
}

void CConsoleHandler::start()
{
	thread = std::thread(std::bind(&CConsoleHandler::run, this));
}

VCMI_LIB_NAMESPACE_END
