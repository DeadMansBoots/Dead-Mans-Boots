/*
 * ChatConsoleWindow.cpp, part of Dead Man's Boots
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "ChatConsoleWindow.h"

#include "CMessage.h"

#include "../CPlayerInterface.h"
#include "../CServerHandler.h"
#include "../ClientCommandManager.h"
#include "../GameChatHandler.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../adventureMap/CInGameConsole.h"
#include "../gui/Shortcut.h"
#include "../gui/WindowHandler.h"
#include "../gui/TextAlignment.h"
#include "../render/Canvas.h"
#include "../render/Colors.h"
#include "../render/Graphics.h"
#include "../render/IFont.h"
#include "../render/IRenderHandler.h"
#include "../widgets/CTextInput.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/Images.h"
#include "../widgets/TextControls.h"

#include "../../lib/CConfigHandler.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/CThreadHelper.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/StartInfo.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/texts/TextOperations.h"

namespace
{
const int WIN_W = ChatConsoleWindow::PANEL_W;
const int WIN_H = ChatConsoleWindow::PANEL_H;
const int MARGIN = 12;
const int ROW_H = 22;
const int MIN_W = 300; // below this the log, the To: row and the buttons do not fit
const int MIN_H = 170;
const int CORNER = 10; // the top right corner that is dragged
int clampWidth(int w, const Rect & area) { return std::clamp(w, MIN_W, std::max(MIN_W, area.w * 3 / 4)); }
int clampHeight(int h, const Rect & area) { return std::clamp(h, MIN_H, std::max(MIN_H, area.h * 7 / 10)); }
const ColorRGBA GOLD(212, 175, 55, 255);

// the colours a channel can be set to (settings general.chatColour*: an index), none of them an orange, red, blue, tan, purple, teal or pink of the players
const std::array<ColorRGBA, 8> PALETTE = {ColorRGBA(0, 255, 0), ColorRGBA(100, 210, 255), ColorRGBA(200, 200, 200), ColorRGBA(255, 255, 255), ColorRGBA(255, 255, 0), ColorRGBA(210, 180, 255), ColorRGBA(235, 225, 195), ColorRGBA(235, 205, 125)};
const std::array<const char *, 8> PALETTE_NAMES = {"vcmi.chat.colour.green", "vcmi.chat.colour.cyan", "vcmi.chat.colour.grey", "vcmi.chat.colour.white", "vcmi.chat.colour.yellow", "vcmi.chat.colour.lavender", "vcmi.chat.colour.parchment", "vcmi.chat.colour.palegold"};

const char * colourKey(int row)
{
	return row == 0 ? "chatColourAll" : row == 1 ? "chatColourTeam" : "chatColourSpectators";
}

EFonts chatFont()
{
	const double size = settings["general"]["chatFontSize"].Float();
	return size >= 2 ? FONT_BIG : size >= 1 ? FONT_MEDIUM : FONT_SMALL;
}

// Team is offered only to a player who has an ally (the team has more than one player)
bool teamAvailable()
{
	if(!GAME || !GAME->interface() || !GAME->interface()->cb)
		return true;
	const auto * team = GAME->interface()->cb->getPlayerTeam(GAME->interface()->playerID);
	return !team || team->players.size() > 1;
}

bool channelOffered(EChatChannel channel)
{
	// Spectators is always offered: hot-joined watchers are not in the client's lobby state, so a rule that hides it until one is connected would hide it from the usual case (parked)
	return channel != EChatChannel::TEAM || teamAvailable();
}

std::string channelName(EChatChannel channel)
{
	switch(channel)
	{
		case EChatChannel::TEAM:
			return LIBRARY->generaltexth->translate("vcmi.chat.channel.team");
		case EChatChannel::SPECTATORS:
			return LIBRARY->generaltexth->translate("vcmi.chat.channel.spectators");
		default:
			return LIBRARY->generaltexth->translate("vcmi.chat.channel.all");
	}
}
}

/// a box with a gold border and a centred label that does something when clicked
class ChatButton : public CIntObject
{
	std::function<void()> onClick;
	std::shared_ptr<TransparentFilledRectangle> frame;
	std::shared_ptr<CLabel> label;

public:
	ChatButton(const Rect & area, const std::string & text, std::function<void()> callback)
		: CIntObject(LCLICK, area.topLeft())
		, onClick(std::move(callback))
	{
		OBJECT_CONSTRUCTION;
		pos.w = area.w;
		pos.h = area.h;
		frame = std::make_shared<TransparentFilledRectangle>(Rect(0, 0, area.w, area.h), ColorRGBA(0, 0, 0, 140), GOLD, 1);
		label = std::make_shared<CLabel>(area.w / 2, area.h / 2, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, text);
	}

	void setText(const std::string & text)
	{
		label->setText(text);
	}

	void setWidth(int w)
	{
		pos.w = w;
		frame->pos.w = w;
		label->moveTo(pos.topLeft() + Point(w / 2, pos.h / 2));
	}

	void clickPressed(const Point &) override
	{
		if(onClick)
			onClick();
	}
};

/// the scrollback: the game's chat history, wrapped to the box, each line in its channel's colour, the newest at the bottom; the wheel goes back in it
class ChatLog : public CIntObject
{
	int scrollBack = 0; // lines up from the bottom
	int smallLine = 16; // the line height of the small font (the compact panel draws it closer)

public:
	explicit ChatLog(const Rect & area)
		: CIntObject(WHEEL, area.topLeft())
	{
		pos.w = area.w;
		pos.h = area.h;
	}

	void wheelScrolled(int distance) override
	{
		scrollBack = std::max(0, scrollBack + distance * 3);
		redraw();
	}

	void toBottom()
	{
		scrollBack = 0;
	}

	void setSmallLine(int height)
	{
		smallLine = height;
	}

	void showAll(Canvas & to) override
	{
		// the mockup's log is a flat dark panel: the texture does not show through (the Options' Glass lets the map through)
		to.drawColorBlended(pos, ColorRGBA(14, 9, 6, settings["general"]["chatGlass"].Bool() ? 150 : 242));
		to.drawBorder(pos, ColorRGBA(150, 122, 50, 255), 1);

		const auto & history = GAME->server().getGameChat().getChatHistory();
		const EFonts font = chatFont();
		const int lineHeight = font == FONT_BIG ? 26 : font == FONT_MEDIUM ? 20 : smallLine;
		const int rows = std::max(1, (pos.h - 8) / lineHeight);
		std::vector<std::pair<std::string, ColorRGBA>> lines;
		const size_t first = history.size() > 300 ? history.size() - 300 : 0;
		for(size_t i = first; i < history.size(); ++i)
		{
			const auto & entry = history[i];
			if((entry.channel == EChatChannel::TEAM && settings["general"]["chatMuteTeam"].Bool()) || (entry.channel == EChatChannel::SPECTATORS && settings["general"]["chatMuteSpectators"].Bool()) || (entry.senderName == "System" && settings["general"]["chatMuteSystem"].Bool()))
				continue;
			const std::string who = entry.channel == EChatChannel::ALL ? entry.senderName : "[" + channelName(entry.channel) + "] " + entry.senderName;
			const std::string text = (settings["general"]["chatTimestamps"].Bool() ? "[" + entry.dateFormatted + "] " : std::string()) + who + ": " + entry.messageText;
			// each player's lines in that player's colour; the game's own lines (System) a soft green (S100)
			const ColorRGBA colour = ChatConsoleWindow::lineColour(entry.sender, entry.senderName, entry.channel);
			for(const auto & part : CMessage::breakText(text, pos.w - 22, font))
				lines.emplace_back(part, colour);
		}
		const int total = static_cast<int>(lines.size());
		scrollBack = std::min(scrollBack, std::max(0, total - rows));
		const int last = total - scrollBack; // one past the last line shown
		const int begin = std::max(0, last - rows);
		for(int i = begin; i < last; ++i)
			to.drawText(pos.topLeft() + Point(8, 4 + (i - begin) * lineHeight), font, lines[i].second, ETextAlignment::TOPLEFT, lines[i].first);
		// the scroll bar: a track on the right and a thumb that shows where in the history the lines are (the wheel moves it)
		const Rect track(pos.x + pos.w - 9, pos.y + 3, 6, pos.h - 6);
		to.drawColorBlended(track, ColorRGBA(0, 0, 0, 120));
		if(total > rows)
		{
			const int thumbH = std::max(10, track.h * rows / total);
			const int thumbY = track.y + (track.h - thumbH) * begin / std::max(1, total - rows);
			to.drawColorBlended(Rect(track.x, thumbY, track.w, thumbH), ColorRGBA(212, 175, 55, 220));
		}
	}
};

/// the line being typed; Enter sends, Escape closes the window (the keys reach it through the window, as in the lobby's chat box)
class ChatLineInput : public CIntObject
{
	std::shared_ptr<TransparentFilledRectangle> background;
	std::shared_ptr<CTextInput> box;
	std::function<void(const std::string &)> onSend;
	std::function<void()> onTab;
	std::function<void()> onClose;

public:
	ChatLineInput(const Rect & area, std::function<void(const std::string &)> send, std::function<void()> tabCallback, std::function<void()> closeCallback)
		: CIntObject(KEYBOARD | TEXTINPUT, area.topLeft())
		, onSend(std::move(send))
		, onTab(std::move(tabCallback))
		, onClose(std::move(closeCallback))
	{
		OBJECT_CONSTRUCTION;
		pos.w = area.w;
		pos.h = area.h;
		background = std::make_shared<TransparentFilledRectangle>(Rect(0, 0, area.w, area.h), ColorRGBA(0, 0, 0, 170), GOLD, 1);
		box = std::make_shared<CTextInput>(Rect(4, 0, area.w - 8, area.h), EFonts::FONT_SMALL, ETextAlignment::CENTERLEFT, true);
		box->removeUsedEvents(KEYBOARD);
	}

	// while the line has the focus every key is the line's (the map's shortcuts would fire on the letters otherwise); a click outside it, or Escape, gives the keys back
	bool captureThisKey(EShortcut key) override
	{
		// only the keys the line uses: the dispatcher hands a pressed key's first captured shortcut to one object, so capturing every key lost Return (the letters do not reach the map while the text input is on)
		return box->hasFocus() && (key == EShortcut::GLOBAL_ACCEPT || key == EShortcut::GLOBAL_CANCEL || key == EShortcut::GLOBAL_BACKSPACE || key == EShortcut::GAME_ACTIVATE_CONSOLE);
	}

	void focusLine()
	{
		box->giveFocus();
	}

	void setWidth(int w)
	{
		pos.w = w;
		background->pos.w = w;
		box->pos.w = w - 8;
	}

	void unfocusLine()
	{
		if(box->hasFocus())
			box->removeFocus();
	}

	void setColour(const ColorRGBA & colour)
	{
		box->setColor(colour);
	}

	void keyPressed(EShortcut key) override
	{
		if(key == EShortcut::GLOBAL_ACCEPT)
		{
			if(!box->getText().empty())
			{
				const std::string text = box->getText();
				box->setText("");
				onSend(text);
			}
		}
		else if(key == EShortcut::GLOBAL_CANCEL)
		{
			// K, November 4th: "chat should close after a message is sent, or a player hits escape after clicking in there or after a message" (this replaces VCMI's and the beta 19.1 rule that Escape only gave the keys back)
			unfocusLine();
			onClose();
		}
		else if(key == EShortcut::GAME_ACTIVATE_CONSOLE)
		{
			if(box->getText().empty())
				onTab();
		}
		else
			box->keyPressed(key);
	}
};

Rect ChatConsoleWindow::panelArea = Rect();
Rect ChatConsoleWindow::chipArea = Rect();
bool ChatConsoleWindow::openAsked = false;
bool ChatConsoleWindow::toggleAsked = false;
EChatChannel ChatConsoleWindow::currentChannel = EChatChannel::ALL;

ChatConsoleWindow::ChatConsoleWindow(const Rect & area, bool compactPanel)
	: CIntObject(LCLICK | WHEEL | DRAG)
{
	OBJECT_CONSTRUCTION;
	compact = compactPanel;
	margin = 10; // the band (7 pixels) and a little air
	rowH = compact ? 20 : ROW_H;
	topY = compact ? 27 : 33;
	bottomPad = compact ? margin : 4;
	mapArea = area;
	// the remembered size (settings general.chatPanelW and chatPanelH, 0 is the default), kept inside the limits of this window
	const double savedW = settings["general"]["chatPanelW"].Float();
	const double savedH = settings["general"]["chatPanelH"].Float();
	const int w = compact ? COMPACT_W : clampWidth(savedW > 0 ? static_cast<int>(savedW) : WIN_W, area);
	const int h = compact ? COMPACT_H : clampHeight(savedH > 0 ? static_cast<int>(savedH) : WIN_H, area);
	// K, November 4th: "nestled into the bottom left corner, with the chat window's ui border perfectly/seamlessly overlapping with the first set of delineation/ui lines bounding the main adventure map window":
	// K, on the picture: "its too far to the left, its correct on the bottom": the left edge is the map window's own left edge (the frame's strip, 7 pixels, stays visible to its left), the bottom edge the map view's bottom (the frame's lines under it)
	// in a battle (no such frame) the compact panel is at the field's bottom left with a band on all four sides
	pos = Rect(area.x, area.y + area.h - h, w, h);
	background = std::make_shared<CFilledTexture>(ImagePath::builtin("DIBOXBCK"), Rect(0, 0, pos.w, pos.h));
	glass = std::make_shared<TransparentFilledRectangle>(Rect(0, 0, pos.w, pos.h), ColorRGBA(18, 10, 4, 120));

	// K, October 22nd mockup: the title sits a little down and to the right; Options and the x top right, their text inside their boxes
	const int buttonY = 9;
	const int buttonH = compact ? 16 : 20;
	title = std::make_shared<CLabel>(margin + 34, compact ? 17 : 22, FONT_MEDIUM, ETextAlignment::CENTER, Colors::YELLOW, LIBRARY->generaltexth->translate("vcmi.chat.window.title"));
	closeButton = std::make_shared<ChatButton>(Rect(pos.w - margin - 20, buttonY, 20, buttonH), "x", [this]() { closeAsked = true; });
	settingsButton = std::make_shared<ChatButton>(Rect(pos.w - margin - 20 - 4 - 64, buttonY, 64, buttonH), LIBRARY->generaltexth->translate("vcmi.chat.window.settings"), [this]() { setSettingsOpen(!settingsOpen); });

	log = std::make_shared<ChatLog>(Rect(margin, topY, pos.w - 2 * margin, pos.h - topY - bottomPad - rowH - 4));
	if(compact)
		log->setSmallLine(13);

	const int rowY = pos.h - bottomPad - rowH;
	const int toW = compact ? 106 : 108;
	toButton = std::make_shared<ChatButton>(Rect(margin, rowY, toW, rowH), "", [this]() { setListOpen(!listOpen); });
	for(int i = 0; i < 3; ++i)
	{
		const auto ch = static_cast<EChatChannel>(i);
		toItems.push_back(std::make_shared<ChatButton>(Rect(margin, rowY - (3 - i) * rowH, toW, rowH), channelName(ch), [this, ch]() { setChannel(ch); }));
	}
	input = std::make_shared<ChatLineInput>(Rect(margin + toW + 6, rowY, pos.w - 2 * margin - toW - 6, rowH), [this](const std::string & text) { send(text); }, [this]() { nextChannel(); }, [this]() { closeAsked = true; });

	visibleSettingRows = std::max(1, (pos.h - topY - bottomPad - 2) / 16);
	for(int i = 0; i < 12; ++i)
		settingsRows.push_back(std::make_shared<ChatButton>(Rect(margin + 20, topY + 2 + i * 16, pos.w - 2 * margin - 40, 15), "", [this, i]() { changeSetting(i); }));
	refreshSettingsRows();
	setSettingsOpen(false);
	applyBackground();

	// the channel the player last wrote to, if it is still offered
	setChannel(channelOffered(currentChannel) ? currentChannel : EChatChannel::ALL);
	setListOpen(false);
	panelArea = pos;
	GAME->server().getGameChat().setConsoleWindowOpen(true);
}

ChatConsoleWindow::~ChatConsoleWindow()
{
	panelArea = Rect();
	if(GAME && GAME->interface())
		GAME->server().getGameChat().setConsoleWindowOpen(false);
}

void ChatConsoleWindow::requestOpen()
{
	openAsked = true;
}

void ChatConsoleWindow::requestToggle()
{
	toggleAsked = true;
}

bool ChatConsoleWindow::coversPoint(const Point & point)
{
	return (panelArea.w > 0 && panelArea.isInside(point)) || (chipArea.w > 0 && chipArea.isInside(point));
}

void ChatConsoleWindow::focusInput()
{
	input->focusLine();
}

void ChatConsoleWindow::releaseFocus()
{
	input->unfocusLine();
}

bool ChatConsoleWindow::receiveEvent(const Point & position, int eventType) const
{
	// every click reaches the panel, so that a click on the map can give the keys back; the rest only where the panel is
	if(eventType == LCLICK)
		return true;
	return CIntObject::receiveEvent(position, eventType);
}

void ChatConsoleWindow::clickPressed(const Point & cursorPosition)
{
	resizing = false;
	if(!compact && pos.isInside(cursorPosition) && cursorPosition.x >= pos.right() - CORNER && cursorPosition.y < pos.y + CORNER)
	{
		resizing = true;
		pressPoint = cursorPosition;
		pressSize = Point(pos.w, pos.h);
		pressBottom = pos.bottom();
	}
	if(!pos.isInside(cursorPosition) && !(chipArea.w > 0 && chipArea.isInside(cursorPosition)))
		input->unfocusLine();
	else if(pos.isInside(cursorPosition) && !resizing && !settingsOpen)
		input->focusLine(); // K: "escape after clicking in there": a click on the panel's log gives the line the keys, so Escape closes the panel
}

void ChatConsoleWindow::clickReleased(const Point &)
{
	if(resizing)
	{
		resizing = false;
		saveSize();
	}
}

void ChatConsoleWindow::mouseDragged(const Point & cursorPosition, const Point &)
{
	if(!resizing)
		return;
	// the top right corner follows the mouse, the bottom left corner stays
	const int w = clampWidth(pressSize.x + cursorPosition.x - pressPoint.x, mapArea);
	const int h = clampHeight(pressSize.y - (cursorPosition.y - pressPoint.y), mapArea);
	if(w == pos.w && h == pos.h)
		return;
	moveTo(Point(pos.x, pressBottom - h));
	pos.w = w;
	pos.h = h;
	applyLayout();
	ENGINE->windows().totalRedraw();
}

void ChatConsoleWindow::applyLayout()
{
	background->pos.w = pos.w;
	background->pos.h = pos.h;
	glass->pos.w = pos.w;
	glass->pos.h = pos.h;
	closeButton->moveTo(pos.topLeft() + Point(pos.w - margin - 20, 9));
	settingsButton->moveTo(pos.topLeft() + Point(pos.w - margin - 20 - 4 - 64, 9));
	log->pos.w = pos.w - 2 * margin;
	log->pos.h = pos.h - topY - bottomPad - rowH - 4;
	const int rowY = pos.h - bottomPad - rowH;
	const int toW = 108;
	toButton->moveTo(pos.topLeft() + Point(margin, rowY));
	input->moveTo(pos.topLeft() + Point(margin + toW + 6, rowY));
	input->setWidth(pos.w - 2 * margin - toW - 6);
	for(auto & row : settingsRows)
		row->setWidth(pos.w - 2 * margin - 40);
	visibleSettingRows = std::max(1, (pos.h - topY - bottomPad - 2) / 16);
	settingsOffset = std::min(settingsOffset, std::max(0, static_cast<int>(settingsRows.size()) - visibleSettingRows));
	layoutSettingsRows();
	setListOpen(listOpen);
	panelArea = pos;
}

void ChatConsoleWindow::saveSize()
{
	Settings width = settings.write["general"]["chatPanelW"];
	width->Float() = pos.w;
	Settings height = settings.write["general"]["chatPanelH"];
	height->Float() = pos.h;
	ENGINE->windows().totalRedraw();
}

void ChatConsoleWindow::showAll(Canvas & to)
{
	CIntObject::showAll(to);
	if(settingsOpen && static_cast<int>(settingsRows.size()) > visibleSettingRows)
	{
		const Rect track(pos.x + pos.w - margin - 8, pos.y + topY + 2, 5, visibleSettingRows * 16);
		to.drawColorBlended(track, ColorRGBA(0, 0, 0, 120));
		const int total = static_cast<int>(settingsRows.size());
		const int thumbH = std::max(8, track.h * visibleSettingRows / total);
		const int thumbY = track.y + (track.h - thumbH) * settingsOffset / std::max(1, total - visibleSettingRows);
		to.drawColorBlended(Rect(track.x, thumbY, track.w, thumbH), ColorRGBA(212, 175, 55, 220));
	}
	// the corner to drag (not in a battle): three short diagonal strokes in the top right corner of the band
	if(!compact)
		for(int k = 2; k <= 8; k += 3)
			to.drawLine(Point(pos.right() - 1 - k, pos.y + 1), Point(pos.right() - 1, pos.y + 1 + k), ColorRGBA(255, 240, 160, 255), ColorRGBA(255, 240, 160, 255));
	// the To: mark: a filled triangle pointing down (the mockup's), at the right end of the To: button
	if(!toButton->isDisabled())
	{
		const Point tip(toButton->pos.x + toButton->pos.w - 12, toButton->pos.y + toButton->pos.h / 2 - 2);
		for(int row = 0; row < 4; ++row)
			to.drawColorBlended(Rect(tip.x + row, tip.y + row, 9 - 2 * row, 1), ColorRGBA(232, 204, 120, 255));
	}
	// the band: the screen frame's own pattern from the outside in (dark, gold, gold, the player's colour twice, gold, gold), 7 pixels; the left and bottom sides are the frame's lines already
	const ColorRGBA band = bandColour();
	const std::array<ColorRGBA, 7> pattern = {ColorRGBA(74, 49, 24), ColorRGBA(165, 140, 66), ColorRGBA(247, 231, 123), band, band, ColorRGBA(173, 140, 66), ColorRGBA(239, 214, 123)};
	for(int i = 0; i < 7; ++i)
	{
		to.drawColorBlended(Rect(compact ? pos.x + i : pos.x, pos.y + i, compact ? pos.w - 2 * i : pos.w - i, 1), pattern[i]); // the top side (it starts at the map window's left edge)
		to.drawColorBlended(Rect(pos.right() - 1 - i, pos.y + i, 1, pos.h - i - (compact ? i : 0)), pattern[i]); // the right side, down to the map view's bottom line
		if(compact)
		{
			to.drawColorBlended(Rect(pos.x + i, pos.y + i, 1, pos.h - 2 * i), pattern[i]); // the left
			to.drawColorBlended(Rect(pos.x + i, pos.bottom() - 1 - i, pos.w - 2 * i, 1), pattern[i]); // the bottom
		}
	}
}

void ChatConsoleWindow::nextChannel()
{
	auto next = channel;
	for(int i = 0; i < 3; ++i)
	{
		next = static_cast<EChatChannel>((static_cast<int>(next) + 1) % 3);
		if(channelOffered(next))
			break;
	}
	setChannel(next);
}

ColorRGBA ChatConsoleWindow::bandColour()
{
	if(GAME && GAME->interface() && GAME->interface()->playerID.isValidPlayer())
	{
		const ColorRGBA & c = graphics->playerColors[GAME->interface()->playerID.getNum()];
		return ColorRGBA(static_cast<uint8_t>(c.r * 7 / 10), static_cast<uint8_t>(c.g * 7 / 10), static_cast<uint8_t>(c.b * 7 / 10), 255);
	}
	return ColorRGBA(160, 0, 0, 255);
}

ColorRGBA ChatConsoleWindow::lineColour(PlayerColor sender, const std::string & senderName, EChatChannel channel)
{
	if(senderName == "System")
		return ColorRGBA(130, 215, 130);
	if(!sender.isValidPlayer() && !senderName.empty() && GAME->interface() && GAME->interface()->cb)
	{
		if(const auto * startInfo = GAME->interface()->cb->getStartInfo())
			for(const auto & [colour, player] : startInfo->playerInfos)
				if(player.name == senderName)
				{
					sender = colour;
					break;
				}
	}
	if(sender.isValidPlayer() && sender.getNum() < static_cast<int>(graphics->playerColors.size()))
		return graphics->playerColors[sender.getNum()];
	return channelColour(channel);
}

ColorRGBA ChatConsoleWindow::channelColour(EChatChannel channel)
{
	const int row = channel == EChatChannel::TEAM ? 1 : channel == EChatChannel::SPECTATORS ? 2 : 0;
	return PALETTE[std::clamp(static_cast<int>(settings["general"][colourKey(row)].Float()), 0, static_cast<int>(PALETTE.size()) - 1)];
}

void ChatConsoleWindow::setSettingsOpen(bool open)
{
	settingsOpen = open;
	// the Options page has the room of the To: row and the line, which are hidden while it is open (the rows overran them)
	if(open)
		input->unfocusLine();
	toButton->setEnabled(!open);
	input->setEnabled(!open);
	settingsOffset = 0;
	layoutSettingsRows();
	log->setEnabled(!open);
	if(open)
		setListOpen(false);
	redraw();
}

void ChatConsoleWindow::applyBackground()
{
	const bool isGlass = settings["general"]["chatGlass"].Bool();
	background->setEnabled(!isGlass);
	glass->setEnabled(isGlass);
}

// the rows of the Options page that fit under the title row, from settingsOffset; the wheel moves the offset
void ChatConsoleWindow::layoutSettingsRows()
{
	for(int i = 0; i < static_cast<int>(settingsRows.size()); ++i)
	{
		const bool visible = settingsOpen && i >= settingsOffset && i < settingsOffset + visibleSettingRows;
		settingsRows[i]->setEnabled(visible);
		if(visible)
			settingsRows[i]->moveTo(pos.topLeft() + Point(margin + 20, topY + 2 + (i - settingsOffset) * 16));
	}
	redraw();
}

void ChatConsoleWindow::wheelScrolled(int distance)
{
	if(!settingsOpen || distance == 0)
		return;
	const int most = std::max(0, static_cast<int>(settingsRows.size()) - visibleSettingRows);
	settingsOffset = std::clamp(settingsOffset + (distance > 0 ? -1 : 1), 0, most);
	layoutSettingsRows();
}

void ChatConsoleWindow::refreshSettingsRows()
{
	const auto & text = LIBRARY->generaltexth;
	const std::string on = text->translate("vcmi.chat.window.on");
	const std::string off = text->translate("vcmi.chat.window.off");
	settingsRows[0]->setText(text->translate("vcmi.chat.window.opacity") + " " + text->translate(settings["general"]["chatGlass"].Bool() ? "vcmi.chat.window.glass" : "vcmi.chat.window.opaque"));
	settingsRows[1]->setText(text->translate("vcmi.chat.window.timestamps") + " " + (settings["general"]["chatTimestamps"].Bool() ? on : off));
	settingsRows[2]->setText(text->translate("vcmi.chat.window.muteTeam") + " " + (settings["general"]["chatMuteTeam"].Bool() ? on : off));
	settingsRows[3]->setText(text->translate("vcmi.chat.window.muteSpectators") + " " + (settings["general"]["chatMuteSpectators"].Bool() ? on : off));
	for(int row = 0; row < 3; ++row)
	{
		const int index = std::clamp(static_cast<int>(settings["general"][colourKey(row)].Float()), 0, static_cast<int>(PALETTE.size()) - 1);
		const std::string label = text->translate(row == 0 ? "vcmi.chat.channel.all" : row == 1 ? "vcmi.chat.channel.team" : "vcmi.chat.channel.spectators");
		// S100: a player's lines are in that player's colour; these colour the typing line and the lines of spectators and observers
		settingsRows[4 + row]->setText(text->translate("vcmi.chat.window.colourFor") + " " + label + ": " + text->translate(PALETTE_NAMES[index]));
	}
	const double size = settings["general"]["chatFontSize"].Float();
	settingsRows[7]->setText(text->translate("vcmi.chat.window.fontSize") + " " + text->translate(size >= 2 ? "vcmi.chat.window.fontLarge" : size >= 1 ? "vcmi.chat.window.fontMedium" : "vcmi.chat.window.fontSmall"));
	const double fade = settings["general"]["chatFadeSeconds"].Float();
	settingsRows[8]->setText(text->translate("vcmi.chat.window.fade") + " " + (fade <= 0 ? text->translate("vcmi.chat.window.never") : std::to_string(static_cast<int>(fade)) + " s"));
	settingsRows[9]->setText(text->translate("vcmi.chat.window.muteSystem") + " " + (settings["general"]["chatMuteSystem"].Bool() ? on : off));
	settingsRows[10]->setText(text->translate("vcmi.chat.window.sound") + " " + (settings["general"]["chatSound"].Bool() ? on : off));
	settingsRows[11]->setText(text->translate("vcmi.chat.window.soundAI") + " " + (settings["general"]["chatSoundAI"].Bool() ? on : off));
}

void ChatConsoleWindow::changeSetting(int row)
{
	if(row == 0)
	{
		// the panel texture (opaque) or glass
		Settings entry = settings.write["general"]["chatGlass"];
		entry->Bool() = !entry->Bool();
		applyBackground();
	}
	else if(row >= 4 && row <= 6)
	{
		Settings entry = settings.write["general"][std::string(colourKey(row - 4))];
		entry->Float() = (static_cast<int>(entry->Float()) + 1) % static_cast<int>(PALETTE.size());
	}
	else if(row == 7)
	{
		Settings entry = settings.write["general"]["chatFontSize"];
		entry->Float() = entry->Float() >= 2 ? 0 : entry->Float() + 1;
	}
	else if(row == 8)
	{
		// 10 s, 30 s, never (0) and round again
		const double now = settings["general"]["chatFadeSeconds"].Float();
		const double next = now <= 0 ? 10 : now < 20 ? 30 : now < 45 ? 0 : 10;
		Settings entry = settings.write["general"]["chatFadeSeconds"];
		entry->Float() = next;
	}
	else
	{
		const char * key = row == 1 ? "chatTimestamps" : row == 2 ? "chatMuteTeam" : row == 3 ? "chatMuteSpectators" : row == 9 ? "chatMuteSystem" : row == 11 ? "chatSoundAI" : "chatSound";
		Settings entry = settings.write["general"][std::string(key)];
		entry->Bool() = !entry->Bool();
	}
	refreshSettingsRows();
	redraw();
}

void ChatConsoleWindow::setChannel(EChatChannel newChannel)
{
	channel = newChannel;
	currentChannel = newChannel;
	toButton->setText(LIBRARY->generaltexth->translate("vcmi.chat.window.to") + " " + channelName(channel));
	input->setColour(channelColour(channel));
	setListOpen(false);
	redraw();
}

void ChatConsoleWindow::setListOpen(bool open)
{
	listOpen = open;
	// the entries a player is offered, from the To: box upward
	const int rowY = pos.h - bottomPad - rowH;
	int shown = 0;
	for(int i = 0; i < 3; ++i)
	{
		const bool offered = channelOffered(static_cast<EChatChannel>(i));
		toItems[i]->setEnabled(open && offered);
		if(offered)
		{
			++shown;
			toItems[i]->moveTo(pos.topLeft() + Point(margin, rowY - shown * rowH));
		}
	}
	redraw();
}

void ChatConsoleWindow::send(const std::string & text)
{
	if(text.empty())
		return;
	if(text[0] == '/')
	{
		// a command, as in the overlay: some of them do not work from the GUI thread
		std::thread([text]()
		{
			setThreadName("processCommand");
			ClientCommandManager commandController;
			commandController.processCommand(text.substr(1), true);
		}).detach();
	}
	else
		GAME->server().getGameChat().sendMessageGameplay(text, channel);
	log->toBottom();
	// K, November 4th: the panel closes after a message is sent (the chip's next tick closes it; the next opening keeps the channel)
	closeAsked = true;
	redraw();
}
