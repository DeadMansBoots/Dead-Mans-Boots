/*
 * main.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "main.h"
#include "mainwindow_moc.h"
#include "prepare.h"
#include "bugreport.h"
#include "helper.h"
#include "theme.h"

#include "../lib/VCMIDirs.h"
#include "../lib/CConfigHandler.h"
#include "../lib/filesystem/Filesystem.h"

#include <QApplication>
#include <QDir>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <QFile>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>

// Conan workaround https://github.com/conan-io/conan-center-index/issues/13332
#ifdef VCMI_IOS
# if __has_include("QIOSIntegrationPlugin.h")
#  include "QIOSIntegrationPlugin.h"
# endif
int argcForClient;
char ** argvForClient;
#elif defined(VCMI_ANDROID)
# include <QAndroidJniObject>
# include <QtAndroid>
#else
# include <QMessageBox>
# include <QProcess>
#endif // VCMI_IOS

// android must export main explicitly to make it visible in the shared library
#ifdef VCMI_ANDROID
# define MAIN_EXPORT ELF_VISIBILITY
#else
# define MAIN_EXPORT
#endif // VCMI_ANDROID

namespace
{
bool loadLauncherFilesPlain()
{
	try
	{
		CResourceHandler::initialize();
		CResourceHandler::load("config/filesystem.json");
		Helper::loadSettings();
		return true;
	}
	catch(const std::exception &)
	{
		return false;
	}
}

/// the player's game files read for the crash window's look; false when anything goes wrong, an access violation included (DMB, Reviewer pass 96)
bool loadLauncherFilesGuarded()
{
#ifdef _MSC_VER
	__try
	{
		return loadLauncherFilesPlain();
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
#else
	return loadLauncherFilesPlain();
#endif
}
}

int MAIN_EXPORT main(int argc, char * argv[])
{
	int result;
#ifdef VCMI_IOS
	{
#endif
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
	QApplication vcmilauncher(argc, argv);

	// use system proxy
	{
		QNetworkProxyFactory::setUseSystemConfiguration(true);
		const auto systemProxies = QNetworkProxyFactory::systemProxyForQuery();
		if(!systemProxies.isEmpty())
			QNetworkProxy::setApplicationProxy(systemProxies[0]);
	}

	launcher::prepare();

	// DMB: "VCMI_launcher.exe --report" (started by the crash dialog) makes and sends a bug report and nothing else
	// "VCMI_launcher.exe --report-crash" is started by the game's crash handler, with no window: it builds the report and sends it (K, October 10th: "you can't
	// see crashes you should")
	if(vcmilauncher.arguments().contains("--report-crash"))
	{
		// Settings, General, "Send crash reports to the developers automatically" (launcher.sendCrashReports, off until the player says yes)
		if(!BugReport::crashReportsEnabled())
			return 0;
		const BugReport::Report report = BugReport::build("the game crashed (sent by itself)");
		QString problem;
		if(report.error.isEmpty())
			BugReport::send(report, problem);
		return 0;
	}

	// "--report-dry FILE [--send]" is for tests: builds the report, writes where it is, the upload result and the crash summary to FILE, and shows and sends nothing
	const int dry = vcmilauncher.arguments().indexOf("--report-dry");
	if(dry >= 0 && dry + 1 < vcmilauncher.arguments().size())
	{
		const BugReport::Report report = BugReport::build("test", "test description from " + qEnvironmentVariable("USERNAME"));
		QFile out(vcmilauncher.arguments().at(dry + 1));
		// with "--send" too, the upload is made and its result written after the error line
		QString sent;
		if(vcmilauncher.arguments().contains("--send"))
		{
			QString problem;
			sent = BugReport::send(report, problem) ? "SENT" : "NOT SENT: " + problem;
		}
		if(out.open(QIODevice::WriteOnly))
			out.write((report.zipPath + "\n" + report.error + "\n" + sent + "\n" + report.crash).toUtf8());
		return 0;
	}
	// "--crash-window <dump>" is started by the game's crash handler, in the player's view (K, October 13th): one window with Send report and Don't send
	const int crashWindow = vcmilauncher.arguments().indexOf("--crash-window");
	if(crashWindow >= 0 && crashWindow + 1 < vcmilauncher.arguments().size())
	{
		// DMB (October 15th): the window wears the launcher's look (frame, gold buttons, lettering), which is read from the player's game files. A crash that came
		// from a broken mod or file must not take this window down too (Reviewer pass 96: a C++ catch does not stop an access violation), so the files are read on a
		// worker thread that is guarded against access violations, and waited for for a few seconds: a hang or a fault there leaves the window in the platform's own look
		QDir::setCurrent(QApplication::applicationDirPath());
		auto loaded = std::make_shared<std::promise<bool>>();
		std::future<bool> loadedResult = loaded->get_future();
		std::thread([loaded]() { loaded->set_value(loadLauncherFilesGuarded()); }).detach();
		if(loadedResult.wait_for(std::chrono::seconds(6)) == std::future_status::ready && loadedResult.get())
		{
			try
			{
				LauncherTheme::apply();
			}
			catch(const std::exception &)
			{
			}
		}
		BugReport::runCrashWindow(vcmilauncher.arguments().at(crashWindow + 1));
		return 0;
	}
	if(vcmilauncher.arguments().contains("--report"))
	{
		BugReport::run(nullptr, "after a crash");
		return 0;
	}

	MainWindow mainWindow;
	mainWindow.show();

#ifdef VCMI_ANDROID
	// changing language causes window to increase size over the bounds, force it back to proper value
	// TODO: check in Qt 6 if the hack is still needed
	auto appWindow = vcmilauncher.focusWindow();
	auto resizeWindowToScreen = [appWindow]{
		appWindow->resize(appWindow->screen()->availableSize());
	};
	QObject::connect(appWindow, &QWindow::widthChanged, resizeWindowToScreen);
	QObject::connect(appWindow, &QWindow::heightChanged, resizeWindowToScreen);
#endif

	result = vcmilauncher.exec();
#ifdef VCMI_IOS
	}
	if (result == 0)
		launchGame(argcForClient, argvForClient);
#endif
	return result;
}

void startGame(const QStringList & args)
{
	logGlobal->warn("Starting game with the arguments: %s", args.join(" ").toStdString());

#ifdef VCMI_IOS
	static const char clientName[] = "vcmiclient";
	argcForClient = args.size() + 1; //first argument is omitted
	argvForClient = new char*[argcForClient];
	argvForClient[0] = new char[strlen(clientName)+1];
	strcpy(argvForClient[0], clientName);
	for(int i = 1; i < argcForClient; ++i)
	{
		std::string s = args.at(i - 1).toStdString();
		argvForClient[i] = new char[s.size() + 1];
		strcpy(argvForClient[i], s.c_str());
	}
	qApp->quit();
#elif defined(VCMI_ANDROID)
	QtAndroid::androidActivity().callMethod<void>("onLaunchGameBtnPressed");
#else
	startExecutable(pathToQString(VCMIDirs::get().clientPath()), args);
#endif
}

void startEditor(const QStringList & args)
{
#ifdef ENABLE_EDITOR
	startExecutable(pathToQString(VCMIDirs::get().mapEditorPath()), args);
#endif
}

#ifndef VCMI_MOBILE
void startExecutable(QString name, const QStringList & args)
{
	QProcess process;
	auto showError = [&] {
		QMessageBox::critical(qApp->activeWindow(),
			QObject::tr("Error starting executable"),
			QObject::tr("Failed to start %1\nReason: %2").arg(name, process.errorString()));
	};

#if defined(VCMI_MAC) || defined(VCMI_WINDOWS)
	if(process.startDetached(name, args))
	{
		// DMB: Settings, General, "Keep Dead Man's Boots Launcher running when you launch the game"; off closes it, as it always did
		if(!settings["launcher"]["keepRunning"].Bool())
			qApp->quit();
	}
	else
	{
		showError();
	}
#else // Linux
	// Start vcmiclient and vcmieditor with QProcess::start() instead of QProcess::startDetached()
	// since startDetached() results in a missing terminal prompt after quitting vcmiclient.
	// QProcess::start() causes the launcher window to freeze while the child process is running, so we hide it in
	// MainWindow::on_startGameButton_clicked() and MainWindow::on_startEditorButton_clicked()
	process.setProcessChannelMode(QProcess::ForwardedChannels);
	process.start(name, args);
	process.waitForFinished(-1);

	if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
		showError();
	}

	qApp->quit();
#endif
}
#endif
