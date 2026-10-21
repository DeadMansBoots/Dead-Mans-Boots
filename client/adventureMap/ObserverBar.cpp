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
#include "../render/Colors.h"
#include "../render/Graphics.h"
#include "../render/IFont.h"
#include "../render/IRenderHandler.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/TextControls.h"

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
}

ObserverBar::ObserverBar(const Rect & area)
{
	OBJECT_CONSTRUCTION;
	clearFlags();

	const auto font = ENGINE->renderHandler().loadFont(FONT_SMALL);
	const int pauseWidth = static_cast<int>(std::max(font->getStringWidth("Pause"), font->getStringWidth("Resume")));
	const int stepWidth = static_cast<int>(font->getStringWidth("Step"));
	const int dayWidth = static_cast<int>(font->getStringWidth("Day 999"));
	const int width = PAD + pauseWidth + GAP + stepWidth + GAP + dayWidth + PAD;
	pos = Rect(area.x + (area.w - width) / 2, area.y + 4, width, HEIGHT);

	background = std::make_shared<TransparentFilledRectangle>(Rect(0, 0, width, HEIGHT), ColorRGBA(0, 0, 0, 170), ColorRGBA(212, 175, 55, 255), 1);
	pauseArea = Rect(PAD - 4, 0, pauseWidth + 8, HEIGHT);
	stepArea = Rect(PAD + pauseWidth + GAP - 4, 0, stepWidth + 8, HEIGHT);
	pauseLabel = std::make_shared<CLabel>(PAD + pauseWidth / 2, HEIGHT / 2, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, "Pause");
	stepLabel = std::make_shared<CLabel>(PAD + pauseWidth + GAP + stepWidth / 2, HEIGHT / 2, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, "Step");
	dayLabel = std::make_shared<CLabel>(PAD + pauseWidth + GAP + stepWidth + GAP + dayWidth / 2, HEIGHT / 2, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, "Day");

	addUsedEvents(LCLICK | TIME);
}

ObserverBar::~ObserverBar()
{
	clearFlags();
}

void ObserverBar::clickPressed(const Point & cursorPosition)
{
	const Point local = cursorPosition - pos.topLeft();
	boost::system::error_code ignored;
	boost::filesystem::create_directories(flagFolder(), ignored);
	if(pauseArea.isInside(local))
	{
		paused = !paused;
		if(paused)
			std::ofstream((flagFolder() / "PAUSE").string()).put('1');
		else
		{
			boost::filesystem::remove(flagFolder() / "PAUSE", ignored);
			boost::filesystem::remove(flagFolder() / "STEP", ignored);
		}
		pauseLabel->setText(paused ? "Resume" : "Pause");
	}
	else if(stepArea.isInside(local) && paused)
	{
		// one hero move: OmniAI deletes STEP after it, and holds again
		std::ofstream((flagFolder() / "STEP").string()).put('1');
	}
}

void ObserverBar::tick(uint32_t msPassed)
{
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
