/*
 * legacypictures.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../StdInc.h"

class QNetworkAccessManager;

/// DMB: "Get legacy menu themes and load screens" (K, October 4th: one button, the launcher does the rest). Downloads the original main
/// menu pictures and loading screens of the releases the player owns from the Heroes III wiki (one request at a time, a pause between
/// them, a User-Agent naming the project, a hash for each file), puts them where the game looks for a player's own pictures
/// (MenuBackgrounds, LoadingScreens), cuts the menu buttons out of the player's own game files and sharpens all of it 4x with
/// Real-ESRGAN (realesrgan-ncnn-vulkan, fetched on first use), falling back to a smooth resize where the program cannot run.
/// Nothing is shipped with the launcher and nothing runs unless the player pressed the button.
class LegacyPicturesWorker : public QThread
{
	Q_OBJECT

public:
	/// the steps change what the files hold: a run with another number sharpens everything again
	static constexpr int PIPELINE_VERSION = 1;

	explicit LegacyPicturesWorker(QObject * parent = nullptr);

	/// what a run will fetch, for the plan the player reads first: (loading screens, menu pictures)
	static QPair<int, int> plannedCounts();

	/// the folder everything of this step is kept in (the player's DMB folder, LegacyPictures)
	static QString baseFolder();

signals:
	void progress(const QString & stage, int done, int total);
	void finishedWith(bool ok, const QString & message);

protected:
	void run() override;

private:
	struct Picture
	{
		QString wikiName; // "SoD Lich.png"
		QString title; // what the lists show: "Shadow of Death"
		bool loading; // a loading screen, else a menu picture
		QString needsMod; // only with this mod installed; empty for the game's own releases
		QString url;
		QString sha1;
		QString originalPath;
	};

	QNetworkAccessManager * network = nullptr;
	QString failure;

	static QVector<Picture> selection();
	QByteArray fetch(const QUrl & url, bool wiki, int timeoutMs = 120000);
	bool cancelled() const;
	bool queryWiki(QVector<Picture> & pictures);
	bool downloadOriginals(QVector<Picture> & pictures);
	bool ensureSharpener(QString & exe);
	bool sharpen(const QString & exe, const QString & inFolder, const QString & outFolder, int expected, const QString & stage, bool & usedProgram);
	bool sharpenPictures(const QVector<Picture> & pictures, const QString & exe, bool & usedProgram);
	bool sharpenButtons(const QString & exe, bool & usedProgram);
	void writeIndex(const QVector<Picture> & pictures, bool usedProgram);
	static QString slugOf(const QString & name);
};

class LegacyPicturesDialog : public QDialog
{
	Q_OBJECT

	QLabel * plan = nullptr;
	QProgressBar * bar = nullptr;
	QLabel * status = nullptr;
	QPushButton * start = nullptr;
	QPushButton * cancel = nullptr;
	LegacyPicturesWorker * worker = nullptr;

	void begin();
	void snapshot(const QString & name);

public:
	explicit LegacyPicturesDialog(QWidget * parent = nullptr);
	~LegacyPicturesDialog() override;

signals:
	void picturesReady(bool ok);
};
