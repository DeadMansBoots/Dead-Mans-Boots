/*
 * ChatConsoleWindow.h, part of Dead Man's Boots
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "../gui/CIntObject.h"

#include "../render/Colors.h"

#include "../../lib/DmbChatRouting.h"

class CFilledTexture;
class TransparentFilledRectangle;
class CTextInput;
class CLabel;
class ChatButton;
class ChatLog;
class ChatLineInput;

/// DMB (K, chat console, beta 20 redesign): the console as a small panel on the adventure map, bottom left above the chip, not a window. Nothing about it is modal: the map behind it keeps
/// redrawing and taking clicks (outside the panel), and the panel stays open until the player closes it. The scrollback (every channel, each in its colour, the mouse wheel goes back in it),
/// a To: choice (Everyone, Team, Spectators) and the line being typed, which takes the keys while it has the focus. It is a child of the chip (ChatChip), which opens and closes it.
/// The lines come from GameChatHandler's history, which the save keeps. The older overlay (CInGameConsole) stays as the short lines that fade on the map.
class ChatConsoleWindow : public CIntObject
{
	std::shared_ptr<CFilledTexture> background;
	std::shared_ptr<TransparentFilledRectangle> glass; // the background when the Options say Glass: the map shows through
	std::shared_ptr<CLabel> title;
	std::shared_ptr<ChatButton> closeButton;
	std::shared_ptr<ChatButton> toButton;
	std::vector<std::shared_ptr<ChatButton>> toItems;
	std::shared_ptr<ChatLog> log;
	std::shared_ptr<ChatLineInput> input;
	// the settings page (a button in the title row swaps it for the scrollback): opacity, timestamps, mutes, colours, text size, fade
	std::shared_ptr<ChatButton> settingsButton;
	std::vector<std::shared_ptr<ChatButton>> settingsRows;
	EChatChannel channel = EChatChannel::ALL;
	bool listOpen = false;
	bool settingsOpen = false;
	bool closeAsked = false;
	// the compact panel of a battle (K: "smaller, and not freeze the gamespace"): 300 by 132, the log, the To: row and the line; the Options rows scroll
	bool compact = false;
	int margin = 12;
	int rowH = 22;
	int topY = 38;
	int bottomPad = 4; // the gap under the To: row: the panel's bottom edge is the map window's frame line, where it is nestled (the battle's compact panel has a band of its own there)
	int settingsOffset = 0;
	int visibleSettingRows = 11;

	void setSettingsOpen(bool open);
	void refreshSettingsRows();
	void layoutSettingsRows();
	void applyBackground();
	void changeSetting(int row);

	void setChannel(EChatChannel newChannel);
	void setListOpen(bool open);
	void send(const std::string & text);
	void nextChannel();

public:
	/// `area`: the map view's rectangle; the panel sits at its bottom left, above the chip
	explicit ChatConsoleWindow(const Rect & area, bool compactPanel = false);
	~ChatConsoleWindow() override;

	static constexpr int PANEL_W = 380;
	static constexpr int PANEL_H = 232;
	static constexpr int COMPACT_W = 300;
	static constexpr int COMPACT_H = 132;

	/// the colour a channel's lines are drawn in, from the settings general.chatColour* (an index into the palette); the quick overlay uses it too
	static ColorRGBA channelColour(EChatChannel channel);

	/// the player's colour for the bands of the chip and the panel (the mockup's red is the red player's); a dull red for a watcher
	static ColorRGBA bandColour();

	/// the chat key and the chip ask for the panel; the chip (which owns it) does it on its next tick
	static void requestOpen();
	static void requestToggle();
	/// a point the chat panel or the chip covers: the map ignores clicks, hovers and the wheel there
	static bool coversPoint(const Point & point);
	/// where the panel and the chip are now (empty rectangles when not shown); set by the chip
	static Rect panelArea;
	static Rect chipArea;
	static bool openAsked;
	static bool toggleAsked;
	/// the channel the next line goes to, for the chip's label
	static EChatChannel currentChannel;

	bool closeRequested() const { return closeAsked; }
	void focusInput();
	void releaseFocus();
	void showAll(Canvas & to) override;
	bool receiveEvent(const Point & position, int eventType) const override;
	void clickPressed(const Point & cursorPosition) override;
	void wheelScrolled(int distance) override;
};
