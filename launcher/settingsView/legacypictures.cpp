/*
 * legacypictures.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "legacypictures.h"

#include <QCryptographicHash>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>

#include "../../lib/VCMIDirs.h"
#include "../../lib/filesystem/MenuBackgrounds.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{
const char * WIKI_API = "https://heroes.thelazy.net/api.php";
const char * AGENT = "DeadMansBoots-Launcher/1.0 (legacy menu pictures and loading screens, one request at a time; Dead Man's Boots project)";
// the portable Windows package of Real-ESRGAN's own release (the program MIT, the models BSD 3-clause), 45,474,481 bytes
const char * SHARPENER_URL = "https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-windows.zip";
const char * SHARPENER_SHA256 = "abc02804e17982a3be33675e4d471e91ea374e65b70167abc09e31acb412802d";
const char * SHARPENER_MODEL = "realesrgan-x4plus";
// the backdrop the sharpener sees: as dark as the buttons' own outline, so what it blurs into the edge is outline and not a pale haze
const QColor FLAT_GREY(22, 18, 20);
const int MARGIN = 8; // pixels round a button for the sharpener

const QStringList BUTTONS = {"MMENUNG", "MMENULG", "MMENUHS", "MMENUCR", "MMENUQT", "GTSINGL", "GTMULTI", "GTCAMPN", "GTTUTOR", "GTBACK"};
const char * BUTTON_RELEASE = "h3bitmap";

QString userFolder(const QString & name)
{
	return pathToQString(VCMIDirs::get().userDataPath() / name.toStdString());
}

void hideWindow(QProcess & process)
{
#ifdef Q_OS_WIN
	process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments * args) { args->flags |= CREATE_NO_WINDOW; });
#else
	Q_UNUSED(process);
#endif
}

QString sha(const QByteArray & data, QCryptographicHash::Algorithm algorithm)
{
	return QString::fromLatin1(QCryptographicHash::hash(data, algorithm).toHex());
}

QString fileSha(const QString & path, QCryptographicHash::Algorithm algorithm)
{
	QFile file(path);
	if(!file.open(QIODevice::ReadOnly))
		return {};
	return sha(file.readAll(), algorithm);
}

/// a picture as 8 bit RGB, the way the sharpener reads it
QImage rgbPicture(const QString & path)
{
	QImage image(path);
	return image.isNull() ? image : image.convertToFormat(QImage::Format_RGB888);
}
}

LegacyPicturesWorker::LegacyPicturesWorker(QObject * parent)
	: QThread(parent)
{
}

QString LegacyPicturesWorker::baseFolder()
{
	return userFolder("LegacyPictures");
}

QString LegacyPicturesWorker::slugOf(const QString & name)
{
	QString slug = name.toLower();
	for(QChar & c : slug)
		if(!c.isLetterOrNumber() || c.unicode() > 127)
			c = '-';
	return slug;
}

QVector<LegacyPicturesWorker::Picture> LegacyPicturesWorker::selection()
{
	// the releases the game holds a menu or loading screen of (always), and the mods whose own pictures these are (only where installed):
	// the lists show what the player has (K, October 4th: "why do we have wake of gods? that's not in my mods list")
	QVector<Picture> all;
	const auto add = [&all](const char * wiki, const char * title, bool loading, const char * mod = "")
	{
		Picture p;
		p.wikiName = wiki;
		p.title = title;
		p.loading = loading;
		p.needsMod = mod;
		all.push_back(p);
	};
	add("Restoration of erathia loading screen.png", "Restoration of Erathia loading screen", true);
	add("AB Loading Screen.png", "Armageddon's Blade loading screen", true);
	add("AB Mutare.png", "Armageddon's Blade Mutare", true);
	add("AB Phoenix.png", "Armageddon's Blade Phoenix", true);
	add("Shadow of death loading.png", "Shadow of Death loading screen", true);
	add("SoD Crag Hack Yog.png", "Shadow of Death Crag Hack and Yog", true);
	add("SoD Gelu Gem.png", "Shadow of Death Gelu and Gem", true);
	add("Complete Loading Screen.png", "Heroes III Complete loading screen", true);
	add("Complete Crag Hack Yog.png", "Heroes III Complete Crag Hack and Yog", true);
	add("Complete Mutare.png", "Heroes III Complete Mutare", true);
	add("HotA Loading Screen.png", "Horn of the Abyss loading screen", true, "hota");
	add("Restoration of erathia menu no buttons.png", "Restoration of Erathia", false);
	add("SoD Lich.png", "Shadow of Death", false);

	QVector<Picture> chosen;
	for(const auto & p : all)
		if(p.needsMod.isEmpty() || !MenuBackgrounds::modFolder(p.needsMod.toStdString()).empty())
			chosen.push_back(p);
	return chosen;
}

QPair<int, int> LegacyPicturesWorker::plannedCounts()
{
	int loading = 0, menus = 0;
	for(const auto & p : selection())
		(p.loading ? loading : menus) += 1;
	return {loading, menus};
}

bool LegacyPicturesWorker::cancelled() const
{
	return isInterruptionRequested();
}

namespace
{
// the steps of a run, with what share of the whole bar each one has (the sharpener fetch only where it is needed)
struct Stage
{
	const char * title;
	int weight;
};
const Stage STAGES[] = {{"Asking the wiki", 3}, {"Downloading the pictures", 17}, {"Fetching the sharpener (once)", 20}, {"Sharpening the pictures 4x", 25}, {"Sharpening the menu buttons 4x", 30}, {"Finishing", 5}};
const int STAGE_COUNT = 6;
}

/// "33.0 of 43.4 MB at 2.4 MB/s": what has come down of how much, and how fast (the wording of every download step); a size the server
/// did not give is said so
static QString transferText(qint64 got, qint64 all, double bytesPerSecond)
{
	const bool inMegabytes = std::max<qint64>(got, all) >= 1048576;
	const auto number = [inMegabytes](qint64 bytes)
	{
		return inMegabytes ? QString::number(bytes / 1048576.0, 'f', 1) : QString::number(bytes / 1024.0, 'f', 0);
	};
	const QString unit = inMegabytes ? "MB" : "KB";
	QString text = all > 0 ? QString("%1 of %2 %3").arg(number(got), number(all), unit) : QString("%1 %2 received (total size unknown)").arg(number(got), unit);
	if(bytesPerSecond > 0)
		text += bytesPerSecond >= 1048576.0 ? QString(" at %1 MB/s").arg(bytesPerSecond / 1048576.0, 0, 'f', 1) : QString(" at %1 KB/s").arg(bytesPerSecond / 1024.0, 0, 'f', 0);
	return text;
}

void LegacyPicturesWorker::report(int stage, double fraction, const QString & detail)
{
	int before = 0, total = 0;
	for(int i = 0; i < STAGE_COUNT; ++i)
	{
		total += STAGES[i].weight;
		if(i < stage)
			before += STAGES[i].weight;
	}
	fraction = std::clamp(fraction, 0.0, 1.0);
	const int permille = static_cast<int>(1000.0 * (before + STAGES[stage].weight * fraction) / total);
	emit overall(permille, stage + 1, STAGE_COUNT, tr(STAGES[stage].title), detail);
}

QByteArray LegacyPicturesWorker::fetch(const QUrl & url, bool wiki, int timeoutMs, std::function<void(qint64, qint64, double)> bytes)
{
	if(wiki)
		msleep(700); // one request at a time with a pause: polite to the wiki
	QNetworkRequest request(url);
	request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(AGENT));
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	QNetworkReply * reply = network->get(request);
	if(bytes)
	{
		// the rate is the last second or so of the transfer (a sample every 400 ms or more, blended with the one before), not the
		// average since the start, so a stall shows
		struct Rate
		{
			QElapsedTimer clock;
			qint64 lastMs = 0;
			qint64 lastGot = 0;
			double perSecond = 0;
		};
		auto rate = std::make_shared<Rate>();
		rate->clock.start();
		QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [bytes, rate](qint64 got, qint64 all)
		{
			const qint64 now = rate->clock.elapsed();
			if(now - rate->lastMs >= 400)
			{
				const double sample = (got - rate->lastGot) * 1000.0 / std::max<qint64>(1, now - rate->lastMs);
				rate->perSecond = rate->perSecond > 0 ? (rate->perSecond + sample) / 2.0 : sample;
				rate->lastMs = now;
				rate->lastGot = got;
			}
			bytes(got, all, rate->perSecond);
		});
	}
	QEventLoop loop;
	QTimer timer;
	timer.setSingleShot(true);
	QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
	QTimer stopCheck;
	QObject::connect(&stopCheck, &QTimer::timeout, &loop, [&]() { if(cancelled()) loop.quit(); });
	timer.start(timeoutMs);
	stopCheck.start(300);
	loop.exec();
	QByteArray data;
	if(reply->isFinished() && reply->error() == QNetworkReply::NoError)
		data = reply->readAll();
	else if(reply->isFinished())
		failure = QString("%1: %2").arg(url.host(), reply->errorString());
	else
	{
		failure = cancelled() ? QString("stopped") : QString("%1 did not answer in time").arg(url.host());
		reply->abort();
	}
	reply->deleteLater();
	return data;
}

bool LegacyPicturesWorker::queryWiki(QVector<Picture> & pictures)
{
	// the address and the hash of each picture, from the wiki's own API, fifty to a request
	for(int from = 0; from < pictures.size(); from += 40)
	{
		QStringList titles;
		const int to = std::min<int>(pictures.size(), from + 40);
		for(int i = from; i < to; ++i)
			titles << ("File:" + pictures[i].wikiName);
		QUrl url(WIKI_API);
		QUrlQuery query;
		query.addQueryItem("action", "query");
		query.addQueryItem("format", "json");
		query.addQueryItem("prop", "imageinfo");
		query.addQueryItem("iiprop", "url|size|sha1");
		query.addQueryItem("titles", titles.join('|'));
		url.setQuery(query);
		const QByteArray data = fetch(url, true);
		if(data.isEmpty())
			return false;
		const QJsonObject pagesObject = QJsonDocument::fromJson(data).object()["query"].toObject()["pages"].toObject();
		for(auto it = pagesObject.begin(); it != pagesObject.end(); ++it)
		{
			const QJsonObject page = it.value().toObject();
			const QString title = page["title"].toString().mid(5); // without "File:"
			const QJsonArray info = page["imageinfo"].toArray();
			if(info.isEmpty())
				continue;
			const QString wanted = QString(title).replace(' ', '_');
			for(int i = from; i < to; ++i)
				if(QString(pictures[i].wikiName).replace(' ', '_').compare(wanted, Qt::CaseInsensitive) == 0)
				{
					pictures[i].url = info[0].toObject()["url"].toString();
					pictures[i].sha1 = info[0].toObject()["sha1"].toString();
					pictures[i].bytes = static_cast<qint64>(info[0].toObject()["size"].toDouble());
				}
		}
	}
	for(const auto & p : pictures)
		if(p.url.isEmpty())
		{
			failure = QString("the wiki has no picture named %1 any more").arg(p.wikiName);
			return false;
		}
	return true;
}

bool LegacyPicturesWorker::downloadOriginals(QVector<Picture> & pictures)
{
	const QString folder = baseFolder() + "/Original";
	QDir().mkpath(folder);
	int done = 0;
	qint64 allBytes = 0, doneBytes = 0;
	for(const auto & p : pictures)
		allBytes += p.bytes;
	for(auto & p : pictures)
	{
		if(cancelled())
		{
			failure = "stopped";
			return false;
		}
		// the line of this picture: its own bytes and rate, and the pictures so far of all of them
		const auto line = [&](qint64 got, qint64 all, double rate)
		{
			QString text = tr("Picture %1 of %2: %3, %4").arg(done + 1).arg(pictures.size()).arg(p.title, transferText(got, all, rate));
			if(allBytes > 0)
				text += tr(". All pictures: %1.").arg(transferText(doneBytes + got, allBytes, 0));
			const double fraction = allBytes > 0 ? static_cast<double>(doneBytes + got) / allBytes : (done + (all > 0 ? static_cast<double>(got) / all : 0.0)) / pictures.size();
			report(1, fraction, text);
		};
		if(allBytes > 0)
			report(1, static_cast<double>(doneBytes) / allBytes, tr("Picture %1 of %2: %3. All pictures: %4.").arg(done + 1).arg(pictures.size()).arg(p.title, transferText(doneBytes, allBytes, 0)));
		else
			report(1, static_cast<double>(done) / pictures.size(), tr("Picture %1 of %2: %3").arg(done + 1).arg(pictures.size()).arg(p.title));
		p.originalPath = folder + "/" + p.wikiName;
		if(!(QFile::exists(p.originalPath) && fileSha(p.originalPath, QCryptographicHash::Sha1) == p.sha1))
		{
			bool good = false;
			for(int attempt = 0; attempt < 3 && !good && !cancelled(); ++attempt)
			{
				const QByteArray data = fetch(QUrl(p.url), true, 120000, line);
				if(!data.isEmpty() && sha(data, QCryptographicHash::Sha1) == p.sha1)
				{
					QFile file(p.originalPath + ".part");
					if(file.open(QIODevice::WriteOnly) && file.write(data) == data.size())
					{
						file.close();
						QFile::remove(p.originalPath);
						good = QFile::rename(p.originalPath + ".part", p.originalPath);
					}
				}
				else if(!data.isEmpty())
					failure = QString("%1 came down damaged (its hash is not the wiki's)").arg(p.wikiName);
			}
			if(!good)
			{
				if(failure.isEmpty())
					failure = QString("%1 could not be downloaded").arg(p.wikiName);
				return false;
			}
		}
		++done;
		doneBytes += p.bytes;
	}
	report(1, 1.0, tr("%1 pictures downloaded and checked").arg(pictures.size()));
	return true;
}

bool LegacyPicturesWorker::ensureSharpener(QString & exe)
{
#ifdef Q_OS_WIN
	const QString tools = baseFolder() + "/Tools/realesrgan";
	exe = tools + "/realesrgan-ncnn-vulkan.exe";
	if(QFile::exists(exe) && QFile::exists(tools + "/models/" + SHARPENER_MODEL + ".bin"))
		return true;
	report(2, 0.0, tr("Starting the download (about 45 MB)"));
	QDir().mkpath(tools);
	const QByteArray zip = fetch(QUrl(QString::fromLatin1(SHARPENER_URL)), false, 20 * 60 * 1000, [this](qint64 got, qint64 all, double rate)
	{
		// a pinned file of a known size: where the server gives none, that size stands
		const qint64 total = all > 0 ? all : 45474481;
		report(2, static_cast<double>(got) / total * 0.9, tr("Downloading the sharpener: %1").arg(transferText(got, total, rate)));
	});
	if(zip.isEmpty() || sha(zip, QCryptographicHash::Sha256) != QString::fromLatin1(SHARPENER_SHA256))
	{
		failure = zip.isEmpty() ? failure : QString("the sharpener download is not the file expected (its hash differs)");
		return false;
	}
	const QString zipPath = tools + "/package.zip";
	QFile file(zipPath);
	if(!file.open(QIODevice::WriteOnly) || file.write(zip) != zip.size())
		return false;
	file.close();
	report(2, 0.92, tr("Unpacking the sharpener"));
	QProcess tar;
	hideWindow(tar);
	tar.start("tar", {"-xf", QDir::toNativeSeparators(zipPath), "-C", QDir::toNativeSeparators(tools)});
	if(!tar.waitForFinished(300000) || tar.exitCode() != 0)
	{
		failure = "the sharpener package could not be unpacked";
		return false;
	}
	QFile::remove(zipPath);
	report(2, 1.0, tr("The sharpener is ready"));
	return QFile::exists(exe);
#else
	Q_UNUSED(exe);
	return false;
#endif
}

bool LegacyPicturesWorker::sharpen(const QString & exe, const QString & inFolder, const QString & outFolder, int expected, int stage, const QString & what, bool & usedProgram)
{
	QDir().mkpath(outFolder);
	// the program on a whole folder, which loads its model once; where it cannot run (no Vulkan card), the pictures are enlarged smoothly
	bool ranProgram = false;
	if(!exe.isEmpty() && QFile::exists(exe))
	{
		QProcess process;
		hideWindow(process);
		process.setWorkingDirectory(QFileInfo(exe).absolutePath());
		process.start(exe, {"-i", QDir::toNativeSeparators(inFolder), "-o", QDir::toNativeSeparators(outFolder), "-n", SHARPENER_MODEL, "-s", "4", "-f", "png"});
		QElapsedTimer clock;
		clock.start();
		while(process.state() != QProcess::NotRunning)
		{
			process.waitForFinished(700);
			{
				const int made = QDir(outFolder).entryList({"*.png"}).size();
				report(stage, static_cast<double>(made) / std::max(1, expected), tr("%1: %2 of %3 done").arg(what).arg(made).arg(expected));
			}
			if(cancelled() || clock.elapsed() > 45 * 60 * 1000)
			{
				process.kill();
				process.waitForFinished(3000);
				failure = cancelled() ? "stopped" : "the sharpener took too long";
				return false;
			}
		}
		ranProgram = process.exitCode() == 0 && QDir(outFolder).entryList({"*.png"}).size() >= expected;
	}
	usedProgram = usedProgram || ranProgram;
	if(!ranProgram)
	{
		int done = 0;
		for(const QString & name : QDir(inFolder).entryList({"*.png"}))
		{
			if(cancelled())
			{
				failure = "stopped";
				return false;
			}
			if(!QFile::exists(outFolder + "/" + name))
			{
				const QImage small(inFolder + "/" + name);
				small.scaled(small.size() * 4, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).save(outFolder + "/" + name);
			}
			++done;
			report(stage, static_cast<double>(done) / std::max(1, expected), tr("%1 (smooth enlarging): %2 of %3 done").arg(what).arg(done).arg(expected));
		}
	}
	return true;
}

bool LegacyPicturesWorker::sharpenPictures(const QVector<Picture> & pictures, const QString & exe, bool & usedProgram)
{
	const QString work = baseFolder() + "/Work/pictures";
	QDir(work).removeRecursively();
	QDir().mkpath(work + "/in");
	for(const auto & p : pictures)
	{
		const QImage image = rgbPicture(p.originalPath);
		if(image.isNull() || !image.save(work + "/in/" + slugOf(p.wikiName) + ".png"))
		{
			failure = QString("%1 could not be read").arg(p.wikiName);
			return false;
		}
	}
	if(!sharpen(exe, work + "/in", work + "/out", pictures.size(), 3, tr("Pictures"), usedProgram))
		return false;
	for(const auto & p : pictures)
	{
		const QString slug = slugOf(p.title);
		const QString place = userFolder(p.loading ? "LoadingScreens" : "MenuBackgrounds");
		const QString prefix = p.loading ? "DMBLoad-" : "DMBMenuFile-";
		QDir().mkpath(place + "/Upscaled/DATA4X");
		QDir().mkpath(place + "/Upscaled/DATA3X");
		QDir().mkpath(place + "/Upscaled/DATA2X");
		const QString finalName = p.title + ".png";
		QFile::remove(place + "/" + finalName);
		if(!QFile::copy(p.originalPath, place + "/" + finalName))
		{
			failure = QString("%1 could not be put in the %2 folder").arg(finalName, p.loading ? "LoadingScreens" : "MenuBackgrounds");
			return false;
		}
		const QImage big(work + "/out/" + slugOf(p.wikiName) + ".png");
		if(big.isNull())
		{
			failure = QString("the sharpened %1 is missing").arg(p.wikiName);
			return false;
		}
		const QImage four = big.size() == QSize(3200, 2400) ? big : big.scaled(3200, 2400, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		const QString file = prefix + slugOf(finalName.left(finalName.size() - 4)) + ".png";
		four.save(place + "/Upscaled/DATA4X/" + file);
		four.scaled(2400, 1800, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).save(place + "/Upscaled/DATA3X/" + file);
		four.scaled(1600, 1200, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).save(place + "/Upscaled/DATA2X/" + file);
	}
	QDir(work).removeRecursively();
	return true;
}

/// K, October 5th: "these cutouts still have some really nasty jagged edges". The cut-out's alpha comes from the 1x sprite's hard mask, so each
/// 1x pixel of its outline shows as a 4 px stair. The alpha is blurred (a gaussian of `sigma` pixels) and made sharp again with a smooth step:
/// the contour is a smooth line with a feather of about 2 px; the colours, the artist's dark outline among them, are left as they are
static void smoothContour(QImage & image, double sigma)
{
	const int w = image.width(), h = image.height();
	const int radius = static_cast<int>(std::ceil(sigma * 3));
	std::vector<float> kernel(2 * radius + 1);
	float sum = 0;
	for(int i = -radius; i <= radius; ++i)
		sum += kernel[i + radius] = std::exp(-(i * i) / (2.0f * static_cast<float>(sigma * sigma)));
	for(auto & k : kernel)
		k /= sum;
	std::vector<float> a(static_cast<size_t>(w) * h), t(a.size());
	for(int y = 0; y < h; ++y)
	{
		const QRgb * line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
		for(int x = 0; x < w; ++x)
			a[static_cast<size_t>(y) * w + x] = qAlpha(line[x]) / 255.0f;
	}
	for(int y = 0; y < h; ++y) // across (nothing outside the picture)
		for(int x = 0; x < w; ++x)
		{
			float v = 0;
			for(int i = std::max(-radius, -x); i <= std::min(radius, w - 1 - x); ++i)
				v += a[static_cast<size_t>(y) * w + x + i] * kernel[i + radius];
			t[static_cast<size_t>(y) * w + x] = v;
		}
	const auto step = [](float low, float high, float v)
	{
		const float k = std::clamp((v - low) / (high - low), 0.0f, 1.0f);
		return k * k * (3 - 2 * k);
	};
	for(int y = 0; y < h; ++y) // down, and back into the picture
	{
		QRgb * line = reinterpret_cast<QRgb *>(image.scanLine(y));
		for(int x = 0; x < w; ++x)
		{
			float v = 0;
			for(int i = std::max(-radius, -y); i <= std::min(radius, h - 1 - y); ++i)
				v += t[static_cast<size_t>(y + i) * w + x] * kernel[i + radius];
			line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), static_cast<int>(std::lround(step(0.28f, 0.62f, v) * 255.0f)));
		}
	}
}

bool LegacyPicturesWorker::sharpenButtons(const QString & exe, bool & usedProgram)
{
	// the buttons of the Complete release, cut out of the player's own sprite archive with the glow taken away (MenuBackgrounds), each
	// frame on a neutral grey with a margin for the sharpener, then back to a transparent frame with a sharp edge
	const MenuEra * era = nullptr;
	for(const auto & candidate : MenuBackgrounds::eras())
		if(candidate.id == BUTTON_RELEASE && !candidate.sprites.empty())
			era = &candidate;
	if(era == nullptr)
		return true; // no such archive: the buttons stay as the game draws them
	const QString work = baseFolder() + "/Work/buttons";
	QDir(work).removeRecursively();
	QDir().mkpath(work + "/in");
	QMap<QString, QImage> bases;
	for(const QString & name : BUTTONS)
	{
		const auto frames = MenuBackgrounds::neutralFrames(*era, name.toStdString());
		for(size_t n = 0; n < frames.size(); ++n)
		{
			const auto & frame = frames[n];
			QImage base(frame.fullWidth, frame.fullHeight, QImage::Format_ARGB32);
			base.fill(Qt::transparent);
			for(int y = 0; y < frame.fullHeight; ++y)
				for(int x = 0; x < frame.fullWidth; ++x)
				{
					const ui8 index = frame.pixels[static_cast<size_t>(y) * frame.fullWidth + x];
					if(index > 7)
						base.setPixel(x, y, qRgba(frame.palette[index * 3], frame.palette[index * 3 + 1], frame.palette[index * 3 + 2], 255));
				}
			const QString key = QString("%1_%2").arg(name).arg(n);
			bases[key] = base;
			QImage flat(base.width() + 2 * MARGIN, base.height() + 2 * MARGIN, QImage::Format_RGB888);
			flat.fill(FLAT_GREY);
			QPainter painter(&flat);
			painter.drawImage(MARGIN, MARGIN, base);
			painter.end();
			flat.save(work + "/in/" + key + ".png");
		}
	}
	if(bases.isEmpty())
		return true;
	if(!sharpen(exe, work + "/in", work + "/out", bases.size(), 4, tr("Button frames"), usedProgram))
		return false;
	const QString root = userFolder("MenuBackgrounds") + "/Upscaled";
	for(auto it = bases.begin(); it != bases.end(); ++it)
	{
		const QImage up(work + "/out/" + it.key() + ".png");
		if(up.isNull())
		{
			failure = QString("the sharpened button %1 is missing").arg(it.key());
			return false;
		}
		const QImage base = it.value();
		const int scale = up.width() / (base.width() + 2 * MARGIN);
		QImage cut = up.copy(MARGIN * scale, MARGIN * scale, base.width() * scale, base.height() * scale).convertToFormat(QImage::Format_ARGB32);
		// the edge from the original's own outline: its alpha enlarged smoothly and made sharp again
		const QImage alpha = base.scaled(cut.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		for(int y = 0; y < cut.height(); ++y)
			for(int x = 0; x < cut.width(); ++x)
			{
				const int a = qAlpha(alpha.pixel(x, y));
				const int sharp = a > 170 ? 255 : (a < 70 ? 0 : (a - 70) * 255 / 100);
				cut.setPixel(x, y, qRgba(qRed(cut.pixel(x, y)), qGreen(cut.pixel(x, y)), qBlue(cut.pixel(x, y)), sharp));
			}
		smoothContour(cut, 5.0);
		const QString name = it.key().section('_', 0, 0);
		const int frameNumber = it.key().section('_', 1, 1).toInt();
		for(int factor = 2; factor <= 4; ++factor)
		{
			const QString sub = QString("DMBN-%1-%2").arg(BUTTON_RELEASE, name);
			const QString folder = QString("%1/SPRITES%2X").arg(root).arg(factor);
			QDir().mkpath(folder + "/" + sub);
			const QImage out = factor == 4 ? cut : cut.scaled(cut.width() * factor / 4, cut.height() * factor / 4, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
			out.save(QString("%1/%2/%3.png").arg(folder, sub).arg(frameNumber));
		}
	}
	for(const QString & name : BUTTONS)
	{
		int count = 0;
		while(bases.contains(QString("%1_%2").arg(name).arg(count)))
			++count;
		if(count == 0)
			continue;
		QJsonArray frames;
		for(int n = 0; n < count; ++n)
			frames.append(QString("%1.png").arg(n));
		const QString sub = QString("DMBN-%1-%2").arg(BUTTON_RELEASE, name);
		QJsonObject layout{{"basepath", sub + "/"}, {"sequences", QJsonArray{QJsonObject{{"group", 0}, {"frames", frames}}}}};
		for(int factor = 2; factor <= 4; ++factor)
		{
			QFile file(QString("%1/SPRITES%2X/%3.json").arg(root).arg(factor).arg(sub));
			if(file.open(QIODevice::WriteOnly))
				file.write(QJsonDocument(layout).toJson());
		}
	}
	QDir(work).removeRecursively();
	return true;
}

void LegacyPicturesWorker::writeIndex(const QVector<Picture> & pictures, bool usedProgram)
{
	QJsonArray files;
	for(const auto & p : pictures)
		files.append(QJsonObject{{"file", p.wikiName}, {"title", p.title}, {"kind", p.loading ? "loading screen" : "menu picture"},
			{"bytes", QFileInfo(p.originalPath).size()}, {"sha1", p.sha1}, {"wiki", "https://heroes.thelazy.net/index.php/File:" + QString(p.wikiName).replace(' ', '_')}});
	QJsonObject index{{"pipeline", PIPELINE_VERSION}, {"finished", QDateTime::currentDateTime().toString(Qt::ISODate)},
		{"sharpened", usedProgram ? "realesrgan-x4plus" : "smooth resize (the sharpener could not run)"}, {"files", files}};
	QFile file(baseFolder() + "/index.json");
	if(file.open(QIODevice::WriteOnly))
		file.write(QJsonDocument(index).toJson());
}

void LegacyPicturesWorker::run()
{
	QNetworkAccessManager manager;
	network = &manager;
	failure.clear();
	QDir().mkpath(baseFolder());
	QVector<Picture> pictures = selection();
	report(0, 0.0, tr("Asking the Heroes III wiki which pictures there are"));
	if(!queryWiki(pictures) || !downloadOriginals(pictures))
	{
		emit finishedWith(false, failure.isEmpty() ? QString("the download did not finish") : failure);
		return;
	}
	// a second press with nothing new: the same pipeline made all of it, with the sharpener, and every file is in place
	{
		bool complete = false;
		QFile indexFile(baseFolder() + "/index.json");
		if(indexFile.open(QIODevice::ReadOnly))
		{
			const QJsonObject index = QJsonDocument::fromJson(indexFile.readAll()).object();
			complete = index["pipeline"].toInt() == PIPELINE_VERSION && index["sharpened"].toString() == "realesrgan-x4plus"
				&& index["files"].toArray().size() == pictures.size();
		}
		for(const auto & p : pictures)
		{
			const QString place = userFolder(p.loading ? "LoadingScreens" : "MenuBackgrounds");
			const QString prefix = p.loading ? "DMBLoad-" : "DMBMenuFile-";
			if(!QFile::exists(place + "/" + p.title + ".png") || !QFile::exists(place + "/Upscaled/DATA4X/" + prefix + slugOf(p.title) + ".png"))
				complete = false;
		}
		for(const QString & name : BUTTONS)
			if(!QFile::exists(QString("%1/Upscaled/SPRITES4X/DMBN-%2-%3.json").arg(userFolder("MenuBackgrounds"), BUTTON_RELEASE, name)))
				complete = false;
		if(complete)
		{
			MenuBackgrounds::rescan();
			report(5, 1.0, tr("Everything is already in place"));
			emit finishedWith(true, QString("Nothing new: the %1 loading screens and menu pictures and the sharpened menu buttons are already in place.").arg(pictures.size()));
			return;
		}
	}
	QString exe;
	const bool haveProgram = ensureSharpener(exe);
	if(!haveProgram)
		exe.clear();
	bool usedProgram = false;
	if(!sharpenPictures(pictures, exe, usedProgram) || !sharpenButtons(exe, usedProgram))
	{
		emit finishedWith(false, failure.isEmpty() ? QString("the sharpening did not finish") : failure);
		return;
	}
	report(5, 0.2, tr("Writing the list of files"));
	writeIndex(pictures, usedProgram);
	report(5, 0.7, tr("Adding the pictures to the lists"));
	MenuBackgrounds::rescan();
	report(5, 1.0, tr("Done"));
	int loading = 0, menus = 0;
	for(const auto & p : pictures)
		(p.loading ? loading : menus) += 1;
	emit finishedWith(true, QString("%1 loading screens and %2 menu pictures added, with the menu buttons, %3. Pick them in the Main menu background and Loading screen lists.")
		.arg(loading).arg(menus).arg(usedProgram ? "all sharpened 4x" : "enlarged smoothly (the sharpener could not run on this computer)"));
}

LegacyPicturesDialog::LegacyPicturesDialog(QWidget * parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Get legacy menu themes and load screens"));
	setMinimumWidth(520);
	auto * layout = new QVBoxLayout(this);
	const auto counts = LegacyPicturesWorker::plannedCounts();
	plan = new QLabel(tr("This fetches %1 loading screens and %2 menu pictures of the Heroes III releases you have from the Heroes III wiki "
		"(about 10 MB, one picture at a time), cuts the menu buttons out of your own game files, and sharpens everything 4 times larger with "
		"Real-ESRGAN (a small program of about 45 MB, fetched once, which needs a graphics card; without one the pictures are only "
		"enlarged smoothly). Everything goes in your own Dead Man's Boots folder (%3). Nothing is installed apart from that, and it runs "
		"only while this window is open.").arg(counts.first).arg(counts.second).arg(LegacyPicturesWorker::baseFolder()), this);
	plan->setWordWrap(true);
	layout->addWidget(plan);
	bar = new QProgressBar(this);
	bar->setRange(0, 1000);
	bar->setValue(0);
	bar->setTextVisible(true);
	bar->setFormat("%p%");
	bar->setMinimumHeight(26);
	layout->addWidget(bar);
	step = new QLabel(tr("Ready. Press Start."), this);
	step->setWordWrap(true);
	QFont stepFont = step->font();
	stepFont.setBold(true);
	step->setFont(stepFont);
	layout->addWidget(step);
	status = new QLabel(QString(), this);
	status->setWordWrap(true);
	status->setMinimumHeight(40);
	layout->addWidget(status);
	auto * row = new QHBoxLayout();
	row->addStretch(1);
	start = new QPushButton(tr("Start"), this);
	cancel = new QPushButton(tr("Cancel"), this);
	row->addWidget(start);
	row->addWidget(cancel);
	layout->addLayout(row);
	setObjectName("legacyPicturesDialog");
	bar->setObjectName("legacyPicturesBar");
	status->setObjectName("legacyPicturesStatus");
	step->setObjectName("legacyPicturesStep");
	plan->setMinimumHeight(190);
	start->setObjectName("legacyPicturesStart");
	cancel->setObjectName("legacyPicturesCancel");

	connect(start, &QPushButton::clicked, this, &LegacyPicturesDialog::begin);
	connect(cancel, &QPushButton::clicked, this, [this]()
	{
		if(worker && worker->isRunning())
		{
			worker->requestInterruption();
			status->setText(tr("Stopping..."));
		}
		else
			reject();
	});
}

LegacyPicturesDialog::~LegacyPicturesDialog()
{
	if(worker && worker->isRunning())
	{
		worker->requestInterruption();
		worker->wait(15000);
	}
}

void LegacyPicturesDialog::snapshot(const QString & name)
{
	// a test hook: with DMB_LEGACY_SNAP set to a folder, the window is saved there when a stage begins and at the end (the test harness
	// cannot capture a dialog, so this is how its stages are looked at)
	const QString folder = qEnvironmentVariable("DMB_LEGACY_SNAP");
	if(folder.isEmpty())
		return;
	static QString last;
	if(name == last)
		return;
	last = name;
	QDir().mkpath(folder);
	grab().save(QString("%1/%2-%3.png").arg(folder).arg(QDateTime::currentDateTime().toString("hhmmss"), QString(name).replace(' ', '-')));
}

void LegacyPicturesDialog::begin()
{
	snapshot("plan");
	start->setEnabled(false);
	cancel->setText(tr("Stop"));
	worker = new LegacyPicturesWorker(this);
	connect(worker, &LegacyPicturesWorker::overall, this, [this](int permille, int stepNo, int steps, const QString & title, const QString & detail)
	{
		bar->setRange(0, 1000);
		bar->setValue(permille);
		step->setText(tr("Step %1 of %2: %3").arg(stepNo).arg(steps).arg(title));
		status->setText(detail);
		snapshot(QString("%1-%2").arg(title).arg(permille / 100));
	});
	connect(worker, &LegacyPicturesWorker::finishedWith, this, [this](bool ok, const QString & message)
	{
		cancel->setText(tr("Close"));
		start->setEnabled(!ok);
		if(ok)
		{
			bar->setRange(0, 1000);
			bar->setValue(1000);
			step->setText(tr("DONE. You can close this window."));
			status->setText(message);
		}
		else
		{
			step->setText(tr("STOPPED. Nothing is broken; what is done is kept."));
			status->setText(tr("%1\nPress Start to try again; it continues where it ended.").arg(message));
		}
		snapshot(ok ? "finished" : "stopped");
		emit picturesReady(ok);
	});
	worker->start();
}
