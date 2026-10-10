/*
 * updatedialog_moc.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "launchertooltips.h"
#include "updatedialog_moc.h"
#include "ui_updatedialog_moc.h"

#include "../lib/CConfigHandler.h"
#include "../lib/GameConstants.h"
#include "../lib/DmbRelease.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QFile>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QApplication>
#include <QMessageBox>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

UpdateDialog::UpdateDialog(bool calledManually, QWidget *parent):
	QDialog(parent),
	ui(new Ui::UpdateDialog),
	calledManually(calledManually)
{
	ui->setupUi(this);
	LauncherTooltips::apply(this, "updatedialog_moc.ui");
	
	if(calledManually)
	{
		setWindowModality(Qt::ApplicationModal);
		show();
	}
	
	connect(ui->closeButton, SIGNAL(clicked()), this, SLOT(close()));
	
	if(settings["launcher"]["updateOnStartup"].Bool())
		ui->checkOnStartup->setCheckState(Qt::CheckState::Checked);
	
	// DMB's own number: the update feed lists DMB releases, which all run on the same VCMI version
	// the beta too (K, October 10th: "he should be able to auto update"): DMB_RELEASE_LABEL is "beta.4" and so on; the feed names the newest
	currentVersion = "DMB " DMB_VERSION_STRING "-" DMB_RELEASE_LABEL_TEXT;
	
	setWindowTitle(QString::fromStdString(currentVersion));
	
#ifdef VCMI_WINDOWS
	platformParameter = "windows";
#elif defined(VCMI_MAC)
	platformParameter = "macos";
#elif defined(VCMI_IOS)
	platformParameter = "ios";
#elif defined(VCMI_ANDROID)
	platformParameter = "android";
#elif defined(VCMI_UNIX)
	platformParameter = "linux";
#endif
	
	QString url = QString::fromStdString(settings["launcher"]["updateConfigUrl"].String());
		
	QNetworkReply *response = networkManager.get(QNetworkRequest(QUrl(url)));
	
	connect(response, &QNetworkReply::finished, [&, response]{
		response->deleteLater();
		
		if(response->error() != QNetworkReply::NoError)
		{
			ui->versionLabel->setStyleSheet("QLabel { background-color : red; color : black; }");
			ui->versionLabel->setText(tr("Network error"));
			ui->plainTextEdit->setPlainText(response->errorString());
			return;
		}
		
		auto byteArray = response->readAll();
		JsonNode node(reinterpret_cast<const std::byte*>(byteArray.constData()), byteArray.size(), "<network packet from server at updateConfigUrl>");
		loadFromJson(node);
	});
}

UpdateDialog::~UpdateDialog()
{
	delete ui;
}

void UpdateDialog::showUpdateDialog(bool isManually)
{
	auto * dialog = new UpdateDialog(isManually);
	
	dialog->setAttribute(Qt::WA_DeleteOnClose);
}

void UpdateDialog::on_checkOnStartup_stateChanged(int state)
{
	Settings node = settings.write["launcher"]["updateOnStartup"];
	node->Bool() = ui->checkOnStartup->isChecked();
}

void UpdateDialog::loadFromJson(const JsonNode & node)
{
	if(node.getType() != JsonNode::JsonType::DATA_STRUCT ||
	   node["updateType"].getType() != JsonNode::JsonType::DATA_STRING ||
	   node["version"].getType() != JsonNode::JsonType::DATA_STRING ||
	   node["changeLog"].getType() != JsonNode::JsonType::DATA_STRING ||
	   node["downloadLinks"].getType() != JsonNode::JsonType::DATA_STRUCT) //we need at least one link - other are optional
	{
		ui->plainTextEdit->setPlainText(tr("Cannot read JSON from URL or incorrect JSON data"));
		return;
	}
	
	//check whether update is needed
	bool isFutureVersion = true;
	std::string newVersion = node["version"].String();
	for(auto & prevVersion : node["history"].Vector())
	{
		if(prevVersion.String() == currentVersion)
			isFutureVersion = false;
	}
		
	if(isFutureVersion || currentVersion == newVersion)
	{
		if(!calledManually)
			close();
		
		return;
	}
	
	if(!calledManually)
	{
		setWindowModality(Qt::ApplicationModal);
		show();
	}
	
	const auto updateType = node["updateType"].String();
	
	QString bgColor;
	if(updateType == "minor")
		bgColor = "gray";
	else if(updateType == "major")
		bgColor = "orange";
	else if(updateType == "critical")
		bgColor = "red";
	
	ui->versionLabel->setStyleSheet(QString("QLabel { background-color : %1; color : black; }").arg(bgColor));
	ui->versionLabel->setText(QString::fromStdString(newVersion));
	ui->plainTextEdit->setPlainText(QString::fromStdString(node["changeLog"].String()));
	
	QString downloadLink = QString::fromStdString(node["downloadLinks"]["other"].String());
	if(node["downloadLinks"][platformParameter].getType() == JsonNode::JsonType::DATA_STRING)
		downloadLink = QString::fromStdString(node["downloadLinks"][platformParameter].String());
	
	ui->downloadLink->setText(QString{"<a href=\"%1\">Download page</a>"}.arg(downloadLink));

	// DMB: an "Update now" button when the notice names a Windows installer (https only), with the installer's SHA-256 when it gives one
	installerUrl.clear();
	installerSha256.clear();
	if(platformParameter == "windows" && node["downloadLinks"]["windows"].getType() == JsonNode::JsonType::DATA_STRING)
	{
		const QString url = QString::fromStdString(node["downloadLinks"]["windows"].String());
		if(url.startsWith("https://") && url.endsWith(".exe", Qt::CaseInsensitive))
		{
			installerUrl = url;
			if(node["windowsSha256"].getType() == JsonNode::JsonType::DATA_STRING)
				installerSha256 = QString::fromStdString(node["windowsSha256"].String()).toLower();
		}
	}
	if(!installerUrl.isEmpty())
	{
		if(!updateButton)
		{
			updateButton = new QPushButton(this);
			updateButton->setObjectName("updateNow");
			ui->gridLayout->addWidget(updateButton, ui->gridLayout->rowCount(), 0, 1, -1);
			connect(updateButton, &QPushButton::clicked, this, &UpdateDialog::on_updateNow_clicked);
		}
		updateButton->setText(tr("Update now: download and install %1").arg(QString::fromStdString(newVersion)));
		updateButton->setEnabled(true);
	}
}

void UpdateDialog::on_updateNow_clicked()
{
	if(installerUrl.isEmpty() || !updateButton)
		return;
	updateButton->setEnabled(false);
	updateButton->setText(tr("Downloading..."));

	QNetworkRequest request{QUrl(installerUrl)};
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	QNetworkReply * reply = networkManager.get(request);
	connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total)
	{
		if(total > 0 && updateButton)
			updateButton->setText(tr("Downloading... %1%").arg(static_cast<int>(100.0 * got / total)));
	});
	connect(reply, &QNetworkReply::finished, this, [this, reply]
	{
		reply->deleteLater();
		const auto fail = [this](const QString & why)
		{
			updateButton->setEnabled(true);
			updateButton->setText(tr("Update now (failed: %1; try again, or use the download page)").arg(why));
		};
		if(reply->error() != QNetworkReply::NoError)
			return fail(reply->errorString());
		const QByteArray data = reply->readAll();
		if(data.size() < 1024 * 1024 || data.left(2) != "MZ")
			return fail(tr("not an installer"));
		if(!installerSha256.isEmpty() && QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().toLower() != installerSha256.toUtf8())
			return fail(tr("the file does not match the update notice"));
		const QString path = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/DeadMansBoots-Update.exe";
		QFile file(path);
		if(!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
			return fail(tr("could not save the installer"));
		file.close();
#ifdef Q_OS_WIN
		// ShellExecute, not QProcess: the installer of an all-users install asks for administrator rights (the UAC question), which a plain
		// CreateProcess refuses. /SILENT shows its small progress window, /CLOSEAPPLICATIONS (also in the script) closes this launcher,
		// /LAUNCH starts the new launcher at the end
		const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.toStdWString().c_str(), L"/SILENT /NORESTART /LAUNCH", nullptr, SW_SHOWNORMAL));
		if(result <= 32)
			return fail(tr("the installer could not be started"));
		QApplication::quit();
#else
		fail(tr("only on Windows"));
#endif
	});
}
