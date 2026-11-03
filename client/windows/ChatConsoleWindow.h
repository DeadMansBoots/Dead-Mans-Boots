/*
 * ChatConsoleWindow.h, part of Dead Man's Boots
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "CWindowObject.h"

#include "../render/Colors.h"

#include "../../lib/DmbChatRouting.h"

class CTextInput;
class CLabel;
class ChatButton;
class ChatLog;
class ChatLineInput;

/// DMB (K, chat console, phase 1): the console as a window in the Heroes frame. The scrollback (every channel, each in its colour, the mouse wheel goes back in it), a To: choice (Everyone,
/// Team, Spectators) and the line being typed. Opens from the chip at the bottom left of the adventure map (ChatChip) or with the chat key; Enter sends, Escape closes. The lines come from
/// GameChatHandler's history, which the save keeps, so a loaded game shows its old chat. The older overlay (CInGameConsole) stays as the quick line.
class ChatConsoleWindow : public CWindowObject
{
	std::shared_ptr<CFilledTexture> background;
	std::shared_ptr<CLabel> title;
	std::shared_ptr<ChatButton> closeButton;
	std::shared_ptr<ChatButton> toButton;
	std::vector<std::shared_ptr<ChatButton>> toItems;
	std::shared_ptr<ChatLog> log;
	std::shared_ptr<ChatLineInput> input;
	// phase 2: the settings page (a button in the title row swaps it for the scrollback): opacity, timestamps, mute Team, mute Spectators
	std::shared_ptr<ChatButton> settingsButton;
	std::vector<std::shared_ptr<ChatButton>> settingsRows;
	EChatChannel channel = EChatChannel::ALL;
	bool listOpen = false;
	bool settingsOpen = false;

	void setSettingsOpen(bool open);
	void refreshSettingsRows();
	void changeSetting(int row);

	void setChannel(EChatChannel newChannel);
	void setListOpen(bool open);
	void send(const std::string & text);

public:
	ChatConsoleWindow();
	~ChatConsoleWindow() override;

	/// the colour a channel's lines are drawn in, from the settings general.chatColour* (an index into the palette); the quick overlay uses it too
	static ColorRGBA channelColour(EChatChannel channel);

	void showAll(Canvas & to) override;
	void keyPressed(EShortcut key) override;
	bool captureThisKey(EShortcut key) override;
};
