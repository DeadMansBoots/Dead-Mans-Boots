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

#include <QApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QCloseEvent>
#include <QPointer>
#include <QRandomGenerator>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QWidget>

namespace
{
/// DMB: a window-modal note shown while a report is made and sent, which the player cannot close meanwhile; hide() ends it
class WaitNote : public QDialog
{
public:
	using QDialog::QDialog;

protected:
	void reject() override {}
	void closeEvent(QCloseEvent * event) override { event->ignore(); }
};

constexpr qint64 LOG_TAIL_BYTES = 4 * 1024 * 1024;
constexpr qint64 PLAY_LOG_TAIL_BYTES = 6 * 1024 * 1024;

/// DMB: through the wide path on Windows; path::string() there is the ANSI code page, which QString read as UTF-8, so a user
/// folder with letters outside it (Jürgen, a Cyrillic or CJK name) named a folder that does not exist
QString userFolder(const boost::filesystem::path & path)
{
	return pathToQString(path);
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
	// DMB: a JSON string to its real end, past escaped quotes; "[^"]*" stopped at \" and left the rest of a name, and [^\]]* stopped at
	// a "]" inside a name and left the names after it
	const QString string = "\"(?:[^\"\\\\]|\\\\.)*\"";
	text.replace(QRegularExpression("(\"remoteHostname\"\\s*:\\s*)" + string), "\\1\"<removed>\"");
	text.replace(QRegularExpression("(\"playerName\"\\s*:\\s*)" + string), "\\1\"<removed>\"");
	text.replace(QRegularExpression("(\"multiPlayerNames\"\\s*:\\s*)\\[\\s*(?:" + string + "\\s*,?\\s*)*\\]"), "\\1[]");
	// other text a player types or pastes: the autosave/save prefix, the host the game's own server binds to, an extra mod repository address (it can carry a key)
	for(const char * key : {"savePrefix", "localHostname", "extraRepositoryURL"})
		text.replace(QRegularExpression(QString("(\"%1\"\\s*:\\s*)").arg(key) + string), "\\1\"<removed>\"");
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
			// DMB: the Users folder in each case too, as a dump holds the Users folder in more than one case
			for(const QString & variant : {name, name.toLower(), name.toUpper()})
				for(const QString & users : {QString("Users"), QString("users"), QString("USERS")})
				for(const QString & separator : {QString("\\"), QString("/")})
				{
					const QString whole = users + separator + variant + separator;
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
		QFile client(userFolder(VCMIDirs::get().clientPath()));
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

quint32 crc32Of(const QByteArray & data)
{
	static quint32 table[256];
	static bool made = false;
	if(!made)
	{
		for(quint32 n = 0; n < 256; ++n)
		{
			quint32 c = n;
			for(int k = 0; k < 8; ++k)
				c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
			table[n] = c;
		}
		made = true;
	}
	quint32 crc = 0xFFFFFFFFu;
	for(const char byte : data)
		crc = table[(crc ^ static_cast<uchar>(byte)) & 0xFF] ^ (crc >> 8);
	return crc ^ 0xFFFFFFFFu;
}

/// `data` as a gzip file (Sentry takes a compressed minidump as gzip, zstd, bzip2 or xz): the deflate data of Qt's own zlib stream (qCompress: four bytes of length, a two byte
/// zlib header, the data, four bytes of checksum) between a gzip header and the gzip trailer (the data's CRC-32 and length)
QByteArray gzipped(const QByteArray & data)
{
	const QByteArray zlibStream = qCompress(data, 6);
	QByteArray out(10, '\0');
	out[0] = static_cast<char>(0x1f);
	out[1] = static_cast<char>(0x8b);
	out[2] = 8;
	out[9] = static_cast<char>(0xff);
	out.append(zlibStream.constData() + 6, zlibStream.size() - 10);
	const auto littleEndian = [&out](quint32 value)
	{
		for(int i = 0; i < 4; ++i)
			out.append(static_cast<char>((value >> (8 * i)) & 0xFF));
	};
	littleEndian(crc32Of(data));
	littleEndian(static_cast<quint32>(data.size()));
	return out;
}
}

namespace BugReport
{
Report build(const QString & why, const QString & description, bool includeDump, const QString & dumpPath)
{
	Report report;
	try
	{
		const QString logs = userFolder(VCMIDirs::get().userLogsPath());
		const QString config = userFolder(VCMIDirs::get().userConfigPath());
		const QString data = userFolder(VCMIDirs::get().userDataPath());

		QDir().mkpath(data + "/Reports");
		const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
		const QString name = QString("DMB-report-%1-%2.zip").arg(stamp).arg(QString::number(QRandomGenerator::global()->generate() & 0xFFFFFF, 16));
		report.zipPath = data + "/Reports/" + name;

		std::shared_ptr<CIOApi> api = std::make_shared<CDefaultIOApi>();
		CZipSaver saver(api, qstringToPath(report.zipPath));
		// a text file goes into the zip and into the list of parts that a one-by-one upload sends
		const auto add = [&](const std::string & fileName, const QByteArray & bytes)
		{
			addToZip(saver, fileName, bytes);
			report.parts.append({QString::fromStdString(fileName), bytes});
		};

		QString clientLog;
		for(const QFileInfo & file : QDir(logs).entryInfoList({"*.txt"}, QDir::Files, QDir::Name))
		{
			QByteArray text = tailOf(file.absoluteFilePath(), LOG_TAIL_BYTES);
			if(file.fileName() == "VCMI_Client_log.txt")
				clientLog = QString::fromUtf8(text);
			add("logs/" + file.fileName().toStdString(), redacted(text));
		}
		for(const QString & jsonName : {"settings.json", "modSettings.json"})
		{
			const QByteArray text = tailOf(config + "/" + jsonName, 2 * 1024 * 1024);
			if(!text.isEmpty())
				add("config/" + jsonName.toStdString(), scrubbedSettings(text));
		}
		// the newest play log (what was clicked and what the game answered), its end
		QFileInfoList playLogs = QDir(data + "/PlayLogs").entryInfoList({"*.jsonl"}, QDir::Files, QDir::Time);
		if(!playLogs.isEmpty())
			add("playlog/" + playLogs.first().fileName().toStdString(), redacted(tailOf(playLogs.first().absoluteFilePath(), PLAY_LOG_TAIL_BYTES)));

		if(!description.trimmed().isEmpty())
			add("description.txt", redacted(description.toUtf8()));
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
				add("mods-listing.txt", redacted(listing.toUtf8()));
		}
		// the newest crash dump of the game, when it is from the last day (a dump holds memory: the reports go to a private repository); when the whole
		// report comes out over 40 MB the report is made again without it (the upload takes it as one piece)
		if(includeDump)
		{
			// DMB: the crash window's own dump, where it names one; the newest in the folder can be another crash's
			const QFileInfoList dumps = !dumpPath.isEmpty() ? QFileInfoList{QFileInfo(dumpPath)}
				: QDir(logs).entryInfoList({"*_crash_*.dmp"}, QDir::Files, QDir::Time);
			if(!dumps.isEmpty() && (!dumpPath.isEmpty() || dumps.first().lastModified().secsTo(QDateTime::currentDateTime()) < 24 * 3600))
			{
				QFile dump(dumps.first().absoluteFilePath());
				if(dumps.first().size() <= 120 * 1024 * 1024 && dump.open(QIODevice::ReadOnly))
					{
						QByteArray bytes = dump.readAll();
						scrubDump(bytes);
						addToZip(saver, "crash/" + dumps.first().fileName().toStdString(), bytes);
						report.dump = bytes;
						report.dumpName = dumps.first().fileName();
					}
			}
		}
		const QString crash = crashSummary(clientLog);
		if(!crash.isEmpty())
			add("crash-summary.txt", redacted(crash.toUtf8()));
		const QString info = systemInfo(why);
		add("report-info.txt", redacted(info.toUtf8()));

		report.summary = info;
		// DMB: the text goes out as the report's summary too, so it is redacted like the files (an event report names players and a reason line)
		report.description = QString::fromUtf8(redacted(description.toUtf8()));
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
	QFile file(userFolder(VCMIDirs::get().userConfigPath()) + "/settings.json");
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

/// Sentry's minidump endpoint (an address with "/minidump/" in it, https://docs.sentry.io/platforms/native/guides/minidumps/): the crash dump in the field
/// upload_file_minidump, gzip compressed, and every scrubbed text file as a file part of its own (each becomes an event attachment, with the name and type given here;
/// Sentry scrubs nothing in an attachment, so the scrub is ours), and a "sentry" field with the release and a fingerprint of this report alone, so that each report is
/// an issue of its own and can be deleted on its own. One request must stay under 40 MB: the dump is left out whole when it would not fit, and then nothing is sent (the
/// endpoint takes no report without a dump) and the player gets the saved zip as for any failed upload.
static bool sendToSentry(const Report & report, const QUrl & endpoint, QNetworkAccessManager & manager, QString & message)
{
	constexpr qint64 REQUEST_LIMIT = 38LL * 1024 * 1024;
	if(report.dump.isEmpty())
	{
		message = "no crash file in this report: nothing is sent to the crash service";
		return false;
	}
	const QByteArray dump = gzipped(report.dump);
	qint64 total = dump.size();
	for(const auto & part : report.parts)
		total += part.second.size();
	if(total > REQUEST_LIMIT)
	{
		message = "the report is too large for the crash service";
		return false;
	}

	auto * multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
	const auto addFile = [multi](const QString & field, const QString & fileName, const QByteArray & mime, const QByteArray & bytes)
	{
		QHttpPart part;
		part.setHeader(QNetworkRequest::ContentDispositionHeader, QString("form-data; name=\"%1\"; filename=\"%2\"").arg(field, fileName));
		part.setHeader(QNetworkRequest::ContentTypeHeader, mime);
		part.setBody(bytes);
		multi->append(part);
	};
	addFile("upload_file_minidump", report.dumpName + ".gz", "application/gzip", dump);
	for(const auto & part : report.parts)
	{
		QString flat = part.first;
		flat.replace('/', '_');
		addFile(flat, flat, "text/plain", part.second);
	}
	QJsonObject sentry;
	sentry["release"] = QString::fromUtf8(DMB_RELEASE_TEXT);
	sentry["environment"] = "beta";
	sentry["fingerprint"] = QJsonArray{QFileInfo(report.zipPath).completeBaseName()};
	QHttpPart field;
	field.setHeader(QNetworkRequest::ContentDispositionHeader, "form-data; name=\"sentry\"");
	field.setBody(QJsonDocument(sentry).toJson(QJsonDocument::Compact));
	multi->append(field);

	QNetworkRequest request{endpoint};
	request.setRawHeader("User-Agent", "DeadMansBoots-Launcher");
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	QNetworkReply * reply = manager.post(request, multi);
	multi->setParent(reply);
	QEventLoop loop;
	QTimer timeout;
	timeout.setSingleShot(true);
	QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	timeout.start(180 * 1000);
	loop.exec();
	const bool done = reply->isFinished() && reply->error() == QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
	message = done ? QString() : (reply->isFinished() ? reply->errorString() : QString("the upload timed out"));
	reply->deleteLater();
	return done;
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
	if(endpointUrl.path().contains("/minidump"))
		return sendToSentry(report, endpointUrl, manager, message);

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
	// a stored "true" counts only once the player has answered the launcher's question (crashReportsAsked): a file written by an earlier beta, whose default was on, stays off until then (K, October 18th)
	return launcherSettings()["sendCrashReports"].toBool(false) && launcherSettings()["crashReportsAsked"].toBool(false);
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
	// DMB: the upload waits in nested event loops for up to minutes. A window-modal note keeps the launcher from being closed
	// under it or asked for a second report meanwhile, and the boxes after it take the launcher only while it still exists
	const QPointer<QWidget> owner(parent);
	// the note is the launcher's child, so it is made on the heap and followed by a QPointer: if the launcher goes during the wait, it takes the
	// note with it, and nothing here touches or deletes it again
	const QPointer<WaitNote> waiting = new WaitNote(parent, Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
	waiting->setWindowTitle(QObject::tr("Report a bug"));
	waiting->setWindowModality(Qt::WindowModal);
	auto * waitingLayout = new QVBoxLayout(waiting.data());
	waitingLayout->addWidget(new QLabel(QObject::tr("Making the report and sending it to the developers...")));
	waiting->show();
	QApplication::processEvents();
	const Report report = build(why, description);
	QString problem;
	const bool sent = report.zipPath.isEmpty() || !report.error.isEmpty() ? false : send(report, problem);
	if(waiting)
	{
		waiting->hide();
		delete waiting.data();
	}
	if(report.zipPath.isEmpty() || !report.error.isEmpty())
	{
		QMessageBox::critical(owner.data(), QObject::tr("Report"), QObject::tr("The report could not be made: %1").arg(report.error));
		return;
	}
	if(sent)
	{
		QMessageBox::information(owner.data(), QObject::tr("Report sent"), QObject::tr("Your report was sent to the developers. Thank you."));
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
	QMessageBox::information(owner.data(), QObject::tr("Report saved"),
		QObject::tr("The report is saved here:\n%1\n\nIts folder is open, and so is a page on GitHub with the details filled in. Drag the zip file into that page's box and press the \"Submit New Issue\" button (this needs a GitHub account).%2")
			.arg(report.zipPath, problem.isEmpty() ? QString() : QObject::tr("\n\n(The automatic upload did not work: %1)").arg(problem)));
}

void runCrashWindow(const QString & dumpPath)
{
	// K, October 13th: "this is not the easy, one-click crash reporter I expected to see triggered on a crash"
	QDialog dialog;
	dialog.setWindowTitle(QObject::tr("Dead Man's Boots crashed"));
	dialog.setMinimumWidth(520);
	auto * layout = new QVBoxLayout(&dialog);
	auto * what = new QLabel(QObject::tr("<b>Dead Man's Boots has crashed.</b> Sorry about that."));
	auto * detail = new QLabel;
	auto * sends = new QLabel(QObject::tr("The report holds the crash file (a small snapshot of what the game was doing), the game's logs and its settings. Your Windows user name and computer name are removed first, and nothing else leaves your computer."));
	auto * stays = new QLabel(QObject::tr("The crash file stays on your computer: %1").arg(QDir::toNativeSeparators(dumpPath)));
	for(QLabel * label : {detail, sends, stays})
		label->setWordWrap(true);
	detail->hide();
	auto * buttons = new QHBoxLayout;
	auto * sendButton = new QPushButton(QObject::tr("Send report"));
	auto * dont = new QPushButton(QObject::tr("Don't send"));
	sendButton->setDefault(true);
	buttons->addStretch(1);
	buttons->addWidget(sendButton);
	buttons->addWidget(dont);
	for(QWidget * widget : QList<QWidget *>{what, detail, sends, stays})
		layout->addWidget(widget);
	layout->addLayout(buttons);

	const auto finish = [&]()
	{
		sendButton->hide();
		dont->setText(QObject::tr("Close"));
		dont->setDefault(true);
	};
	const auto sendNow = [&](bool automatic)
	{
		sendButton->setEnabled(false);
		dont->setEnabled(false);
		detail->setText(automatic ? QObject::tr("Sending a report by itself (the \"General\" button on the \"Settings\" tab switches this off)...") : QObject::tr("Sending..."));
		detail->show();
		QApplication::processEvents();
		// DMB (Meg, October 24th: "the crash reporter crashed too"): nothing the building or the sending throws may take this window down; what went wrong is said,
		// and the saved zip (if the report was made) is named
		Report report;
		QString problem;
		bool sent = false;
		try
		{
			report = build("the game crashed", {}, true, dumpPath);
			sent = report.error.isEmpty() && send(report, problem);
		}
		catch(const std::exception & e)
		{
			problem = QString::fromUtf8(e.what());
			if(report.error.isEmpty())
				report.error = problem;
		}
		catch(...)
		{
			problem = "an unknown error";
			if(report.error.isEmpty())
				report.error = problem;
		}
		// the crash text opens with the log's lines ("[2026-Oct-14 08:42:28.739218] ERROR [39436] global - Disaster happened."): the exception line without its stamp says more
		// (K, October 14th: "Exception EXCEPTION_ACCESS_VIOLATION (0xc0000005) at ? (Attempt to read from 0x0000000000000000)")
		static const QRegularExpression stamp("^\\[[^\\]]+\\]\\s+\\w+\\s+\\[[^\\]]+\\]\\s+\\w+\\s+-\\s+");
		const QStringList crashLines = report.crash.split('\n');
		QString cause;
		for(int i = 0; i < crashLines.size() && i < 12; i++)
		{
			QString line = crashLines[i];
			line.remove(stamp);
			line = line.trimmed();
			if(line.startsWith("Exception "))
			{
				cause = line;
				if(i + 1 < crashLines.size())
				{
					QString next = crashLines[i + 1];
					next.remove(stamp);
					next = next.trimmed();
					if(next.startsWith("Attempt to "))
						cause += " (" + next + ")";
				}
				break;
			}
		}
		if(cause.isEmpty())
			cause = report.crash.section('\n', 0, 0).remove(stamp).trimmed();
		if(cause.size() > 160)
			cause = cause.left(160) + "...";
		QString text = sent ? (automatic ? QObject::tr("A report was sent to the developers by itself. Thank you.") : QObject::tr("The report was sent. Thank you."))
			: QObject::tr("The report could not be sent (%1).").arg(report.error.isEmpty() ? problem : report.error);
		if(!sent && !report.zipPath.isEmpty())
			text += QObject::tr(" It is saved here: %1").arg(QDir::toNativeSeparators(report.zipPath));
		else if(!sent)
			text += QObject::tr(" The game's logs and the crash file are in this folder: %1").arg(QDir::toNativeSeparators(QFileInfo(dumpPath).absolutePath()));
		if(report.error.isEmpty() && report.dump.isEmpty())
			text += QObject::tr(" The crash file is not in the report (it could not be read, or it made the report too large to send); the logs and settings are.");
		if(!cause.isEmpty())
			text = QObject::tr("What happened: %1").arg(cause) + "\n\n" + text;
		detail->setText(text);
		dont->setEnabled(true);
		finish();
		// the window grows to the text it shows now (after a failed send its last lines, which name the saved zip, were cut off)
		dialog.adjustSize();
	};
	QObject::connect(sendButton, &QPushButton::clicked, &dialog, [&]() { sendNow(false); });
	QObject::connect(dont, &QPushButton::clicked, &dialog, &QDialog::accept);
	if(crashReportsEnabled())
		QTimer::singleShot(0, &dialog, [&]() { sendNow(true); });
	dialog.exec();
}
}
