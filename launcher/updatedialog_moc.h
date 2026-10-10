/*
 * updatedialog_moc.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once
#include <QDialog>
#include <QNetworkAccessManager>

class QPushButton;

VCMI_LIB_NAMESPACE_BEGIN

class JsonNode;

VCMI_LIB_NAMESPACE_END

namespace Ui {
class UpdateDialog;
}

class UpdateDialog : public QDialog
{
	Q_OBJECT

public:
	explicit UpdateDialog(bool calledManually, QWidget *parent = nullptr);
	~UpdateDialog();
	
	static void showUpdateDialog(bool isManually);

private slots:
    void on_checkOnStartup_stateChanged(int state);
	/// DMB (K, October 10th: "the launcher still doesn't automatically update from github"): downloads the installer the update notice names,
	/// checks it against the notice's SHA-256 when it gives one, and runs it silently over this install, then the launcher ends and the
	/// installer starts the new one
	void on_updateNow_clicked();

private:
	Ui::UpdateDialog *ui;
	
	std::string currentVersion;
	std::string platformParameter = "other";
	
	QNetworkAccessManager networkManager;
	
	bool calledManually;
	
	QPushButton * updateButton = nullptr;
	QString installerUrl;
	QString installerSha256;

	void loadFromJson(const JsonNode & node);
};
