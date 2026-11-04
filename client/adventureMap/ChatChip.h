/*
 * ChatChip.h, part of Dead Man's Boots
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "../gui/CIntObject.h"

class CLabel;
class TransparentFilledRectangle;
class ChatConsoleWindow;

/// DMB (K, chat console, phase 1): the chip at the bottom left of the adventure map. It reads "Chat", with the number of lines that came in since the console window was last open; a click
/// opens the console window (ChatConsoleWindow). Its pos is everything it draws; the map view paints the tiles under it fresh every frame (AdventureMapInterface::show asks it to).
class ChatChip : public CIntObject
{
	std::shared_ptr<TransparentFilledRectangle> background;
	std::shared_ptr<TransparentFilledRectangle> frame;
	std::shared_ptr<CLabel> label;
	std::vector<std::shared_ptr<TransparentFilledRectangle>> badgeParts; // the red round-cornered badge with the unread count, behind badgeLabel
	std::shared_ptr<CLabel> badgeLabel;
	int shownUnread = -1;
	int shownChannel = -1;
	std::shared_ptr<ChatConsoleWindow> panel; // the console panel above the chip while it is open (a child of the chip)
	Rect mapArea;
	Rect closedPanelArea; // where the panel was: the map view paints it fresh for a few frames after the panel closes (nothing else repaints that area)
	int repaintFrames = 0;
	bool onlyWhileOpen = false; // in a battle the chip is drawn only while the panel is open: the hexes under it stay clickable, as in stock

	bool chipShown() const { return !panel && !onlyWhileOpen; } // the console, opened, is where the chip lives; in a battle the chip is not drawn at all (the hexes under it stay clickable)

	void openPanel();
	void closePanel();

	void refreshLabel();

public:
	/// bottom left of `area` (the map view's rectangle)
	explicit ChatChip(const Rect & area, bool onlyWhileOpen = false);

	void showAll(Canvas & to) override;
	void show(Canvas & to) override;
	void clickPressed(const Point & cursorPosition) override;
	void tick(uint32_t msPassed) override;
	void deactivate() override;
	bool receiveEvent(const Point & position, int eventType) const override;
	~ChatChip() override;
	/// the chip and the panel together: what the map view has to repaint under them each frame
	Rect repaintArea() const;
};
