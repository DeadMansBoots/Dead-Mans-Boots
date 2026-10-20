/*
 * DmbIssueReport.cpp, part of Dead Man's Boots
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "DmbIssueReport.h"
#include "DmbIssueReportLimit.h"

#include "CConfigHandler.h"
#include "VCMIDirs.h"

#include <chrono>
#include <map>
#include <mutex>

#ifdef VCMI_WINDOWS
#include <windows.h>
#endif

VCMI_LIB_NAMESPACE_BEGIN

namespace DmbIssueReport
{
void send(const std::string & kind, const std::string & reason) noexcept
{
	try
	{
#ifdef VCMI_WINDOWS
		// a headless or --testmap client has nobody to ask and nothing worth sending; the consent is the crash report's own
		if(settings["session"]["headless"].Bool() || !settings["session"]["testmap"].isNull())
			return;
		if(!(settings["launcher"]["sendCrashReports"].Bool() && settings["launcher"]["crashReportsAsked"].Bool()))
			return;

		static std::mutex limitMutex;
		static DmbIssueReportLimit limit;
		{
			std::lock_guard guard(limitMutex);
			if(!limit.allow(kind, DmbIssueReportLimit::Clock::now()))
				return;
		}

		const std::string launcher = (VCMIDirs::get().clientPath().parent_path() / "VCMI_launcher.exe").string();
		// the reason is one argument: quotes are dropped, line ends become spaces, and it is cut at 600 characters
		std::string clean;
		for(const char c : reason)
			clean += (c == '"') ? '\'' : (c == '\n' || c == '\r') ? ' ' : c;
		if(clean.size() > 600)
			clean.resize(600);
		std::string line = "\"" + launcher + "\" --report-event " + kind + " \"" + clean + "\"";
		STARTUPINFOA startup = {};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION started = {};
		if(CreateProcessA(launcher.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &started))
		{
			CloseHandle(started.hThread);
			CloseHandle(started.hProcess);
		}
#else
		(void)kind;
		(void)reason;
#endif
	}
	catch(...)
	{
	}
}
}

VCMI_LIB_NAMESPACE_END
