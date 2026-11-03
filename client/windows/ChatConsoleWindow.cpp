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
const int WIN_W = 520;
const int WIN_H = 380;
const int MARGIN = 14;
const int ROW_H = 24;
const ColorRGBA GOLD(212, 175, 55, 255);

ColorRGBA channelColour(EChatChannel channel)
{
	// the colours of the overlay: all green, team cyan, spectators grey (the settings panel of phase 2 makes them choosable)
	return channel == EChatChannel::TEAM ? ColorRGBA(100, 210, 255) : channel == EChatChannel::SPECTATORS ? ColorRGBA(200, 200, 200) : Colors::GREEN;
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
		const int lineHeight = 16;
		const int rows = std::max(1, (pos.h - 8) / lineHeight);
		std::vector<std::pair<std::string, EChatChannel>> lines;
		const size_t first = history.size() > 300 ? history.size() - 300 : 0;
		for(size_t i = first; i < history.size(); ++i)
		{
			const auto & entry = history[i];
			if((entry.channel == EChatChannel::TEAM && settings["general"]["chatMuteTeam"].Bool()) || (entry.channel == EChatChannel::SPECTATORS && settings["general"]["chatMuteSpectators"].Bool()))
				continue;
			const std::string who = entry.channel == EChatChannel::ALL ? entry.senderName : "[" + channelName(entry.channel) + "] " + entry.senderName;
			const std::string text = (settings["general"]["chatTimestamps"].Bool() ? "[" + entry.dateFormatted + "] " : std::string()) + who + ": " + entry.messageText;
			for(const auto & part : CMessage::breakText(text, pos.w - 16, FONT_SMALL))
				lines.emplace_back(part, entry.channel);
		}
		const int total = static_cast<int>(lines.size());
		scrollBack = std::min(scrollBack, std::max(0, total - rows));
		const int last = total - scrollBack; // one past the last line shown
		const int begin = std::max(0, last - rows);
		for(int i = begin; i < last; ++i)
			to.drawText(pos.topLeft() + Point(8, 4 + (i - begin) * lineHeight), FONT_SMALL, channelColour(lines[i].second), ETextAlignment::TOPLEFT, lines[i].first);
	}
};

/// the line being typed; Enter sends, Escape closes the window (the keys reach it through the window, as in the lobby's chat box)
class ChatLineInput : public CIntObject
{
	std::shared_ptr<TransparentFilledRectangle> background;
	std::shared_ptr<CTextInput> box;
	std::function<void(const std::string &)> onSend;
	std::function<void()> onClose;

public:
	ChatLineInput(const Rect & area, std::function<void(const std::string &)> send, std::function<void()> close)
		: CIntObject(KEYBOARD | TEXTINPUT, area.topLeft())
		, onSend(std::move(send))
		, onClose(std::move(close))
	{
		OBJECT_CONSTRUCTION;
		pos.w = area.w;
		pos.h = area.h;
		background = std::make_shared<TransparentFilledRectangle>(Rect(0, 0, area.w, area.h), ColorRGBA(0, 0, 0, 170), GOLD, 1);
		box = std::make_shared<CTextInput>(Rect(4, 0, area.w - 8, area.h), EFonts::FONT_SMALL, ETextAlignment::CENTERLEFT, true);
		box->removeUsedEvents(KEYBOARD);
	}

	bool captureThisKey(EShortcut key) override
	{
		return key == EShortcut::GLOBAL_ACCEPT || key == EShortcut::GLOBAL_CANCEL;
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
			onClose();
		else
			box->keyPressed(key);
	}
};

ChatConsoleWindow::ChatConsoleWindow()
	: CWindowObject(BORDERED)
{
	OBJECT_CONSTRUCTION;
	pos.w = WIN_W;
	pos.h = WIN_H;
	center();
	background = std::make_shared<CFilledTexture>(ImagePath::builtin("DIBOXBCK"), Rect(0, 0, pos.w, pos.h));

	// K, October 22nd mockup: the title sits a little down and to the right of the middle
	title = std::make_shared<CLabel>(pos.w / 2 + 4, 22, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, LIBRARY->generaltexth->translate("vcmi.chat.window.title"));
	// the x is centred in its box
	closeButton = std::make_shared<ChatButton>(Rect(pos.w - MARGIN - 24, 10, 24, 24), "x", [this]() { close(); });
	settingsButton = std::make_shared<ChatButton>(Rect(MARGIN, 10, 90, 24), LIBRARY->generaltexth->translate("vcmi.chat.window.settings"), [this]() { setSettingsOpen(!settingsOpen); });

	log = std::make_shared<ChatLog>(Rect(MARGIN, 44, pos.w - 2 * MARGIN, pos.h - 44 - 56));

	const int rowY = pos.h - MARGIN - ROW_H;
	const int toW = 150;
	toButton = std::make_shared<ChatButton>(Rect(MARGIN, rowY, toW, ROW_H), "", [this]() { setListOpen(!listOpen); });
	for(int i = 0; i < 3; ++i)
	{
		const auto ch = static_cast<EChatChannel>(i);
		toItems.push_back(std::make_shared<ChatButton>(Rect(MARGIN, rowY - (3 - i) * ROW_H, toW, ROW_H), channelName(ch), [this, ch]() { setChannel(ch); }));
	}
	input = std::make_shared<ChatLineInput>(Rect(MARGIN + toW + 8, rowY, pos.w - 2 * MARGIN - toW - 8, ROW_H), [this](const std::string & text) { send(text); }, [this]() { close(); });

	for(int i = 0; i < 4; ++i)
		settingsRows.push_back(std::make_shared<ChatButton>(Rect(pos.w / 2 - 130, 70 + i * 40, 260, 28), "", [this, i]() { changeSetting(i); }));
	refreshSettingsRows();
	setSettingsOpen(false);

	setChannel(EChatChannel::ALL);
	setListOpen(false);
	GAME->server().getGameChat().setConsoleWindowOpen(true);
	updateShadow();
}

ChatConsoleWindow::~ChatConsoleWindow()
{
	if(GAME && GAME->interface())
		GAME->server().getGameChat().setConsoleWindowOpen(false);
}

void ChatConsoleWindow::showAll(Canvas & to)
{
	CIntObject::showAll(to);
	CMessage::drawBorder(PlayerColor(GAME->interface()->playerID), to, pos.w + 28, pos.h + 29, pos.x - 14, pos.y - 15);
}

bool ChatConsoleWindow::captureThisKey(EShortcut key)
{
	return key == EShortcut::GLOBAL_CANCEL;
}

void ChatConsoleWindow::keyPressed(EShortcut key)
{
	if(key == EShortcut::GLOBAL_CANCEL)
		close();
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
	toButton->setText(LIBRARY->generaltexth->translate("vcmi.chat.window.to") + " " + channelName(channel) + " v");
	setListOpen(false);
	redraw();
}

void ChatConsoleWindow::setListOpen(bool open)
{
	listOpen = open;
	for(auto & item : toItems)
		item->setEnabled(open);
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
