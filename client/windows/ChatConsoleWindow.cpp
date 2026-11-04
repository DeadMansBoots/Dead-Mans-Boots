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
#include "../../lib/CThreadHelper.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/texts/TextOperations.h"

namespace
{
const int WIN_W = ChatConsoleWindow::PANEL_W;
const int WIN_H = ChatConsoleWindow::PANEL_H;
const int MARGIN = 12;
const int ROW_H = 22;
const ColorRGBA GOLD(212, 175, 55, 255);

// the colours a channel can be set to (settings general.chatColour*: an index), none of them an orange, red, blue, tan, purple, teal or pink of the players
const std::array<ColorRGBA, 6> PALETTE = {ColorRGBA(0, 255, 0), ColorRGBA(100, 210, 255), ColorRGBA(200, 200, 200), ColorRGBA(255, 255, 255), ColorRGBA(255, 255, 0), ColorRGBA(210, 180, 255)};
const std::array<const char *, 6> PALETTE_NAMES = {"vcmi.chat.colour.green", "vcmi.chat.colour.cyan", "vcmi.chat.colour.grey", "vcmi.chat.colour.white", "vcmi.chat.colour.yellow", "vcmi.chat.colour.lavender"};

const char * colourKey(int row)
{
	return row == 0 ? "chatColourAll" : row == 1 ? "chatColourTeam" : "chatColourSpectators";
}

EFonts chatFont()
{
	return settings["general"]["chatFontSize"].Float() >= 1 ? FONT_MEDIUM : FONT_SMALL;
}

// Team is offered only to a player who has an ally (the team has more than one player)
bool teamAvailable()
{
	if(!GAME || !GAME->interface() || !GAME->interface()->cb)
		return true;
	const auto * team = GAME->interface()->cb->getPlayerTeam(GAME->interface()->playerID);
	return !team || team->players.size() > 1;
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

	void showAll(Canvas & to) override
	{
		to.drawColorBlended(pos, ColorRGBA(0, 0, 0, static_cast<uint8_t>(std::clamp(settings["general"]["chatOpacity"].Float() * 2.55, 0.0, 255.0))));
		to.drawBorder(pos, ColorRGBA(60, 50, 20, 255), 1);

		const auto & history = GAME->server().getGameChat().getChatHistory();
		const EFonts font = chatFont();
		const int lineHeight = font == FONT_MEDIUM ? 20 : 16;
		const int rows = std::max(1, (pos.h - 8) / lineHeight);
		std::vector<std::pair<std::string, ColorRGBA>> lines;
		const size_t first = history.size() > 300 ? history.size() - 300 : 0;
		for(size_t i = first; i < history.size(); ++i)
		{
			const auto & entry = history[i];
			if((entry.channel == EChatChannel::TEAM && settings["general"]["chatMuteTeam"].Bool()) || (entry.channel == EChatChannel::SPECTATORS && settings["general"]["chatMuteSpectators"].Bool()))
				continue;
			const std::string who = entry.channel == EChatChannel::ALL ? entry.senderName : "[" + channelName(entry.channel) + "] " + entry.senderName;
			const std::string text = (settings["general"]["chatTimestamps"].Bool() ? "[" + entry.dateFormatted + "] " : std::string()) + who + ": " + entry.messageText;
			// the game's own lines (System) are a soft green, apart from the three channels
			const ColorRGBA colour = entry.senderName == "System" ? ColorRGBA(130, 215, 130) : ChatConsoleWindow::channelColour(entry.channel);
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
		const Rect track(pos.x + pos.w - 8, pos.y + 3, 5, pos.h - 6);
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

public:
	ChatLineInput(const Rect & area, std::function<void(const std::string &)> send, std::function<void()> tabCallback)
		: CIntObject(KEYBOARD | TEXTINPUT, area.topLeft())
		, onSend(std::move(send))
		, onTab(std::move(tabCallback))
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
			unfocusLine();
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

ChatConsoleWindow::ChatConsoleWindow(const Rect & area)
	: CIntObject(LCLICK)
{
	OBJECT_CONSTRUCTION;
	// bottom left of the map view, over the chip (the chip is 22 high with 8 below it)
	pos = Rect(area.x + 8, area.y + area.h - 22 - 8 - 4 - WIN_H, WIN_W, WIN_H);
	background = std::make_shared<CFilledTexture>(ImagePath::builtin("DIBOXBCK"), Rect(0, 0, pos.w, pos.h));

	// K, October 22nd mockup: the title sits a little down and to the right; Options and the x top right, their text inside their boxes
	title = std::make_shared<CLabel>(MARGIN + 28, 24, FONT_MEDIUM, ETextAlignment::CENTER, Colors::YELLOW, LIBRARY->generaltexth->translate("vcmi.chat.window.title"));
	closeButton = std::make_shared<ChatButton>(Rect(pos.w - MARGIN - 20, 12, 20, 20), "x", [this]() { closeAsked = true; });
	settingsButton = std::make_shared<ChatButton>(Rect(pos.w - MARGIN - 20 - 4 - 64, 12, 64, 20), LIBRARY->generaltexth->translate("vcmi.chat.window.settings"), [this]() { setSettingsOpen(!settingsOpen); });

	log = std::make_shared<ChatLog>(Rect(MARGIN, 38, pos.w - 2 * MARGIN, pos.h - 38 - MARGIN - ROW_H - 4));

	const int rowY = pos.h - MARGIN - ROW_H;
	const int toW = 108;
	toButton = std::make_shared<ChatButton>(Rect(MARGIN, rowY, toW, ROW_H), "", [this]() { setListOpen(!listOpen); });
	for(int i = 0; i < 3; ++i)
	{
		const auto ch = static_cast<EChatChannel>(i);
		toItems.push_back(std::make_shared<ChatButton>(Rect(MARGIN, rowY - (3 - i) * ROW_H, toW, ROW_H), channelName(ch), [this, ch]() { setChannel(ch); }));
	}
	input = std::make_shared<ChatLineInput>(Rect(MARGIN + toW + 6, rowY, pos.w - 2 * MARGIN - toW - 6, ROW_H), [this](const std::string & text) { send(text); }, [this]() { nextChannel(); });

	for(int i = 0; i < 9; ++i)
		settingsRows.push_back(std::make_shared<ChatButton>(Rect(MARGIN + 20, 40 + i * 17, pos.w - 2 * MARGIN - 40, 16), "", [this, i]() { changeSetting(i); }));
	refreshSettingsRows();
	setSettingsOpen(false);

	setChannel(EChatChannel::ALL);
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

bool ChatConsoleWindow::receiveEvent(const Point & position, int eventType) const
{
	// every click reaches the panel, so that a click on the map can give the keys back; the rest only where the panel is
	if(eventType == LCLICK)
		return true;
	return CIntObject::receiveEvent(position, eventType);
}

void ChatConsoleWindow::clickPressed(const Point & cursorPosition)
{
	if(!pos.isInside(cursorPosition) && !(chipArea.w > 0 && chipArea.isInside(cursorPosition)))
		input->unfocusLine();
}

void ChatConsoleWindow::showAll(Canvas & to)
{
	CIntObject::showAll(to);
	CMessage::drawBorder(PlayerColor(GAME->interface()->playerID), to, pos.w, pos.h, pos.x, pos.y);
}

void ChatConsoleWindow::nextChannel()
{
	auto next = static_cast<EChatChannel>((static_cast<int>(channel) + 1) % 3);
	if(next == EChatChannel::TEAM && !teamAvailable())
		next = EChatChannel::SPECTATORS;
	setChannel(next);
}

ColorRGBA ChatConsoleWindow::channelColour(EChatChannel channel)
{
	const int row = channel == EChatChannel::TEAM ? 1 : channel == EChatChannel::SPECTATORS ? 2 : 0;
	return PALETTE[std::clamp(static_cast<int>(settings["general"][colourKey(row)].Float()), 0, static_cast<int>(PALETTE.size()) - 1)];
}

void ChatConsoleWindow::setSettingsOpen(bool open)
{
	settingsOpen = open;
	for(auto & row : settingsRows)
		row->setEnabled(open);
	log->setEnabled(!open);
	if(open)
		setListOpen(false);
	redraw();
}

void ChatConsoleWindow::refreshSettingsRows()
{
	const auto & text = LIBRARY->generaltexth;
	const std::string on = text->translate("vcmi.chat.window.on");
	const std::string off = text->translate("vcmi.chat.window.off");
	settingsRows[0]->setText(text->translate("vcmi.chat.window.opacity") + " " + std::to_string(static_cast<int>(settings["general"]["chatOpacity"].Float())) + "%");
	settingsRows[1]->setText(text->translate("vcmi.chat.window.timestamps") + " " + (settings["general"]["chatTimestamps"].Bool() ? on : off));
	settingsRows[2]->setText(text->translate("vcmi.chat.window.muteTeam") + " " + (settings["general"]["chatMuteTeam"].Bool() ? on : off));
	settingsRows[3]->setText(text->translate("vcmi.chat.window.muteSpectators") + " " + (settings["general"]["chatMuteSpectators"].Bool() ? on : off));
	for(int row = 0; row < 3; ++row)
	{
		const int index = std::clamp(static_cast<int>(settings["general"][colourKey(row)].Float()), 0, static_cast<int>(PALETTE.size()) - 1);
		const std::string label = text->translate(row == 0 ? "vcmi.chat.channel.all" : row == 1 ? "vcmi.chat.channel.team" : "vcmi.chat.channel.spectators");
		settingsRows[4 + row]->setText(label + " " + text->translate("vcmi.chat.window.colour") + " " + text->translate(PALETTE_NAMES[index]));
	}
	settingsRows[7]->setText(text->translate("vcmi.chat.window.fontSize") + " " + text->translate(settings["general"]["chatFontSize"].Float() >= 1 ? "vcmi.chat.window.fontMedium" : "vcmi.chat.window.fontSmall"));
	settingsRows[8]->setText(text->translate("vcmi.chat.window.fade") + " " + std::to_string(static_cast<int>(settings["general"]["chatFadeSeconds"].Float())) + " s");
}

void ChatConsoleWindow::changeSetting(int row)
{
	if(row == 0)
	{
		// 15, 30, 45, 60, 80, 100 and round again
		const double now = settings["general"]["chatOpacity"].Float();
		const std::array<double, 6> steps = {15, 30, 45, 60, 80, 100};
		double next = steps[0];
		for(double step : steps)
			if(step > now + 0.5)
			{
				next = step;
				break;
			}
		Settings entry = settings.write["general"]["chatOpacity"];
		entry->Float() = next;
	}
	else if(row >= 4 && row <= 6)
	{
		Settings entry = settings.write["general"][std::string(colourKey(row - 4))];
		entry->Float() = (static_cast<int>(entry->Float()) + 1) % static_cast<int>(PALETTE.size());
	}
	else if(row == 7)
	{
		Settings entry = settings.write["general"]["chatFontSize"];
		entry->Float() = entry->Float() >= 1 ? 0 : 1;
	}
	else if(row == 8)
	{
		// 5, 10, 20, 30, 60 seconds and round again
		const double now = settings["general"]["chatFadeSeconds"].Float();
		const std::array<double, 5> steps = {5, 10, 20, 30, 60};
		double next = steps[0];
		for(double step : steps)
			if(step > now + 0.5)
			{
				next = step;
				break;
			}
		Settings entry = settings.write["general"]["chatFadeSeconds"];
		entry->Float() = next;
	}
	else
	{
		const char * key = row == 1 ? "chatTimestamps" : row == 2 ? "chatMuteTeam" : "chatMuteSpectators";
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
	toButton->setText(LIBRARY->generaltexth->translate("vcmi.chat.window.to") + " " + channelName(channel) + " v");
	input->setColour(channelColour(channel));
	setListOpen(false);
	redraw();
}

void ChatConsoleWindow::setListOpen(bool open)
{
	listOpen = open;
	// the entries a player is offered, from the To: box upward
	const int rowY = pos.h - MARGIN - ROW_H;
	int shown = 0;
	for(int i = 0; i < 3; ++i)
	{
		const bool offered = i != static_cast<int>(EChatChannel::TEAM) || teamAvailable();
		toItems[i]->setEnabled(open && offered);
		if(offered)
		{
			++shown;
			toItems[i]->moveTo(pos.topLeft() + Point(MARGIN, rowY - shown * ROW_H));
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
	redraw();
}
