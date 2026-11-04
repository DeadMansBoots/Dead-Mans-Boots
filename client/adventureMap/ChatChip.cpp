/*
 * ChatChip.cpp, part of Dead Man's Boots
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "ChatChip.h"

#include "../CServerHandler.h"
#include "../GameChatHandler.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../gui/WindowHandler.h"
#include "../render/Colors.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/TextControls.h"
#include "../windows/ChatConsoleWindow.h"

#include "../../lib/GameLibrary.h"
#include "../../lib/texts/CGeneralTextHandler.h"

namespace
{
const int CHIP_W = 124;
const int CHIP_H = 22;
}

ChatChip::ChatChip(const Rect & area)
{
	OBJECT_CONSTRUCTION;
	mapArea = area;
	pos = Rect(area.x + 8, area.y + area.h - CHIP_H - 8, CHIP_W, CHIP_H);
	ChatConsoleWindow::chipArea = pos;
	background = std::make_shared<TransparentFilledRectangle>(Rect(0, 0, CHIP_W, CHIP_H), ColorRGBA(0, 0, 0, 170), ColorRGBA(212, 175, 55, 255), 1);
	label = std::make_shared<CLabel>(8, CHIP_H / 2, FONT_SMALL, ETextAlignment::CENTERLEFT, Colors::YELLOW, "");
	badge = std::make_shared<TransparentFilledRectangle>(Rect(CHIP_W - 24, 3, 20, CHIP_H - 6), ColorRGBA(190, 20, 20, 255), ColorRGBA(255, 215, 120, 255), 1);
	badgeLabel = std::make_shared<CLabel>(CHIP_W - 14, CHIP_H / 2, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, "");
	addUsedEvents(LCLICK | TIME);
	refreshLabel();
}

void ChatChip::refreshLabel()
{
	const int unread = GAME->server().getGameChat().getUnreadLines();
	const int channel = static_cast<int>(ChatConsoleWindow::currentChannel);
	if(unread == shownUnread && channel == shownChannel)
		return;
	shownUnread = unread;
	shownChannel = channel;
	// "Chat <channel>" and a red badge with the number of lines not yet read (K's mockup)
	const char * key = ChatConsoleWindow::currentChannel == EChatChannel::TEAM ? "vcmi.chat.channel.team" : ChatConsoleWindow::currentChannel == EChatChannel::SPECTATORS ? "vcmi.chat.channel.spectators" : "vcmi.chat.channel.all";
	label->setText(LIBRARY->generaltexth->translate("vcmi.chat.window.title") + " " + LIBRARY->generaltexth->translate(key));
	badge->setEnabled(unread > 0);
	badgeLabel->setEnabled(unread > 0);
	badgeLabel->setText(std::to_string(std::min(unread, 99)));
}

void ChatChip::showAll(Canvas & to)
{
	show(to);
}

void ChatChip::show(Canvas & to)
{
	CIntObject::showAll(to);
}

ChatChip::~ChatChip()
{
	// the adventure map's chip and the battle window's chip take turns: only the one that set the area clears it
	if(ChatConsoleWindow::chipArea == pos)
		ChatConsoleWindow::chipArea = Rect();
}

void ChatChip::deactivate()
{
	// a hidden panel must not keep the text input: letters would not reach the shortcuts of the window above
	if(panel)
		panel->releaseFocus();
	CIntObject::deactivate();
}

Rect ChatChip::repaintArea() const
{
	return panel ? pos.include(panel->pos) : pos;
}

void ChatChip::openPanel()
{
	if(panel)
	{
		panel->focusInput();
		return;
	}
	panel = std::make_shared<ChatConsoleWindow>(mapArea);
	addChild(panel.get(), false);
	panel->focusInput();
}

void ChatChip::closePanel()
{
	if(!panel)
		return;
	removeChild(panel.get(), false);
	panel.reset();
}

void ChatChip::tick(uint32_t)
{
	// this chip is the one that is active now (a battle window has its own): the map ignores clicks where it and its panel are
	ChatConsoleWindow::chipArea = pos;
	ChatConsoleWindow::panelArea = panel ? panel->pos : Rect();
	if(ChatConsoleWindow::toggleAsked)
	{
		ChatConsoleWindow::toggleAsked = false;
		if(panel)
			closePanel();
		else
			openPanel();
	}
	if(ChatConsoleWindow::openAsked)
	{
		ChatConsoleWindow::openAsked = false;
		openPanel();
	}
	if(panel && panel->closeRequested())
		closePanel();
	refreshLabel();
}

void ChatChip::clickPressed(const Point &)
{
	ChatConsoleWindow::requestToggle();
}
