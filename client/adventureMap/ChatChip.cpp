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
const int CHIP_W = 84;
const int CHIP_H = 22;
}

ChatChip::ChatChip(const Rect & area)
{
	OBJECT_CONSTRUCTION;
	pos = Rect(area.x + 8, area.y + area.h - CHIP_H - 8, CHIP_W, CHIP_H);
	background = std::make_shared<TransparentFilledRectangle>(Rect(0, 0, CHIP_W, CHIP_H), ColorRGBA(0, 0, 0, 170), ColorRGBA(212, 175, 55, 255), 1);
	label = std::make_shared<CLabel>(CHIP_W / 2, CHIP_H / 2, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, "");
	addUsedEvents(LCLICK | TIME);
	refreshLabel();
}

void ChatChip::refreshLabel()
{
	const int unread = GAME->server().getGameChat().getUnreadLines();
	if(unread == shownUnread)
		return;
	shownUnread = unread;
	std::string text = LIBRARY->generaltexth->translate("vcmi.chat.window.title");
	if(unread > 0)
		text += " " + std::to_string(unread);
	label->setText(text);
	label->color = unread > 0 ? Colors::WHITE : Colors::YELLOW;
}

void ChatChip::showAll(Canvas & to)
{
	show(to);
}

void ChatChip::show(Canvas & to)
{
	CIntObject::showAll(to);
}

void ChatChip::tick(uint32_t)
{
	if(GAME->server().getGameChat().getUnreadLines() != shownUnread)
		refreshLabel();
}

void ChatChip::clickPressed(const Point &)
{
	if(ENGINE->windows().topWindow<ChatConsoleWindow>())
		return;
	ENGINE->windows().createAndPushWindow<ChatConsoleWindow>();
}
