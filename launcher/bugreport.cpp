/*
 * bugreport.cpp, part of Dead Man's Boots
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "bugreport.h"

#include "helper.h"

#include "../lib/DmbRelease.h"
#include "../lib/VCMIDirs.h"
#include "../lib/GameConstants.h"
#include "../lib/filesystem/CZipSaver.h"
#include "../lib/filesystem/Filesystem.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QWidget>

namespace
{
constexpr qint64 LOG_TAIL_BYTES = 4 * 1024 * 1024;
constexpr qint64 PLAY_LOG_TAIL_BYTES = 6 * 1024 * 1024;

QString userFolder(const std::string & path)
{
	return QString::fromStdString(path);
}

/// the last `limit` bytes of a file (a log can run to many megabytes; its end is what matters)
QByteArray tailOf(const QString & path, qint64 limit)
{
	QFile file(path);
	if(!file.open(QIODevice::ReadOnly))
		return {};
	if(file.size() > limit)
		file.seek(file.size() - limit);
	return file.readAll();
}

/// the Windows user name and the computer name replaced, whatever the path style or case; a name of one or two letters is replaced only as a folder of
/// a user path (a folder named so under the Users folder), since anywhere else it is an ordinary word
QByteArray redacted(const QByteArray & data)
{
	QString text = QString::fromUtf8(data);
	const auto replaceName = [&text](const QString & name, const QString & with)
	{
		if(name.size() >= 3)
			text.replace(name, with, Qt::CaseInsensitive);
		else if(!name.isEmpty())
			text.replace(QRegularExpression("(Users[\\\\/])" + QRegularExpression::escape(name) + "(?=[\\\\/\"\\s]|$)", QRegularExpression::CaseInsensitiveOption), "\\1" + with);
	};
	replaceName(qEnvironmentVariable("USERNAME"), "<user>");
	replaceName(QDir::home().dirName(), "<user>");
	replaceName(qEnvironmentVariable("COMPUTERNAME"), "<computer>");
	// a folder named after an employer or a person that OneDrive adds ("OneDrive - Contoso"), and addresses of other computers (not this one's loopback)
	text.replace(QRegularExpression("OneDrive - [^\\\\/\"\r\n]+"), "OneDrive - <name>");
	text.replace(QRegularExpression("\\b(?!127\\.|0\\.0\\.0\\.0)(?:(?:25[0-5]|2[0-4]\\d|1?\\d?\\d)\\.){3}(?:25[0-5]|2[0-4]\\d|1?\\d?\\d)\\b(?![\\w.])"), "<ip>");
	// IPv6 addresses (written in full or with ::)
	text.replace(QRegularExpression("\\b(?:[0-9a-fA-F]{1,4}:){7}[0-9a-fA-F]{1,4}\\b|\\b(?:[0-9a-fA-F]{1,4}:){1,6}:(?:[0-9a-fA-F]{1,4}(?::[0-9a-fA-F]{1,4}){0,5})?(?![\\w:])"), "<ip>");
	return text.toUtf8();
}

/// settings.json for a report: the host name or address of a game joined (server.remoteHostname), the player's name and the names of the other players on
/// the same PC are blanked, then the usual redaction
QByteArray scrubbedSettings(const QByteArray & data)
{
	QString text = QString::fromUtf8(data);
	text.replace(QRegularExpression("(\"remoteHostname\"\\s*:\\s*)\"[^\"]*\""), "\\1\"<removed>\"");
	text.replace(QRegularExpression("(\"playerName\"\\s*:\\s*)\"[^\"]*\""), "\\1\"<removed>\"");
	text.replace(QRegularExpression("(\"multiPlayerNames\"\\s*:\\s*)\\[[^\\]]*\\]"), "\\1[]");
	// other text a player types or pastes: the autosave/save prefix, the host the game's own server binds to, an extra mod repository address (it can carry a key)
	for(const char * key : {"savePrefix", "localHostname", "extraRepositoryURL"})
		text.replace(QRegularExpression(QString("(\"%1\"\\s*:\\s*)\"[^\"]*\"").arg(key)), "\\1\"<removed>\"");
	return redacted(text.toUtf8());
}

/// The crash dump with the Windows user name and computer name overwritten by filler of the same length (a dump lists the full path of every module,
/// and a per-user install sits under C:\Users\<name>), in the plain and in the wide spelling, as written, lower and upper case. Same length, so the
/// dump stays a valid dump
void scrubDump(QByteArray & dump)
{
	const auto overwrite = [&dump](const QString & name)
	{
		if(name.isEmpty())
			return;
		if(name.size() < 3)
		{
			// a short name is overwritten only where it is a user path's folder ("Users\Al\"), in the plain and the wide spelling, as written, lower and upper case
			for(const QString & variant : {name, name.toLower(), name.toUpper()})
				for(const QString & separator : {QString("\\"), QString("/")})
				{
					const QString whole = "Users" + separator + variant + separator;
					const int nameAt = 6; // after "Users" and the separator
					const QByteArray narrow = whole.toUtf8();
					QByteArray filler = narrow;
					filler.replace(nameAt, variant.toUtf8().size(), QByteArray(variant.toUtf8().size(), 'x'));
					for(int at = dump.indexOf(narrow); at >= 0; at = dump.indexOf(narrow, at + narrow.size()))
						dump.replace(at, narrow.size(), filler);
					const QByteArray wide(reinterpret_cast<const char *>(whole.utf16()), whole.size() * 2);
					QByteArray wideFiller = wide;
					for(int i = 0; i < variant.size(); ++i)
					{
						wideFiller[(nameAt + i) * 2] = 'x';
						wideFiller[(nameAt + i) * 2 + 1] = '\0';
					}
					for(int at = dump.indexOf(wide); at >= 0; at = dump.indexOf(wide, at + wide.size()))
						dump.replace(at, wide.size(), wideFiller);
				}
			return;
		}
		for(const QString & variant : {name, name.toLower(), name.toUpper()})
		{
			// as written in UTF-8 and in the PC's local code page (for a name with letters outside ASCII the two differ)
			for(const QByteArray & narrow : {variant.toUtf8(), variant.toLocal8Bit()})
			{
				if(narrow.isEmpty())
					continue;
				const QByteArray filler(narrow.size(), 'x');
				for(int at = dump.indexOf(narrow); at >= 0; at = dump.indexOf(narrow, at + narrow.size()))
					dump.replace(at, narrow.size(), filler);
			}
			QByteArray wide(reinterpret_cast<const char *>(variant.utf16()), variant.size() * 2);
			QByteArray wideFiller;
			for(int i = 0; i < variant.size(); ++i)
				wideFiller.append('x').append('\0');
			for(int at = dump.indexOf(wide); at >= 0; at = dump.indexOf(wide, at + wide.size()))
				dump.replace(at, wide.size(), wideFiller);
		}
	};
	overwrite(qEnvironmentVariable("USERNAME"));
	overwrite(QDir::home().dirName());
	overwrite(qEnvironmentVariable("COMPUTERNAME"));
}

/// the last crash in the client's log: from "Disaster happened" on, at most 60 lines
QString crashSummary(const QString & clientLog)
{
	const QStringList lines = clientLog.split('\n');
	int start = -1;
	for(int i = lines.size() - 1; i >= 0; --i)
		if(lines[i].contains("Disaster happened"))
		{
			start = i;
			break;
		}
	if(start < 0)
		return {};
	return lines.mid(start, 60).join('\n');
}

QString systemInfo(const QString & why)
{
	QString info;
	QTextStream ts(&info);
	ts << "Dead Man's Boots: " << DMB_RELEASE_TEXT << '\n';
	ts << "Dead Man's Boots version: " << DMB_VERSION_STRING << '\n';
	ts << "VCMI version: " << QString::fromStdString(GameConstants::VCMI_VERSION) << '\n';
	ts << "Operating system: " << QSysInfo::prettyProductName() << " (" << QSysInfo::kernelVersion() << ")\n";
	ts << "CPU architecture: " << QSysInfo::currentCpuArchitecture() << '\n';
	// which build the crash addresses belong to: the first 8 characters of the game program's sha256 name the folder of its symbols (build_dmb.py, symbolize.py)
	{
		QFile client(QString::fromStdString(VCMIDirs::get().clientPath().string()));
		if(client.open(QIODevice::ReadOnly))
			ts << "Game program id: " << QCryptographicHash::hash(client.readAll(), QCryptographicHash::Sha256).toHex().left(8) << '\n';
	}
	ts << "Report made: " << QDateTime::currentDateTime().toString(Qt::ISODate) << " (" << why << ")\n";
	return info;
}

void addToZip(CZipSaver & saver, const std::string & name, const QByteArray & data)
{
	auto stream = saver.addFile(name);
	stream->write(reinterpret_cast<const ui8 *>(data.constData()), data.size());
}
}

namespace BugReport
{
Report build(const QString & why, const QString & description, bool includeDump)
{
	Report report;
	try
	{
		const QString logs = userFolder(VCMIDirs::get().userLogsPath().string());
		const QString config = userFolder(VCMIDirs::get().userConfigPath().string());
		const QString data = userFolder(VCMIDirs::get().userDataPath().string());

		QDir().mkpath(data + "/Reports");
		const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
		const QString name = QString("DMB-report-%1-%2.zip").arg(stamp).arg(QString::number(QRandomGenerator::global()->generate() & 0xFFFFFF, 16));
		report.zipPath = data + "/Reports/" + name;

		std::shared_ptr<CIOApi> api = std::make_shared<CDefaultIOApi>();
		CZipSaver saver(api, boost::filesystem::path(report.zipPath.toStdString()));

		QString clientLog;
		for(const QFileInfo & file : QDir(logs).entryInfoList({"*.txt"}, QDir::Files, QDir::Name))
		{
			QByteArray text = tailOf(file.absoluteFilePath(), LOG_TAIL_BYTES);
			if(file.fileName() == "VCMI_Client_log.txt")
				clientLog = QString::fromUtf8(text);
			addToZip(saver, "logs/" + file.fileName().toStdString(), redacted(text));
		}
		for(const QString & jsonName : {"settings.json", "modSettings.json"})
		{
			const QByteArray text = tailOf(config + "/" + jsonName, 2 * 1024 * 1024);
			if(!text.isEmpty())
				addToZip(saver, "config/" + jsonName.toStdString(), scrubbedSettings(text));
		}
		// the newest play log (what was clicked and what the game answered), its end
		QFileInfoList playLogs = QDir(data + "/PlayLogs").entryInfoList({"*.jsonl"}, QDir::Files, QDir::Time);
		if(!playLogs.isEmpty())
			addToZip(saver, "playlog/" + playLogs.first().fileName().toStdString(), redacted(tailOf(playLogs.first().absoluteFilePath(), PLAY_LOG_TAIL_BYTES)));

		if(!description.trimmed().isEmpty())
			addToZip(saver, "description.txt", redacted(description.toUtf8()));
		// every file of the user's Mods folder with its size (K, October 10th, on a crash that only one player had): a mod that is incomplete or different
		// on one PC shows up against another player's listing; paths are relative to the user folder, so no Windows user name is in them
		{
			QString listing;
			QTextStream out(&listing);
			int count = 0;
			QDirIterator files(data + "/Mods", QDir::Files, QDirIterator::Subdirectories);
			while(files.hasNext() && count < 400000)
			{
				files.next();
				out << files.filePath().mid(data.size() + 1) << '\t' << files.fileInfo().size() << '\n';
				++count;
			}
			if(count > 0)
				addToZip(saver, "mods-listing.txt", redacted(listing.toUtf8()));
		}
		// the newest crash dump of the game, when it is from the last day (a dump holds memory: the reports go to a private repository); when the whole
		// report comes out over 40 MB the report is made again without it (the upload takes it as one piece)
		if(includeDump)
		{
			const QFileInfoList dumps = QDir(logs).entryInfoList({"*_crash_*.dmp"}, QDir::Files, QDir::Time);
			if(!dumps.isEmpty() && dumps.first().lastModified().secsTo(QDateTime::currentDateTime()) < 24 * 3600)
			{
				QFile dump(dumps.first().absoluteFilePath());
				if(dumps.first().size() <= 120 * 1024 * 1024 && dump.open(QIODevice::ReadOnly))
					{ QByteArray bytes = dump.readAll(); scrubDump(bytes); addToZip(saver, "crash/" + dumps.first().fileName().toStdString(), bytes); }
			}
		}
		const QString crash = crashSummary(clientLog);
		if(!crash.isEmpty())
			addToZip(saver, "crash-summary.txt", redacted(crash.toUtf8()));
		const QString info = systemInfo(why);
		addToZip(saver, "report-info.txt", redacted(info.toUtf8()));

		report.summary = info;
		report.description = description;
		report.crash = QString::fromUtf8(redacted(crash.toUtf8()));
	}
	catch(const std::exception & e)
	{
		report.error = QString::fromUtf8(e.what());
	}
	// the relay keeps a report as one Workers KV value (25 MiB at most, 24 MB accepted): a zip over 20 MB is made again without the crash file, whole and not cut (the
	// crash summary and frames in the log stay); a real dump is about 40 MB and zips to about 10 MB
	if(includeDump && report.error.isEmpty() && QFileInfo(report.zipPath).size() > 20 * 1024 * 1024)
	{
		QFile::remove(report.zipPath);
		return build(why, description, false);
	}
	return report;
}

/// The player's settings.json read as plain JSON (the report modes run before the launcher's resource handler and settings are set up, and a crash report must not
/// depend on them): the file is the one the game and the launcher write, so a missing key means its default
QJsonObject launcherSettings()
{
	QFile file(userFolder(VCMIDirs::get().userConfigPath().string()) + "/settings.json");
	if(!file.open(QIODevice::ReadOnly))
		return {};
	return QJsonDocument::fromJson(file.readAll()).object()["launcher"].toObject();
}

/// The address the reports go to, read from the update feed (the file the launcher already reads for updates; the repository's dmb-updates.json, key
/// "reportEndpoint"). No address and no key are built into the program: the developers can move the relay or switch uploads off for everyone by editing
/// the feed, and a program taken apart gives nobody anything to write to
QString reportEndpoint(QNetworkAccessManager & manager, QString & message)
{
	const QString feed = launcherSettings()["updateConfigUrl"].toString("https://raw.githubusercontent.com/DeadMansBoots/DMB-Mods-Repository/main/dmb-updates.json");
	QNetworkRequest request{QUrl(feed)};
	request.setRawHeader("User-Agent", "DeadMansBoots-Launcher");
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	QNetworkReply * reply = manager.get(request);
	QEventLoop loop;
	QTimer timeout;
	timeout.setSingleShot(true);
	QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	timeout.start(30 * 1000);
	loop.exec();
	QString endpoint;
	if(reply->isFinished() && reply->error() == QNetworkReply::NoError)
		endpoint = QJsonDocument::fromJson(reply->readAll()).object()["reportEndpoint"].toString();
	else
		message = reply->isFinished() ? reply->errorString() : QString("the update information could not be reached");
	reply->deleteLater();
	if(endpoint.isEmpty() && message.isEmpty())
		message = "automatic uploads are switched off";
	return endpoint;
}

bool send(const Report & report, QString & message)
{
	QFile file(report.zipPath);
	if(!file.open(QIODevice::ReadOnly))
	{
		message = "the report could not be read";
		return false;
	}

	QNetworkAccessManager manager;
	const QString endpoint = reportEndpoint(manager, message);
	const QUrl endpointUrl(endpoint);
	const bool loopback = endpointUrl.scheme() == "http" && endpointUrl.host() == "127.0.0.1"; // only for the tests of this program (the host exactly: "127.0.0.1.example.com" is not it)
	if(endpoint.isEmpty() || !(endpointUrl.scheme() == "https" || loopback))
	{
		if(message.isEmpty())
			message = "no upload address";
		return false;
	}

	QNetworkRequest request{QUrl(endpoint + (endpoint.endsWith('/') ? "" : "/") + "report")};
	request.setRawHeader("User-Agent", "DeadMansBoots-Launcher");
	request.setRawHeader("X-DMB-Report-Name", QFileInfo(report.zipPath).fileName().toUtf8());
	request.setRawHeader("X-DMB-Build", QByteArray(DMB_RELEASE_TEXT));
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/zip");
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	QNetworkReply * reply = manager.post(request, &file);
	QEventLoop loop;
	QTimer timeout;
	timeout.setSingleShot(true);
	QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	timeout.start(180 * 1000);
	loop.exec();
	const bool done = reply->isFinished() && reply->error() == QNetworkReply::NoError
		&& reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
	message = done ? QString() : (reply->isFinished() ? reply->errorString() : QString("the upload timed out"));
	reply->deleteLater();
	return done;
}

bool crashReportsEnabled()
{
	return launcherSettings()["sendCrashReports"].toBool(true);
}

void run(QWidget * parent, const QString & why)
{
	// the player's own words are the most useful part of a report (K, October 10th: "we want to be able to solve bugs quick"); empty is allowed, Cancel stops
	bool asked = false;
	const QString description = QInputDialog::getMultiLineText(parent, QObject::tr("Report a bug"),
		QObject::tr("What went wrong? A sentence or two helps a lot (what you were doing, what you expected). You can leave this empty.\n\nThe report also holds the game's logs and settings, with your Windows user name and computer name removed."),
		QString(), &asked);
	if(!asked)
		return;
	const Report report = build(why, description);
	if(report.zipPath.isEmpty() || !report.error.isEmpty())
	{
		QMessageBox::critical(parent, QObject::tr("Report"), QObject::tr("The report could not be made: %1").arg(report.error));
		return;
	}
	QString problem;
	if(send(report, problem))
	{
		QMessageBox::information(parent, QObject::tr("Report sent"), QObject::tr("Your report was sent to the developers. Thank you."));
		return;
	}
	// no upload (or it failed): the zip is saved, its folder opens, and the repository's new-issue page opens with the summary filled in
	QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(report.zipPath).absolutePath()));
	// the issue form's own fields (.github/ISSUE_TEMPLATE/bug_report.yml, by their ids) are filled in
	QUrl issue(Helper::dmbHomepage() + "/issues/new");
	QUrlQuery query;
	query.addQueryItem("template", "bug_report.yml");
	query.addQueryItem("title", "[Bug] " + QObject::tr("Report from %1").arg(QString::fromUtf8(DMB_RELEASE_TEXT)));
	query.addQueryItem("dmb-version", QString::fromUtf8(DMB_RELEASE_TEXT));
	query.addQueryItem("os-version", QSysInfo::prettyProductName());
	if(!report.crash.isEmpty())
		query.addQueryItem("log-excerpt", report.crash.left(1500));
	if(!report.description.isEmpty())
		query.addQueryItem("summary", report.description.left(1500));
	query.addQueryItem("extra", QObject::tr("Report file: %1 (drag it into this box)").arg(QFileInfo(report.zipPath).fileName()));
	issue.setQuery(query);
	QDesktopServices::openUrl(issue);
	QMessageBox::information(parent, QObject::tr("Report saved"),
		QObject::tr("The report is saved here:\n%1\n\nIts folder is open, and so is a page on GitHub with the details filled in. Drag the zip file into that page's box and press Submit (this needs a GitHub account).%2")
			.arg(report.zipPath, problem.isEmpty() ? QString() : QObject::tr("\n\n(The automatic upload did not work: %1)").arg(problem)));
}
}
