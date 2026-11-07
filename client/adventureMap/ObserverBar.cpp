/*
 * ObserverBar.cpp, part of VCMI engine (DMB addition)
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "ObserverBar.h"

#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../CPlayerInterface.h"
#include "../CServerHandler.h"
#include "../render/Colors.h"
#include "../render/Graphics.h"
#include "../render/IFont.h"
#include "../render/IRenderHandler.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/TextControls.h"

#include "../../lib/DmbObserverPause.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/VCMIDirs.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/gameState/CGameState.h"

#include <boost/filesystem.hpp>
#include <fstream>

namespace
{
const int PAD = 8;
const int HEIGHT = 22;
const int GAP = 14;
// the stock enemy hero speeds (adventure options), slowest first; 0 is no animation
const std::array<int, 5> SPEEDS = {400, 200, 100, 50, 0};

std::string speedText(int ms)
{
	return ms <= 0 ? std::string("Speed max") : "Speed " + std::to_string(100 * 100 / ms) + "%";
}

/// K, November 5th: "battles aren't optional": the bar's switch for the battles that begin from now on (settings session.spectate-skip-battle, read when each begins)
std::string battlesText()
{
	return settings["session"]["spectate-skip-battle"].Bool() ? "Battles: Skip" : "Battles: Watch";
}
const char * const BATTLES_HELP = "Battles: watch or skip the battles that begin from now on";
/// K, November 5th: "time controls only belong to the host always, and time only controls the speed of the AI players": Pause, Step and Speed are the host's
const char * const HOST_ONLY_HELP = "Pause, Step and Speed: only the host of the game controls the AI's time";
}

boost::filesystem::path ObserverBar::flagFolder()
{
	return VCMIDirs::get().userDataPath() / "OmniAI";
}

void ObserverBar::clearFlags()
{
	boost::system::error_code ignored;
	boost::filesystem::remove(flagFolder() / "PAUSE", ignored);
	boost::filesystem::remove(flagFolder() / "STEP", ignored);
	DmbObserverPause::paused = false;
	DmbObserverPause::steps = 0;
}

ObserverBar::ObserverBar(const Rect & area, bool ownsFlags)
	: ownsFlags(ownsFlags)
{
	OBJECT_CONSTRUCTION;
	if(ownsFlags)
		clearFlags();
	else
		paused = DmbObserverPause::paused; // the bar on a battle window shows the state the game is in, it does not reset it
	canPause = GAME->server().isHost();

	const auto font = ENGINE->renderHandler().loadFont(FONT_SMALL);
	const int pauseWidth = static_cast<int>(std::max(font->getStringWidth("Pause"), font->getStringWidth("Resume")));
	const int stepWidth = static_cast<int>(font->getStringWidth("Step"));
	const int speedWidth = static_cast<int>(std::max(font->getStringWidth("Speed 200%"), font->getStringWidth("Speed max")));
	const int battlesWidth = static_cast<int>(std::max(font->getStringWidth("Battles: Watch"), font->getStringWidth("Battles: Skip")));
	const int dayWidth = static_cast<int>(font->getStringWidth("Day 999"));
	const int width = PAD + pauseWidth + GAP + stepWidth + GAP + speedWidth + GAP + battlesWidth + GAP + dayWidth + PAD;
	pos = Rect(area.x + (area.w - width) / 2, area.y + 4, width, HEIGHT);

	background = std::make_shared<TransparentFilledRectangle>(Rect(0, 0, width, HEIGHT), ColorRGBA(0, 0, 0, 170), ColorRGBA(212, 175, 55, 255), 1);
	pauseArea = Rect(PAD - 4, 0, pauseWidth + 8, HEIGHT);
	stepArea = Rect(PAD + pauseWidth + GAP - 4, 0, stepWidth + 8, HEIGHT);
	pauseLabel = std::make_shared<CLabel>(PAD + pauseWidth / 2, HEIGHT / 2, FONT_SMALL, ETextAlignment::CENTER, canPause ? Colors::YELLOW : ColorRGBA(120, 120, 120, 255), paused ? "Resume" : "Pause");
	stepLabel = std::make_shared<CLabel>(PAD + pauseWidth + GAP + stepWidth / 2, HEIGHT / 2, FONT_SMALL, ETextAlignment::CENTER, canPause ? Colors::WHITE : ColorRGBA(120, 120, 120, 255), "Step");
	const int speedX = PAD + pauseWidth + GAP + stepWidth + GAP;
	speedArea = Rect(speedX - 4, 0, speedWidth + 8, HEIGHT);
	speedLabel = std::make_shared<CLabel>(speedX + speedWidth / 2, HEIGHT / 2, FONT_SMALL, ETextAlignment::CENTER, canPause ? Colors::YELLOW : ColorRGBA(120, 120, 120, 255),
		speedText(static_cast<int>(settings["adventure"]["enemyMoveTime"].Float())));
	const int battlesX = speedX + speedWidth + GAP;
	battlesArea = Rect(battlesX - 4, 0, battlesWidth + 8, HEIGHT);
	battlesLabel = std::make_shared<CLabel>(battlesX + battlesWidth / 2, HEIGHT / 2, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, battlesText());
	dayLabel = std::make_shared<CLabel>(battlesX + battlesWidth + GAP + dayWidth / 2, HEIGHT / 2, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, "Day");

	addUsedEvents(LCLICK | TIME | MOVE | HOVER);
}

ObserverBar::~ObserverBar()
{
	if(ownsFlags)
		clearFlags();
}

void ObserverBar::showAll(Canvas & to)
{
	show(to);
}

void ObserverBar::show(Canvas & to)
{
	// the children are static widgets that draw in showAll; CIntObject::showAll draws all of them
	CIntObject::showAll(to);
}

void ObserverBar::clickPressed(const Point & cursorPosition)
{
	const Point local = cursorPosition - pos.topLeft();
	if(speedArea.isInside(local))
	{
		if(!canPause)
			return; // a guest does not set the AI's speed: the label is grey and says why
		// the next stock speed, after the current one (a value between two goes to the faster)
		const int now = static_cast<int>(settings["adventure"]["enemyMoveTime"].Float());
		int next = SPEEDS[0];
		for(size_t i = 0; i < SPEEDS.size(); ++i)
		{
			if(now > SPEEDS[i] || (now == SPEEDS[i] && i + 1 < SPEEDS.size()))
			{
				next = now == SPEEDS[i] ? SPEEDS[i + 1] : SPEEDS[i];
				break;
			}
		}
		Settings entry = settings.write["adventure"]["enemyMoveTime"];
		entry->Float() = next;
		speedLabel->setText(speedText(next));
		logGlobal->info("Observer: enemy hero speed %d ms a tile", next);
		return;
	}
	if(battlesArea.isInside(local))
	{
		// what is watched is the viewer's own choice, the host's or a guest's
		Settings session = settings.write["session"];
		session["spectate-skip-battle"].Bool() = !settings["session"]["spectate-skip-battle"].Bool();
		battlesLabel->setText(battlesText());
		logGlobal->info("Observer: battles that begin from now on are %s", settings["session"]["spectate-skip-battle"].Bool() ? "skipped" : "watched");
		return;
	}
	if(!canPause)
		return; // a guest cannot pause the host's AI: the buttons are grey and say why
	logGlobal->debug("Observer bar clicked at %d,%d (bar at %d,%d)", cursorPosition.x, cursorPosition.y, pos.x, pos.y);
	boost::system::error_code ignored;
	boost::filesystem::create_directories(flagFolder(), ignored);
	if(pauseArea.isInside(local))
	{
		setPaused(!paused);
		if(paused)
			std::ofstream((flagFolder() / "PAUSE").string()).put('1');
		else
		{
			boost::filesystem::remove(flagFolder() / "PAUSE", ignored);
			boost::filesystem::remove(flagFolder() / "STEP", ignored);
		}
	}
	else if(stepArea.isInside(local) && paused)
	{
		// one action: the server lets one held request through; OmniAI makes one hero move (it deletes STEP after it) and holds again
		std::ofstream((flagFolder() / "STEP").string()).put('1');
		DmbObserverPause::steps++;
	}
}

void ObserverBar::setPaused(bool on)
{
	paused = on;
	DmbObserverPause::paused = on;
	if(!on)
		DmbObserverPause::steps = 0;
	pauseLabel->setText(paused ? "Resume" : "Pause");
	logGlobal->info("Observer: the game is %s", paused ? "paused" : "running");
}

void ObserverBar::mouseMoved(const Point & cursorPosition, const Point & lastUpdateDistance)
{
	const Point local = cursorPosition - pos.topLeft();
	static const std::string text = HOST_ONLY_HELP;
	static const std::string battlesHelp = BATTLES_HELP;
	const auto status = ENGINE->statusbar(); // a battle window has its own console, and a window with none has no status bar to write to
	if(!status)
		return;
	if(!canPause && (pauseArea.isInside(local) || stepArea.isInside(local) || speedArea.isInside(local)))
		status->write(text);
	else
		status->clearIfMatching(text);
	if(battlesArea.isInside(local))
		status->write(battlesHelp);
	else
		status->clearIfMatching(battlesHelp);
}

void ObserverBar::hover(bool on)
{
	if(!on && ENGINE->statusbar())
	{
		ENGINE->statusbar()->clearIfMatching(HOST_ONLY_HELP);
		ENGINE->statusbar()->clearIfMatching(BATTLES_HELP);
	}
}

void ObserverBar::tick(uint32_t msPassed)
{
	// the watch console's p and Enter write and remove the same PAUSE file: the bar follows it twice a second, so either control pauses the whole game
	sinceFlagCheck += msPassed;
	if(canPause && sinceFlagCheck >= 500)
	{
		sinceFlagCheck = 0;
		boost::system::error_code ignored;
		const bool flag = boost::filesystem::exists(flagFolder() / "PAUSE", ignored);
		if(flag != paused)
			setPaused(flag);
	}
	const auto * callback = GAME->interface() ? GAME->interface()->cb.get() : nullptr;
	if(!callback)
		return;
	const int day = callback->getDate(Date::DAY);
	if(day != shownDay)
	{
		shownDay = day;
		dayLabel->setText("Day " + std::to_string(day));
	}
}
