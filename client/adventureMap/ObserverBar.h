/*
 * ObserverBar.h, part of VCMI engine (DMB addition)
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../gui/CIntObject.h"

class CLabel;
class TransparentFilledRectangle;

/// DMB observer mode (K, October 20th: "in these tests I need the ability to stop time, examine heroes, look in towns, etc."): a bar at the top of the adventure map in a
/// game the client only watches (settings session.spectate: a game with no human player). Pause / Resume and Step write the PAUSE and STEP flag files in OmniAI's folder,
/// which OmniAI polls and honours (roster\NOTE-OmniAI-Observer-Contract-for-DMB-Dev.md): exact for OmniAI, and an AI that does not read them is not stopped. The bar
/// also shows the day. It reads the game state the client already holds and changes no rule. Its pos is everything it draws; the map view paints the tiles under it fresh
/// every frame (AdventureMapInterface::show asks it to), as for the turn progress bar.
class ObserverBar : public CIntObject
{
	std::shared_ptr<TransparentFilledRectangle> background;
	std::shared_ptr<CLabel> pauseLabel;
	std::shared_ptr<CLabel> stepLabel;
	std::shared_ptr<CLabel> dayLabel;
	Rect pauseArea;
	Rect stepArea;
	bool paused = false;
	int shownDay = -1;

public:
	/// where the bar sits: top centre of `area` (the map view's rectangle)
	explicit ObserverBar(const Rect & area);
	~ObserverBar() override;

	/// the flag files (OmniAI's folder, next to the game's other user data); every flag is removed at the start and the end of a watched game, so one left behind by a crash never freezes the next
	static boost::filesystem::path flagFolder();
	static void clearFlags();

	void clickPressed(const Point & cursorPosition) override;
	void tick(uint32_t msPassed) override;
};
