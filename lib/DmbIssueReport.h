/*
 * DmbIssueReport.h, part of Dead Man's Boots
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once

VCMI_LIB_NAMESPACE_BEGIN

/// An anomaly the game can detect (a lost connection, a request the server refused) sent to the developers through the crash report path (K, October 19th:
/// "we just need VERBOSE, AND COMPLETE ISSUE REPORTING. AUTOMATICALLY"). The game starts "DMB_launcher.exe --report-event KIND REASON" with no window; the launcher
/// builds the same report as for a crash (logs, settings, mod list, the Windows user and computer name removed) and sends it, under the same consent keys:
/// launcher.sendCrashReports and launcher.crashReportsAsked (off until the player has said yes). Nothing is sent from a headless or --testmap client.
/// Rate limit (DmbIssueReportLimit): per kind, 10 minutes apart and three in an hour; twenty an hour over all kinds, from one process. Never blocks and never throws.
namespace DmbIssueReport
{
	/// KIND is a short word (connection-lost, request-refused); REASON is one text line for the developers: what happened, who was playing (host or guest), the turn settings
	DLL_LINKAGE void send(const std::string & kind, const std::string & reason) noexcept;
}

VCMI_LIB_NAMESPACE_END
