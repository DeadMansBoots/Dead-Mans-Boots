/*
 * bugreport.h, part of Dead Man's Boots
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include <QString>

class QWidget;

/// DMB (K, October 10th: "auto-bug reporting ... so we can iterate on issues more quickly between userside error and live patch distribution",
/// "prefer automated one-click bug submission"): one button, or the launcher started with --report after a crash, builds a report from the
/// player's own logs and sends it.
///
/// The report is a zip in the user folder's Reports folder: the client, server and launcher logs (their last megabytes), the first-time setup log,
/// the newest play log, the mod list and settings (not persistentStorage.json, which holds lobby sign-ins), a summary of the last crash taken from
/// the client log, and the game's version and system. The Windows user name and the computer name are replaced in every text file. The crash
/// dump itself is not included (it carries paths and memory).
///
/// Sending: a build made with DMB_REPORT_TOKEN (a fine-grained GitHub token that can write only to the private reports repository) uploads the
/// zip there with one click. A build without it saves the zip, opens its folder and opens the repository's new-issue page with the summary
/// filled in, for the player to drop the zip into.
namespace BugReport
{
	struct Report
	{
		QString zipPath;
		QString summary;
		QString crash;
		QString description;
		QString error;
	};

	/// `description` is the player's own words (what went wrong), kept in the zip as description.txt
	Report build(const QString & why, const QString & description = {}, bool includeDump = true);
	bool send(const Report & report, QString & message);
	/// the whole flow, with the questions and thanks a player sees; `parent` may be null
	void run(QWidget * parent, const QString & why);
}
