/*
 * ChatChip.h, part of Dead Man's Boots
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "../gui/CIntObject.h"

class CLabel;
class TransparentFilledRectangle;

/// DMB (K, chat console, phase 1): the chip at the bottom left of the adventure map. It reads "Chat", with the number of lines that came in since the console window was last open; a click
/// opens the console window (ChatConsoleWindow). Its pos is everything it draws; the map view paints the tiles under it fresh every frame (AdventureMapInterface::show asks it to).
class ChatChip : public CIntObject
{
	std::shared_ptr<TransparentFilledRectangle> background;
	std::shared_ptr<CLabel> label;
	int shownUnread = -1;

	void refreshLabel();

public:
	/// bottom left of `area` (the map view's rectangle)
	explicit ChatChip(const Rect & area);

	void showAll(Canvas & to) override;
	void show(Canvas & to) override;
	void clickPressed(const Point & cursorPosition) override;
	void tick(uint32_t msPassed) override;
};
