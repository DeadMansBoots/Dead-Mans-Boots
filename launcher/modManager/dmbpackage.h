/*
 * dmbpackage.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../StdInc.h"

/// DMB (the project owner, October 23rd: "the addition of DMB mods and .DMB files and an installer that registers it with the system"): a .dmb file is a zip with
/// dmb-manifest.json at its top and the mod's own folder under mod/. The manifest names the kind ("mod" for now), the id, name and version, the DMB
/// versions it works with, and every file with its size and SHA-256. Everything is checked before anything is written (the Reviewer's list,
/// .tmp\review-1021\DMB-FILE-SAFETY-CHECKS-1023.md), and the install goes through a staging folder so a failure leaves the installed mod as it was.
namespace DmbPackage
{
struct FileEntry
{
	QString path; // relative to the mod's folder, '/' separated
	qint64 size = 0;
	QByteArray sha256; // lowercase hex
};

struct Manifest
{
	QString kind;
	QString id;
	QString name;
	QString version;
	QString dmbMin;
	QString dmbMax;
	bool signature = false; // present, and not checked: the window says "not verified"
	QList<FileEntry> files;
};

struct Inspection
{
	bool ok = false;
	QString reason; // why it is refused, in plain words
	Manifest manifest;
	qint64 totalSize = 0;
};

/// what is wrong with one name inside the package, or an empty string when nothing is
QString nameProblem(const QString & name);

/// reads the package and checks all of it; writes nothing
Inspection inspect(const QString & packagePath, const QString & runningDmbVersion);

/// installs the package into modsDir/<id>: unpacked into a staging folder beside it, every file's size and hash checked, then the folders are swapped;
/// false with a plain reason when anything fails, the installed copy of the mod kept as it was
bool install(const QString & packagePath, const QString & modsDir, const QString & runningDmbVersion, QString & reason);

/// a .dmb made from a mod's folder (the folder's name is the id); false with a plain reason when the folder holds a link, has no mod.json, or a
/// file carries the Windows user name, the computer name or a user folder path (the project owner, October 11th: no personal information in what leaves the PC)
bool make(const QString & modFolder, const QString & outPath, const QString & dmbMin, QString & reason);

/// removes what an interrupted or failed install left in the mods folder (.dmb-staging-* and .dmb-old-* folders); the number removed
int sweep(const QString & modsDir);
}
