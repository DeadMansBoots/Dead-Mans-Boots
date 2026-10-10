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

/// the Windows user name and the computer name replaced, whatever the path style or case; a name shorter than three letters is left, since
/// replacing it would damage ordinary words
QByteArray redacted(const QByteArray & data)
{
	QString text = QString::fromUtf8(data);
	const auto replaceName = [&text](const QString & name, const QString & with)
	{
		if(name.size() >= 3)
			text.replace(name, with, Qt::CaseInsensitive);
	};
	replaceName(qEnvironmentVariable("USERNAME"), "<user>");
	replaceName(QDir::home().dirName(), "<user>");
	replaceName(qEnvironmentVariable("COMPUTERNAME"), "<computer>");
	return text.toUtf8();
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
Report build(const QString & why, const QString & description)
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
				addToZip(saver, "config/" + jsonName.toStdString(), redacted(text));
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
				addToZip(saver, "mods-listing.txt", listing.toUtf8());
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
	return report;
}

bool send(const Report & report, QString & message)
{
#ifdef DMB_REPORT_TOKEN
	const QString REPORTS_REPOSITORY = "DeadMansBoots/dmb-reports";
	QFile file(report.zipPath);
	if(!file.open(QIODevice::ReadOnly))
	{
		message = "the report could not be read";
		return false;
	}
	const QByteArray zip = file.readAll();
	QJsonObject body;
	body["message"] = "Report " + QFileInfo(report.zipPath).fileName();
	body["content"] = QString::fromLatin1(zip.toBase64());
	const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

	QNetworkAccessManager manager;
	QNetworkRequest request{QUrl(QString("https://api.github.com/repos/%1/contents/reports/%2").arg(REPORTS_REPOSITORY, QFileInfo(report.zipPath).fileName()))};
	request.setRawHeader("Authorization", QByteArray("Bearer ") + DMB_REPORT_TOKEN);
	request.setRawHeader("Accept", "application/vnd.github+json");
	request.setRawHeader("User-Agent", "DeadMansBoots-Launcher");
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	QNetworkReply * reply = manager.put(request, payload);
	QEventLoop loop;
	QTimer timeout;
	timeout.setSingleShot(true);
	QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	timeout.start(180 * 1000);
	loop.exec();
	const bool done = reply->isFinished() && reply->error() == QNetworkReply::NoError;
	message = done ? QString() : (reply->isFinished() ? reply->errorString() : QString("the upload timed out"));
	reply->deleteLater();
	return done;
#else
	message = "this build has no upload set up";
	return false;
#endif
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
