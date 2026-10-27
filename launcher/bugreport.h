/*
 * bugreport.h, part of Dead Man's Boots
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>

class QWidget;

/// DMB (K, October 10th: "auto-bug reporting ... so we can iterate on issues more quickly between userside error and live patch distribution",
/// "prefer automated one-click bug submission"): one button, or the launcher started with --report after a crash, builds a report from the
/// player's own logs and sends it.
///
/// The report is a zip in the user folder's Reports folder: the client, server and launcher logs (their last megabytes), the first-time setup log,
/// the newest play log, the mod list and settings (not persistentStorage.json, which holds lobby sign-ins), the list of the Mods folder, a summary of the
/// last crash taken from the client log, the crash dump when the game crashed in the last day, and the game's version and system. The Windows user name and the
/// computer name are replaced in every text file and overwritten in the dump; the settings lose the name of a game joined and the player names; addresses of
/// other computers and a OneDrive folder's owner are replaced everywhere.
///
/// Sending: the report goes to the address that the update feed names (key "reportEndpoint"; neither a key nor an address is built into the program, and an HTTPS
/// address is required). An address with "/minidump/" in it is Sentry's minidump endpoint: the dump (gzip compressed, when it keeps the request under 40 MB), the
/// scrubbed text files one by one as attachments and a "sentry" field with the release and a fingerprint of its own, so each report is an issue of its own. Any other
/// address is the report relay: the whole zip in one POST to <address>/report.
/// With no address there, or no connection, the zip is saved, its folder opens, and the repository's new-issue page opens with the
/// summary filled in, for the player to drop the zip into. Settings, General has a switch (launcher.sendCrashReports) for the crashes the launcher
/// sends by itself.
namespace BugReport
{
	struct Report
	{
		QString zipPath;
		QString summary;
		QString crash;
		QString description;
		QString error;
		/// the scrubbed text files of the zip (name inside the zip, content), for an upload that sends them one by one (Sentry's minidump address)
		QList<QPair<QString, QByteArray>> parts;
		/// the scrubbed crash dump (the same bytes as in the zip), empty when there is none or the report was made without it
		QByteArray dump;
		QString dumpName;
	};

	/// `description` is the player's own words (what went wrong), kept in the zip as description.txt. `dumpPath`, when given, is the
	/// crash dump to attach (the crash window's own); without it, the newest dump of the last day in the logs folder
	Report build(const QString & why, const QString & description = {}, bool includeDump = true, const QString & dumpPath = {});
	bool send(const Report & report, QString & message);

	/// Settings, General, "Send crash reports to the developers automatically" (launcher.sendCrashReports in settings.json, off until the player has answered the launcher question, launcher.crashReportsAsked)
	bool crashReportsEnabled();
	/// the whole flow, with the questions and thanks a player sees; `parent` may be null
	void run(QWidget * parent, const QString & why);
	/// the window the game's crash handler starts (DMB_launcher.exe --crash-window <dump>): what happened, Send report and Don't send, what is sent and where the crash file
	/// stays; with the switch (crashReportsEnabled) on it sends at once and says so
	void runCrashWindow(const QString & dumpPath);
}
