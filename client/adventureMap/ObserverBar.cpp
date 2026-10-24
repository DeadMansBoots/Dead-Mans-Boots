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

ObserverBar::ObserverBar(const Rect & area)
{
	OBJECT_CONSTRUCTION;
	clearFlags();
	canPause = GAME->server().isHost();

	const auto font = ENGINE->renderHandler().loadFont(FONT_SMALL);
	const int pauseWidth = static_cast<int>(std::max(font->getStringWidth("Pause"), font->getStringWidth("Resume")));
	const int stepWidth = static_cast<int>(font->getStringWidth("Step"));
	const int dayWidth = static_cast<int>(font->getStringWidth("Day 999"));
	const int width = PAD + pauseWidth + GAP + stepWidth + GAP + dayWidth + PAD;
	pos = Rect(area.x + (area.w - width) / 2, area.y + 4, width, HEIGHT);

	background = std::make_shared<TransparentFilledRectangle>(Rect(0, 0, width, HEIGHT), ColorRGBA(0, 0, 0, 170), ColorRGBA(212, 175, 55, 255), 1);
	pauseArea = Rect(PAD - 4, 0, pauseWidth + 8, HEIGHT);
	stepArea = Rect(PAD + pauseWidth + GAP - 4, 0, stepWidth + 8, HEIGHT);
	pauseLabel = std::make_shared<CLabel>(PAD + pauseWidth / 2, HEIGHT / 2, FONT_SMALL, ETextAlignment::CENTER, canPause ? Colors::YELLOW : ColorRGBA(120, 120, 120, 255), "Pause");
	stepLabel = std::make_shared<CLabel>(PAD + pauseWidth + GAP + stepWidth / 2, HEIGHT / 2, FONT_SMALL, ETextAlignment::CENTER, canPause ? Colors::WHITE : ColorRGBA(120, 120, 120, 255), "Step");
	dayLabel = std::make_shared<CLabel>(PAD + pauseWidth + GAP + stepWidth + GAP + dayWidth / 2, HEIGHT / 2, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, "Day");

	addUsedEvents(LCLICK | TIME | MOVE | HOVER);
}

ObserverBar::~ObserverBar()
{
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
	static const std::string text = "Pause and Step: only the host of the game can pause the AI";
	if(!canPause && (pauseArea.isInside(local) || stepArea.isInside(local)))
		ENGINE->statusbar()->write(text);
	else
		ENGINE->statusbar()->clearIfMatching(text);
}

void ObserverBar::hover(bool on)
{
	if(!on)
		ENGINE->statusbar()->clearIfMatching("Pause and Step: only the host of the game can pause the AI");
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
