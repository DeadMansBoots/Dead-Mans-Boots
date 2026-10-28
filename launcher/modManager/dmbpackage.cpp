/*
 * dmbpackage.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "dmbpackage.h"

#include "../../lib/filesystem/MinizipExtensions.h"
#include "../../lib/filesystem/CZipSaver.h"
#include "../../vcmiqt/convpathqstring.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>

namespace DmbPackage
{
namespace
{
const QString MANIFEST = "dmb-manifest.json";
const QString MOD_FOLDER = "mod/";
const QString SAVE_FOLDER = "save/";
constexpr int MAX_NEEDS = 500;
constexpr int MAX_FILES = 20000;
constexpr qint64 MAX_FILE_SIZE = 512LL * 1024 * 1024;
constexpr qint64 MAX_TOTAL_SIZE = 2LL * 1024 * 1024 * 1024;
constexpr qint64 MAX_MANIFEST_SIZE = 4LL * 1024 * 1024;
constexpr qint64 MAX_RATIO = 200; // a zip that expands past 200 times its packed size is refused (a zip bomb)

/// one entry of the zip as its central directory describes it
struct ZipEntry
{
	QString name;
	qint64 size = 0;
	qint64 packed = 0;
	bool directory = false;
	bool link = false;
};

/// the zip, opened through the game's own minizip I/O, closed when it goes out of scope
class Zip
{
	unzFile handle = nullptr;

public:
	explicit Zip(const QString & path)
	{
		CDefaultIOApi api;
#if MINIZIP_NEEDS_32BIT_FUNCS
		auto functions = api.getApiStructure32();
		handle = unzOpen2(qstringToPath(path).c_str(), &functions);
#else
		auto functions = api.getApiStructure();
		handle = unzOpen2_64(qstringToPath(path).c_str(), &functions);
#endif
	}
	~Zip()
	{
		if(handle)
			unzClose(handle);
	}
	Zip(const Zip &) = delete;
	Zip & operator=(const Zip &) = delete;
	unzFile get() const { return handle; }
};

/// the entries, or false with a reason when a name cannot be read
bool listEntries(unzFile zip, QList<ZipEntry> & out, QString & reason)
{
	int result = unzGoToFirstFile(zip);
	while(result == UNZ_OK)
	{
		unz_file_info64 info;
		if(unzGetCurrentFileInfo64(zip, &info, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK || info.size_filename == 0 || info.size_filename > 1024)
		{
			reason = QObject::tr("A file name inside the package cannot be read.");
			return false;
		}
		std::vector<char> name(info.size_filename);
		unzGetCurrentFileInfo64(zip, &info, name.data(), name.size(), nullptr, 0, nullptr, 0);
		const QByteArray bytes(name.data(), static_cast<int>(name.size()));
		bool ascii = std::all_of(bytes.begin(), bytes.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
		if(!ascii && !(info.flag & 0x800)) // bit 11: the name is UTF-8; any other encoding is refused rather than guessed
		{
			reason = QObject::tr("A file name inside the package is not in UTF-8.");
			return false;
		}
		ZipEntry entry;
		entry.name = QString::fromUtf8(bytes);
		entry.size = static_cast<qint64>(info.uncompressed_size);
		entry.packed = static_cast<qint64>(info.compressed_size);
		entry.directory = entry.name.endsWith('/');
		// a link made on a Unix system keeps its file type in the high half of the external attributes
		entry.link = ((info.external_fa >> 16) & 0170000) == 0120000;
		out.push_back(entry);
		if(out.size() > MAX_FILES + 1)
		{
			reason = QObject::tr("The package has more than %1 files.").arg(MAX_FILES);
			return false;
		}
		result = unzGoToNextFile(zip);
	}
	if(result != UNZ_END_OF_LIST_OF_FILE)
	{
		reason = QObject::tr("The package is damaged.");
		return false;
	}
	return true;
}

/// the bytes of one entry, at most `limit` of them; empty with a reason when it cannot be read or is longer
bool readEntry(unzFile zip, const QString & name, qint64 limit, QByteArray & out, QString & reason)
{
	if(unzLocateFile(zip, name.toUtf8().constData(), 1) != UNZ_OK || unzOpenCurrentFile(zip) != UNZ_OK)
	{
		reason = QObject::tr("%1 cannot be read from the package.").arg(name);
		return false;
	}
	std::array<char, 65536> buffer;
	for(;;)
	{
		const int got = unzReadCurrentFile(zip, buffer.data(), static_cast<unsigned>(buffer.size()));
		if(got < 0)
		{
			unzCloseCurrentFile(zip);
			reason = QObject::tr("%1 is damaged in the package.").arg(name);
			return false;
		}
		if(got == 0)
			break;
		out.append(buffer.data(), got);
		if(out.size() > limit)
		{
			unzCloseCurrentFile(zip);
			reason = QObject::tr("%1 is larger than its declared size.").arg(name);
			return false;
		}
	}
	if(unzCloseCurrentFile(zip) != UNZ_OK)
	{
		reason = QObject::tr("%1 is damaged in the package.").arg(name);
		return false;
	}
	return true;
}

QList<int> versionParts(const QString & text)
{
	QList<int> parts;
	for(const auto & part : text.split('.'))
		parts.push_back(part.toInt());
	while(parts.size() < 4)
		parts.push_back(0);
	return parts;
}

bool validVersion(const QString & text)
{
	static const QRegularExpression pattern("^[0-9]{1,6}(\\.[0-9]{1,6}){0,3}$");
	return pattern.match(text).hasMatch();
}

int compareVersions(const QString & left, const QString & right)
{
	const auto a = versionParts(left);
	const auto b = versionParts(right);
	for(int i = 0; i < 4; ++i)
		if(a[i] != b[i])
			return a[i] < b[i] ? -1 : 1;
	return 0;
}

QString caseKey(const QString & name)
{
	return name.normalized(QString::NormalizationForm_C).toCaseFolded();
}

bool readManifest(const QByteArray & bytes, Manifest & manifest, QString & reason)
{
	QJsonParseError error;
	const auto document = QJsonDocument::fromJson(bytes, &error);
	if(error.error != QJsonParseError::NoError || !document.isObject())
	{
		reason = QObject::tr("The package's manifest is not valid.");
		return false;
	}
	const auto root = document.object();
	if(root.value("format").toInt() != 1)
	{
		reason = QObject::tr("The package was made for a newer Dead Man's Boots (manifest format %1).").arg(root.value("format").toVariant().toString());
		return false;
	}
	manifest.kind = root.value("kind").toString();
	manifest.id = root.value("id").toString();
	manifest.name = root.value("name").toString();
	manifest.version = root.value("version").toString();
	const auto range = root.value("dmbVersion").toObject();
	manifest.dmbMin = range.value("min").toString();
	manifest.dmbMax = range.value("max").toString();
	manifest.signature = root.contains("signature") && !root.value("signature").isNull();
	if(manifest.kind != "mod" && manifest.kind != "save")
	{
		reason = QObject::tr("This package contains a %1; this version of Dead Man's Boots installs mods and saves only.").arg(manifest.kind.isEmpty() ? QObject::tr("thing of no named kind") : manifest.kind);
		return false;
	}
	static const QRegularExpression idPattern("^[a-z0-9][a-z0-9_-]{0,63}$");
	if(!idPattern.match(manifest.id).hasMatch())
	{
		reason = QObject::tr("The package's mod id \"%1\" is not allowed: lowercase letters, digits, - and _ only.").arg(manifest.id);
		return false;
	}
	if(manifest.kind == "mod" && (manifest.id == "vcmi" || manifest.id == "core"))
	{
		reason = QObject::tr("The package would replace the game's own core mod \"%1\".").arg(manifest.id);
		return false;
	}
	if(manifest.name.trimmed().isEmpty() || manifest.name.size() > 100)
	{
		reason = QObject::tr("The package has no usable mod name.");
		return false;
	}
	if(!validVersion(manifest.version) || !validVersion(manifest.dmbMin) || (!manifest.dmbMax.isEmpty() && !validVersion(manifest.dmbMax)))
	{
		reason = QObject::tr("The package's version numbers are not valid.");
		return false;
	}
	QSet<QString> seen;
	for(const auto & value : root.value("files").toArray())
	{
		const auto file = value.toObject();
		FileEntry entry;
		entry.path = file.value("path").toString();
		entry.size = static_cast<qint64>(file.value("size").toDouble(-1));
		entry.sha256 = file.value("sha256").toString().toLatin1();
		static const QRegularExpression hashPattern("^[0-9a-f]{64}$");
		const QString problem = nameProblem(entry.path);
		if(!problem.isEmpty())
		{
			reason = QObject::tr("The manifest lists \"%1\": %2").arg(entry.path, problem);
			return false;
		}
		if(entry.size < 0 || entry.size > MAX_FILE_SIZE || !hashPattern.match(QString::fromLatin1(entry.sha256)).hasMatch())
		{
			reason = QObject::tr("The manifest's entry for \"%1\" is not valid.").arg(entry.path);
			return false;
		}
		if(seen.contains(caseKey(entry.path)))
		{
			reason = QObject::tr("The manifest lists \"%1\" twice (letter case aside).").arg(entry.path);
			return false;
		}
		seen.insert(caseKey(entry.path));
		manifest.files.push_back(entry);
	}
	if(manifest.files.isEmpty())
	{
		reason = QObject::tr("The package's manifest lists no files.");
		return false;
	}
	if(manifest.kind == "save")
	{
		const auto save = root.value("save").toObject();
		manifest.saveName = save.value("name").toString();
		manifest.saveDate = save.value("date").toString();
		if(manifest.saveName.trimmed().isEmpty() || manifest.saveName.size() > 100)
		{
			reason = QObject::tr("The package has no usable save name.");
			return false;
		}
		if(manifest.saveDate.size() > 40)
		{
			reason = QObject::tr("The package's save date is not valid.");
			return false;
		}
		const auto needs = root.value("needs").toArray();
		if(needs.size() > MAX_NEEDS)
		{
			reason = QObject::tr("The package lists more than %1 mods the save needs.").arg(MAX_NEEDS);
			return false;
		}
		for(const auto & value : needs)
		{
			const auto need = value.toObject();
			const QString id = need.value("id").toString();
			const QString version = need.value("version").toString();
			if(id.isEmpty() || id.size() > 120 || version.size() > 40)
			{
				reason = QObject::tr("The package's list of needed mods is not valid.");
				return false;
			}
			manifest.needs.push_back({id, version});
		}
		for(const auto & file : manifest.files)
			if(file.path != "game.vsgm1" && file.path != "game.vcgm1")
			{
				reason = QObject::tr("A save package may carry game.vsgm1 and game.vcgm1 only; it lists \"%1\".").arg(file.path);
				return false;
			}
	}
	return true;
}
}

QString nameProblem(const QString & name)
{
	if(name.isEmpty() || name.size() > 400)
		return QObject::tr("an empty or overlong name");
	for(const QChar c : name)
		if(c.unicode() < 0x20 || c.unicode() == 0x7f)
			return QObject::tr("a control character in the name");
	if(name.contains('\\'))
		return QObject::tr("a backslash in the name");
	if(name.contains(':'))
		return QObject::tr("a drive letter or a stream (\":\") in the name");
	if(name.startsWith('/'))
		return QObject::tr("an absolute path");
	QString trimmed = name;
	if(trimmed.endsWith('/'))
		trimmed.chop(1);
	static const QRegularExpression reserved(QString::fromUtf8("^(con|prn|aux|nul|com[0-9\u00b9\u00b2\u00b3]|lpt[0-9\u00b9\u00b2\u00b3]|conin\\$|conout\\$)$"),
		QRegularExpression::CaseInsensitiveOption);
	for(const auto & part : trimmed.split('/'))
	{
		if(part.isEmpty())
			return QObject::tr("an empty folder name (\"//\")");
		if(part == "." || part == "..")
			return QObject::tr("a \".\" or \"..\" folder");
		if(part.endsWith('.') || part.endsWith(' '))
			return QObject::tr("a name ending in a dot or a space");
		if(part.size() > 255)
			return QObject::tr("a name longer than 255 characters");
		if(reserved.match(part.section('.', 0, 0)).hasMatch())
			return QObject::tr("a name Windows reserves (%1)").arg(part);
		if(part.contains(QRegularExpression("[<>\"|?*]")))
			return QObject::tr("a character Windows does not allow in a name");
	}
	return {};
}

Inspection inspect(const QString & packagePath, const QString & runningDmbVersion)
{
	Inspection result;
	auto refuse = [&result](const QString & reason) { result.ok = false; result.reason = reason; return result; };

	QFile raw(packagePath);
	if(!raw.open(QIODevice::ReadOnly))
		return refuse(QObject::tr("The file cannot be opened."));
	if(raw.read(4) != QByteArray("PK\x03\x04", 4))
		return refuse(QObject::tr("This is not a Dead Man's Boots package."));
	raw.close();

	Zip zip(packagePath);
	if(!zip.get())
		return refuse(QObject::tr("This is not a Dead Man's Boots package."));

	QList<ZipEntry> entries;
	QString reason;
	if(!listEntries(zip.get(), entries, reason))
		return refuse(reason);

	// every name, before anything else: one bad name refuses the whole package
	QSet<QString> keys;
	QMap<QString, ZipEntry> files; // by the path inside mod/
	qint64 packedTotal = 0;
	bool hasManifest = false;
	bool anyMod = false;
	bool anySave = false;
	for(const auto & entry : entries)
	{
		const QString problem = nameProblem(entry.name);
		if(!problem.isEmpty())
			return refuse(QObject::tr("The package contains \"%1\": %2.").arg(entry.name, problem));
		if(entry.link)
			return refuse(QObject::tr("The package contains a link (\"%1\"); links are not allowed.").arg(entry.name));
		const QString key = caseKey(entry.directory ? entry.name.chopped(1) : entry.name);
		if(keys.contains(key))
			return refuse(QObject::tr("The package contains \"%1\" twice (letter case aside).").arg(entry.name));
		keys.insert(key);
		if(entry.size > MAX_FILE_SIZE)
			return refuse(QObject::tr("\"%1\" is larger than %2 MB.").arg(entry.name).arg(MAX_FILE_SIZE / 1024 / 1024));
		if(entry.packed > 0 && entry.size / entry.packed > MAX_RATIO)
			return refuse(QObject::tr("\"%1\" expands far beyond its packed size; the package is refused.").arg(entry.name));
		if(entry.packed == 0 && entry.size > 0)
			return refuse(QObject::tr("\"%1\" expands far beyond its packed size; the package is refused.").arg(entry.name));
		result.totalSize += entry.size;
		packedTotal += entry.packed;
		if(result.totalSize > MAX_TOTAL_SIZE)
			return refuse(QObject::tr("The package unpacks to more than %1 GB.").arg(MAX_TOTAL_SIZE / 1024 / 1024 / 1024));
		if(entry.name == MANIFEST)
		{
			hasManifest = true;
			continue;
		}
		const bool inSave = entry.name.startsWith(SAVE_FOLDER);
		if(!inSave && !entry.name.startsWith(MOD_FOLDER))
			return refuse(QObject::tr("The package contains \"%1\", outside its mod or save folder.").arg(entry.name));
		(inSave ? anySave : anyMod) = true;
		if(!entry.directory)
			files.insert(entry.name.mid((inSave ? SAVE_FOLDER : MOD_FOLDER).size()), entry);
	}
	if(packedTotal > 0 && result.totalSize / packedTotal > MAX_RATIO)
		return refuse(QObject::tr("The package expands far beyond its packed size; it is refused."));
	if(!hasManifest)
		return refuse(QObject::tr("This is not a Dead Man's Boots package (it has no %1).").arg(MANIFEST));

	QByteArray manifestBytes;
	if(!readEntry(zip.get(), MANIFEST, MAX_MANIFEST_SIZE, manifestBytes, reason))
		return refuse(reason);
	if(!readManifest(manifestBytes, result.manifest, reason))
		return refuse(reason);

	if(result.manifest.kind == "save" ? anyMod : anySave)
		return refuse(QObject::tr("The package carries files of another kind than its manifest names."));

	// the manifest is the authority: every file in it, nothing more, at the size it says
	QSet<QString> listed;
	for(const auto & file : result.manifest.files)
	{
		listed.insert(file.path);
		if(!files.contains(file.path))
			return refuse(QObject::tr("The manifest lists \"%1\", which is not in the package.").arg(file.path));
		if(files.value(file.path).size != file.size)
			return refuse(QObject::tr("\"%1\" is not the size the manifest says.").arg(file.path));
	}
	for(const auto & path : files.keys())
		if(!listed.contains(path))
			return refuse(QObject::tr("The package contains \"%1\", which its manifest does not list.").arg(path));
	if(result.manifest.kind == "save")
	{
		// a save never refuses on a version: the importer lists the differences (saveDifferences) and the player decides
		if(!listed.contains("game.vsgm1"))
			return refuse(QObject::tr("The package's save folder has no game.vsgm1."));
		result.ok = true;
		return result;
	}
	if(!listed.contains("mod.json"))
		return refuse(QObject::tr("The package's mod folder has no mod.json."));

	if(compareVersions(runningDmbVersion, result.manifest.dmbMin) < 0)
		return refuse(QObject::tr("This mod needs Dead Man's Boots %1 or newer; this is %2.").arg(result.manifest.dmbMin, runningDmbVersion));
	if(!result.manifest.dmbMax.isEmpty() && compareVersions(runningDmbVersion, result.manifest.dmbMax) > 0)
		return refuse(QObject::tr("This mod works with Dead Man's Boots up to %1; this is %2.").arg(result.manifest.dmbMax, runningDmbVersion));

	result.ok = true;
	return result;
}

bool install(const QString & packagePath, const QString & modsDir, const QString & runningDmbVersion, QString & reason)
{
	const Inspection inspection = inspect(packagePath, runningDmbVersion);
	if(!inspection.ok)
	{
		reason = inspection.reason;
		return false;
	}
	const Manifest & manifest = inspection.manifest;
	if(manifest.kind != "mod")
	{
		reason = QObject::tr("This package is a save, not a mod.");
		return false;
	}
	QDir mods(modsDir);
	if(!mods.exists())
	{
		reason = QObject::tr("The mods folder does not exist.");
		return false;
	}
	// a folder that differs from the id only by letter case would be a second copy Windows cannot tell apart
	for(const auto & existing : mods.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
		if(existing != manifest.id && existing.compare(manifest.id, Qt::CaseInsensitive) == 0)
		{
			reason = QObject::tr("A mod folder \"%1\" is already installed; it differs from \"%2\" only by letter case.").arg(existing, manifest.id);
			return false;
		}

	const QString tag = QString::number(QRandomGenerator::global()->generate64(), 16);
	const QString staging = mods.filePath(".dmb-staging-" + tag);
	const QString stagedMod = staging + "/" + manifest.id;
	auto cleanStaging = [&staging]() { QDir(staging).removeRecursively(); };
	if(!QDir().mkpath(stagedMod))
	{
		reason = QObject::tr("A staging folder cannot be made in the mods folder.");
		return false;
	}
	const QString stagedRoot = QDir(stagedMod).canonicalPath();

	Zip zip(packagePath);
	if(!zip.get())
	{
		cleanStaging();
		reason = QObject::tr("The package cannot be opened.");
		return false;
	}
	std::array<char, 65536> buffer;
	// every target must fit Windows' path limit before anything is written (a deep path refused half way left its staging folders behind, the Reviewer's R4)
	const QString longestPossible = mods.filePath(".dmb-staging-" + QString(16, 'f') + "/" + manifest.id + "/");
	for(const auto & file : manifest.files)
		if(QDir::toNativeSeparators(longestPossible + file.path).size() >= 250)
		{
			cleanStaging();
			reason = QObject::tr("\"%1\" would be too long a path for Windows in this mods folder.").arg(file.path);
			return false;
		}
	for(const auto & file : manifest.files)
	{
		const QString target = stagedMod + "/" + file.path;
		const QString folder = QFileInfo(target).absolutePath();
		if(!QDir().mkpath(folder))
		{
			cleanStaging();
			reason = QObject::tr("\"%1\" cannot be written.").arg(file.path);
			return false;
		}
		// the folder it lands in must be inside the staged mod, resolved as Windows resolves it
		const QString canonicalFolder = QDir(folder).canonicalPath();
		if(canonicalFolder.isEmpty() || !(canonicalFolder.compare(stagedRoot, Qt::CaseInsensitive) == 0
			|| canonicalFolder.startsWith(stagedRoot + "/", Qt::CaseInsensitive)))
		{
			cleanStaging();
			reason = QObject::tr("\"%1\" would land outside the mod's folder.").arg(file.path);
			return false;
		}
		QFile out(target);
		if(QFileInfo::exists(target) || !out.open(QIODevice::WriteOnly | QIODevice::NewOnly))
		{
			cleanStaging();
			reason = QObject::tr("\"%1\" cannot be written.").arg(file.path);
			return false;
		}
		const QString entryName = MOD_FOLDER + file.path;
		if(unzLocateFile(zip.get(), entryName.toUtf8().constData(), 1) != UNZ_OK || unzOpenCurrentFile(zip.get()) != UNZ_OK)
		{
			cleanStaging();
			reason = QObject::tr("\"%1\" cannot be read from the package.").arg(file.path);
			return false;
		}
		QCryptographicHash hash(QCryptographicHash::Sha256);
		qint64 written = 0;
		bool failed = false;
		for(;;)
		{
			const int got = unzReadCurrentFile(zip.get(), buffer.data(), static_cast<unsigned>(buffer.size()));
			if(got < 0)
			{
				failed = true;
				break;
			}
			if(got == 0)
				break;
			written += got;
			if(written > file.size || out.write(buffer.data(), got) != got)
			{
				failed = true;
				break;
			}
			hash.addData(buffer.data(), got);
		}
		const bool closedClean = unzCloseCurrentFile(zip.get()) == UNZ_OK;
		out.close();
		if(failed || !closedClean || written != file.size || hash.result().toHex() != file.sha256)
		{
			cleanStaging();
			reason = QObject::tr("\"%1\" does not match its manifest (size or SHA-256); nothing was installed.").arg(file.path);
			return false;
		}
	}

	// the swap: the installed copy moves aside, the staged one moves in, and the old one goes only after that worked
	const QString destination = mods.filePath(manifest.id);
	const QString aside = mods.filePath(".dmb-old-" + manifest.id + "-" + tag);
	const bool hadOld = QFileInfo::exists(destination);
	if(hadOld && !QDir().rename(destination, aside))
	{
		cleanStaging();
		reason = QObject::tr("The installed copy of \"%1\" is in use; close Dead Man's Boots and try again.").arg(manifest.id);
		return false;
	}
	if(!QDir().rename(stagedMod, destination))
	{
		if(hadOld && !QDir().rename(aside, destination))
		{
			reason = QObject::tr("The mod could not be moved into place, and the installed copy could not be moved back: it is in %1. Close Dead Man's Boots and "
				"rename that folder to %2.").arg(QDir::toNativeSeparators(aside), manifest.id);
			return false;
		}
		cleanStaging();
		reason = QObject::tr("The mod could not be moved into place; the installed copy is unchanged.");
		return false;
	}
	cleanStaging();
	if(hadOld && !QDir(aside).removeRecursively())
		logGlobal->warn("DMB package: the old copy of %s stays in %s until the next install sweeps it", manifest.id.toStdString(), aside.toStdString());
	return true;
}

int sweep(const QString & modsDir)
{
	int removed = 0;
	QDir mods(modsDir);
	for(const auto & name : mods.entryList({".dmb-staging-*", ".dmb-old-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot))
	{
		const QString path = QDir::toNativeSeparators(mods.absoluteFilePath(name));
		if(name.startsWith(".dmb-old-"))
		{
			// .dmb-old-<id>-<tag>: an installed copy moved aside; when its mod's own folder is missing (the move into place and the move back both
			// failed), it is the only copy and goes back to <id> instead of being deleted (the Reviewer's read of 3f940af79)
			const QString id = name.mid(9).section('-', 0, -2);
			if(!id.isEmpty() && !QFileInfo::exists(mods.filePath(id)))
			{
				if(QDir().rename(mods.absoluteFilePath(name), mods.filePath(id)))
					removed++;
				continue;
			}
		}
		// the long-path form reaches folders deeper than Windows' usual limit
		if(QDir(path).removeRecursively() || QDir("\\\\?\\" + path).removeRecursively())
			removed++;
	}
	return removed;
}

namespace
{
bool isLink(const QFileInfo & info)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
	if(info.isJunction())
		return true;
#endif
	return info.isSymLink();
}

/// the personal information a file must not carry: the user name, the computer name, a path into a user's folder (not Public or Default)
QString personalInformation(const QByteArray & bytes)
{
	QStringList names;
	for(const char * variable : {"USERNAME", "COMPUTERNAME"})
	{
		const QString value = qEnvironmentVariable(variable);
		if(value.size() >= 3)
			names.push_back(value);
	}
	for(const auto & name : names)
	{
		const QByteArray utf8 = name.toUtf8().toLower();
		QByteArray utf16;
		for(const QChar c : name.toLower())
		{
			utf16.append(static_cast<char>(c.unicode() & 0xff));
			utf16.append(static_cast<char>(c.unicode() >> 8));
		}
		const QByteArray lowered = bytes.toLower();
		if(lowered.contains(utf8) || lowered.contains(utf16))
			return QObject::tr("the Windows user or computer name");
	}
	static const QRegularExpression userPath("[A-Za-z]:[\\\\/]+Users[\\\\/]+(?!Public|Default|All Users)[^\\\\/\\s\"<>|*?]{1,40}", QRegularExpression::CaseInsensitiveOption);
	if(userPath.match(QString::fromLatin1(bytes)).hasMatch())
		return QObject::tr("a path into a Windows user's folder");
	return {};
}
}

bool make(const QString & modFolder, const QString & outPath, const QString & dmbMin, QString & reason)
{
	const QFileInfo folder(modFolder);
	if(!folder.isDir() || isLink(folder))
	{
		reason = QObject::tr("The mod's folder cannot be read, or it is a link.");
		return false;
	}
	const QString id = folder.fileName().toLower();
	static const QRegularExpression idPattern("^[a-z0-9][a-z0-9_-]{0,63}$");
	if(!idPattern.match(id).hasMatch() || id == "vcmi" || id == "core")
	{
		reason = QObject::tr("The folder's name \"%1\" is not a mod id a package can carry.").arg(folder.fileName());
		return false;
	}
	QFile modJson(folder.absoluteFilePath() + "/mod.json");
	if(!modJson.open(QIODevice::ReadOnly))
	{
		reason = QObject::tr("The mod's folder has no mod.json.");
		return false;
	}
	QString jsonText = QString::fromUtf8(modJson.readAll());
	jsonText.remove(QRegularExpression("^\\s*//[^\\n]*$", QRegularExpression::MultilineOption));
	const auto meta = QJsonDocument::fromJson(jsonText.toUtf8()).object();
	modJson.close();

	static const QSet<QString> junkFolders = {".git", ".svn", "__pycache__"};
	static const QSet<QString> junkFiles = {"thumbs.db", "desktop.ini", ".ds_store"};
	QStringList files;
	QStringList pending = {folder.absoluteFilePath()};
	while(!pending.isEmpty())
	{
		const QString current = pending.takeFirst();
		for(const auto & info : QDir(current).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name))
		{
			if(isLink(info))
			{
				reason = QObject::tr("The mod's folder holds a link (%1); a package cannot carry one.").arg(info.fileName());
				return false;
			}
			if(info.isDir())
			{
				if(!junkFolders.contains(info.fileName()))
					pending.push_back(info.absoluteFilePath());
			}
			else if(!junkFiles.contains(info.fileName().toLower()))
				files.push_back(QDir(folder.absoluteFilePath()).relativeFilePath(info.absoluteFilePath()));
		}
	}
	files.sort();

	QJsonArray listed;
	QList<QByteArray> contents;
	for(const auto & path : files)
	{
		const QString problem = nameProblem(path);
		if(!problem.isEmpty())
		{
			reason = QObject::tr("\"%1\": %2.").arg(path, problem);
			return false;
		}
		QFile file(folder.absoluteFilePath() + "/" + path);
		if(!file.open(QIODevice::ReadOnly) || file.size() > MAX_FILE_SIZE)
		{
			reason = QObject::tr("\"%1\" cannot be read, or is larger than %2 MB.").arg(path).arg(MAX_FILE_SIZE / 1024 / 1024);
			return false;
		}
		const QByteArray bytes = file.readAll();
		const QString personal = personalInformation(bytes);
		if(!personal.isEmpty())
		{
			reason = QObject::tr("\"%1\" carries %2; nothing was written.").arg(path, personal);
			return false;
		}
		QJsonObject entry;
		entry["path"] = path;
		entry["size"] = static_cast<double>(bytes.size());
		entry["sha256"] = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
		listed.append(entry);
		contents.push_back(bytes);
	}
	QJsonObject manifest;
	manifest["format"] = 1;
	manifest["kind"] = "mod";
	manifest["id"] = id;
	manifest["name"] = meta.value("name").toString(id);
	manifest["version"] = meta.value("version").toString("1.0");
	manifest["dmbVersion"] = QJsonObject{{"min", dmbMin}};
	manifest["files"] = listed;

	const QString partial = outPath + ".part";
	QFile::remove(partial);
	try
	{
		CZipSaver saver(std::make_shared<CDefaultIOApi>(), qstringToPath(partial));
		{
			const QByteArray bytes = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
			auto stream = saver.addFile(MANIFEST.toStdString());
			stream->write(reinterpret_cast<const ui8 *>(bytes.constData()), bytes.size());
		}
		for(int i = 0; i < files.size(); ++i)
		{
			auto stream = saver.addFile((MOD_FOLDER + files[i]).toUtf8().toStdString());
			stream->write(reinterpret_cast<const ui8 *>(contents[i].constData()), contents[i].size());
		}
	}
	catch(const std::exception & e)
	{
		QFile::remove(partial);
		reason = QObject::tr("The package could not be written: %1").arg(QString::fromUtf8(e.what()));
		return false;
	}
	QFile::remove(outPath);
	if(!QFile::rename(partial, outPath))
	{
		QFile::remove(partial);
		reason = QObject::tr("The package could not be written to %1.").arg(outPath);
		return false;
	}
	return true;
}

QStringList saveDifferences(const Manifest & manifest, const QString & runningDmbVersion, const QMap<QString, QString> & installedMods)
{
	QStringList differences;
	if(compareVersions(runningDmbVersion, manifest.dmbMin) != 0)
		differences.push_back(QObject::tr("The save was made with Dead Man's Boots %1; this is %2.").arg(manifest.dmbMin, runningDmbVersion));
	for(const auto & need : manifest.needs)
	{
		if(!installedMods.contains(need.first))
			differences.push_back(QObject::tr("The mod \"%1\" (version %2) is not installed on this machine.").arg(need.first, need.second));
		else if(installedMods.value(need.first) != need.second)
			differences.push_back(QObject::tr("The mod \"%1\": the save has version %2, this machine has %3.").arg(need.first, need.second, installedMods.value(need.first)));
	}
	return differences;
}

namespace
{
/// the file name a save is imported under: what Windows allows, at most 80 characters
QString safeFileName(QString name)
{
	name.replace(QRegularExpression("[<>:\"/\\\\|?*\\x00-\\x1f]"), "_");
	name = name.trimmed();
	while(name.endsWith('.') || name.endsWith(' '))
		name.chop(1);
	static const QRegularExpression reserved("^(con|prn|aux|nul|com[0-9]|lpt[0-9])$", QRegularExpression::CaseInsensitiveOption);
	if(name.isEmpty())
		name = "save";
	if(reserved.match(name.section('.', 0, 0)).hasMatch())
		name = "_" + name;
	return name.left(80).trimmed();
}

/// one entry of the package written to `target` (which must not exist), its size and SHA-256 checked against the manifest's
bool extractTo(unzFile zip, const QString & entryName, const QString & target, const FileEntry & file, QString & reason)
{
	QFile out(target);
	if(!out.open(QIODevice::WriteOnly | QIODevice::NewOnly))
	{
		reason = QObject::tr("\"%1\" cannot be written.").arg(file.path);
		return false;
	}
	if(unzLocateFile(zip, entryName.toUtf8().constData(), 1) != UNZ_OK || unzOpenCurrentFile(zip) != UNZ_OK)
	{
		reason = QObject::tr("\"%1\" cannot be read from the package.").arg(file.path);
		return false;
	}
	std::array<char, 65536> buffer;
	QCryptographicHash hash(QCryptographicHash::Sha256);
	qint64 written = 0;
	bool failed = false;
	for(;;)
	{
		const int got = unzReadCurrentFile(zip, buffer.data(), static_cast<unsigned>(buffer.size()));
		if(got < 0)
		{
			failed = true;
			break;
		}
		if(got == 0)
			break;
		written += got;
		if(written > file.size || out.write(buffer.data(), got) != got)
		{
			failed = true;
			break;
		}
		hash.addData(buffer.data(), got);
	}
	const bool closedClean = unzCloseCurrentFile(zip) == UNZ_OK;
	out.close();
	if(failed || !closedClean || written != file.size || hash.result().toHex() != file.sha256)
	{
		reason = QObject::tr("\"%1\" does not match its manifest (size or SHA-256); nothing was imported.").arg(file.path);
		return false;
	}
	return true;
}
}

bool makeSave(const QString & savePath, const QString & outPath, const QString & dmbVersion, const QList<QPair<QString, QString>> & mods, QString & reason)
{
	const QFileInfo server(savePath);
	if(!server.isFile() || isLink(server) || !server.fileName().endsWith(".vsgm1", Qt::CaseInsensitive))
	{
		reason = QObject::tr("The save cannot be read, or it is a link.");
		return false;
	}
	QStringList sources = {server.absoluteFilePath()};
	QStringList inside = {"game.vsgm1"};
	const QFileInfo client(server.absoluteFilePath().chopped(6) + ".vcgm1");
	if(client.isFile() && !isLink(client))
	{
		sources.push_back(client.absoluteFilePath());
		inside.push_back("game.vcgm1");
	}

	QJsonArray listed;
	QList<QByteArray> contents;
	for(int i = 0; i < sources.size(); ++i)
	{
		QFile file(sources[i]);
		if(!file.open(QIODevice::ReadOnly) || file.size() > MAX_FILE_SIZE)
		{
			reason = QObject::tr("\"%1\" cannot be read, or is larger than %2 MB.").arg(QFileInfo(sources[i]).fileName()).arg(MAX_FILE_SIZE / 1024 / 1024);
			return false;
		}
		const QByteArray bytes = file.readAll();
		QJsonObject entry;
		entry["path"] = inside[i];
		entry["size"] = static_cast<double>(bytes.size());
		entry["sha256"] = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
		listed.append(entry);
		contents.push_back(bytes);
	}

	const QString name = server.completeBaseName().left(100);
	QString id = name.toLower();
	id.replace(QRegularExpression("[^a-z0-9_-]+"), "-");
	while(id.startsWith('-') || id.startsWith('_'))
		id.remove(0, 1);
	id = id.left(64);
	if(id.isEmpty())
		id = "save";

	QJsonArray needs;
	for(const auto & mod : mods)
	{
		if(needs.size() >= MAX_NEEDS)
			break;
		needs.append(QJsonObject{{"id", mod.first}, {"version", mod.second}});
	}
	QJsonObject manifest;
	manifest["format"] = 1;
	manifest["kind"] = "save";
	manifest["id"] = id;
	manifest["name"] = name.isEmpty() ? QString("save") : name;
	manifest["version"] = "1";
	manifest["dmbVersion"] = QJsonObject{{"min", dmbVersion}};
	manifest["save"] = QJsonObject{{"name", name.isEmpty() ? QString("save") : name}, {"date", server.lastModified().toUTC().toString(Qt::ISODate)}};
	manifest["needs"] = needs;
	manifest["files"] = listed;

	const QString partial = outPath + ".part";
	QFile::remove(partial);
	try
	{
		CZipSaver saver(std::make_shared<CDefaultIOApi>(), qstringToPath(partial));
		{
			const QByteArray bytes = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
			auto stream = saver.addFile(MANIFEST.toStdString());
			stream->write(reinterpret_cast<const ui8 *>(bytes.constData()), bytes.size());
		}
		for(int i = 0; i < inside.size(); ++i)
		{
			auto stream = saver.addFile((SAVE_FOLDER + inside[i]).toStdString());
			stream->write(reinterpret_cast<const ui8 *>(contents[i].constData()), contents[i].size());
		}
	}
	catch(const std::exception & e)
	{
		QFile::remove(partial);
		reason = QObject::tr("The package could not be written: %1").arg(QString::fromUtf8(e.what()));
		return false;
	}
	if(QFileInfo::exists(outPath))
	{
		QFile::remove(partial);
		reason = QObject::tr("%1 already exists; nothing was replaced.").arg(outPath);
		return false;
	}
	if(!QFile::rename(partial, outPath))
	{
		QFile::remove(partial);
		reason = QObject::tr("The package could not be written to %1.").arg(outPath);
		return false;
	}
	return true;
}

bool installSave(const QString & packagePath, const QString & savesDir, const QString & runningDmbVersion, QString & reason, QString & importedAs)
{
	const Inspection inspection = inspect(packagePath, runningDmbVersion);
	if(!inspection.ok)
	{
		reason = inspection.reason;
		return false;
	}
	const Manifest & manifest = inspection.manifest;
	if(manifest.kind != "save")
	{
		reason = QObject::tr("This package is a mod, not a save.");
		return false;
	}
	const QString importedDir = savesDir + "/Imported";
	if(!QDir().mkpath(importedDir))
	{
		reason = QObject::tr("The folder for imported saves cannot be made.");
		return false;
	}
	// what an interrupted import left (its files end in .part, so the game never lists them)
	for(const auto & name : QDir(savesDir).entryList({".dmb-staging-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot))
		QDir(savesDir + "/" + name).removeRecursively();

	// a name nothing of the player's has: the save's own, or with " (2)", " (3)" after it
	const QString safe = safeFileName(manifest.saveName);
	QString stem = importedDir + "/" + safe;
	for(int n = 2; QFileInfo::exists(stem + ".vsgm1") || QFileInfo::exists(stem + ".vcgm1"); ++n)
	{
		if(n > 999)
		{
			reason = QObject::tr("Too many imported saves are already named \"%1\".").arg(safe);
			return false;
		}
		stem = importedDir + "/" + safe + " (" + QString::number(n) + ")";
	}
	if(QDir::toNativeSeparators(stem + ".vcgm1").size() >= 250)
	{
		reason = QObject::tr("The save's name would make too long a path for Windows in this folder.");
		return false;
	}

	const QString tag = QString::number(QRandomGenerator::global()->generate64(), 16);
	const QString staging = savesDir + "/.dmb-staging-" + tag;
	auto cleanStaging = [&staging]() { QDir(staging).removeRecursively(); };
	if(!QDir().mkpath(staging))
	{
		reason = QObject::tr("A staging folder cannot be made in the saves folder.");
		return false;
	}
	Zip zip(packagePath);
	if(!zip.get())
	{
		cleanStaging();
		reason = QObject::tr("The package cannot be opened.");
		return false;
	}
	QStringList staged;
	QStringList finals;
	for(const auto & file : manifest.files)
	{
		const QString target = staging + "/" + file.path + ".part";
		if(!extractTo(zip.get(), SAVE_FOLDER + file.path, target, file, reason))
		{
			cleanStaging();
			return false;
		}
		staged.push_back(target);
		finals.push_back(stem + file.path.mid(file.path.lastIndexOf('.')));
	}
	// the client's file first: the save shows in the game's list only when its .vsgm1 is in place
	QStringList moved;
	for(const QString ending : {QString(".vcgm1"), QString(".vsgm1")})
		for(int i = 0; i < staged.size(); ++i)
			if(finals[i].endsWith(ending))
			{
				if(!QFile::rename(staged[i], finals[i]))
				{
					for(const auto & done : moved)
						QFile::remove(done);
					cleanStaging();
					reason = QObject::tr("The save could not be moved into place; nothing was imported.");
					return false;
				}
				moved.push_back(finals[i]);
			}
	cleanStaging();
	importedAs = stem + ".vsgm1";
	return true;
}
}
