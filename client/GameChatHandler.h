/*
 * GameChatHandler.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../lib/constants/EntityIdentifiers.h"
#include "../lib/DmbChatRouting.h"
#include "../lib/json/JsonNode.h"

struct GameChatMessage
{
	std::string senderName;
	std::string messageText;
	std::string dateFormatted;
	EChatChannel channel = EChatChannel::ALL; // DMB: the channel the line arrived on
	/// DMB (K, November 4th: "the player chats should be colored with their player color by default per individual"): the sender's player, whose colour the line is drawn in;
	/// none for System, spectator and observer lines, and for lines saved by a version before 19.4 (their colour then comes from the sender's name)
	PlayerColor sender = PlayerColor::CANNOT_DETERMINE;
};

/// Class that manages game chat for current game match
/// Used for all matches - singleplayer, local multiplayer, online multiplayer
class GameChatHandler : boost::noncopyable
{
	std::vector<GameChatMessage> chatHistory;
	int unreadLines = 0;
	bool consoleWindowOpen = false;
public:
	/// DMB (chat console, phase 1): lines that arrived from the game while the console window was closed; the chip on the adventure map shows the count
	int getUnreadLines() const { return unreadLines; }
	/// the console window sets this while it is open (its lines are read as they come) and clears the count on opening
	void setConsoleWindowOpen(bool open) { consoleWindowOpen = open; if(open) unreadLines = 0; }

	/// Returns all message history for current match
	const std::vector<GameChatMessage> & getChatHistory() const;

	/// Erases any local state, must be called when client disconnects from match server
	void resetMatchState();

	/// DMB (K, November 1st: the console's scrollback survives a save): the history as JSON, the last 2000 lines, kept in the player's local state that stock puts in the save
	JsonNode toJson(bool allChannels = false) const;

	/// DMB (K, chat saved with saves): the whole history, every channel this player was in, goes to a file next to the save on this PC (<save>.chat), not into the game state that every client and the host's save receive
	void writeSidecar(const std::string & saveName) const;
	/// reads the file of the game that was loaded (the lobby state's map name is the save's name); false when there is none
	bool readSidecar();
	/// a loaded game's history, taken when this session has no lines of its own yet
	void fromJson(const JsonNode & node);

	/// Must be called when local player sends new message into chat from gameplay mode (adventure map)
	/// DMB: on a channel; only the all channel is also given to the global lobby (a team or spectator line must not leave the game)
	void sendMessageGameplay(const std::string & messageText, EChatChannel channel = EChatChannel::ALL);

	/// Must be called when local player sends new message into chat from pregame mode (match lobby)
	void sendMessageLobby(const std::string & senderName, const std::string & messageText);

	/// Must be called when client receives new chat message from server
	void onNewLobbyMessageReceived(const std::string & senderName, const std::string & messageText);

	/// Must be called when client receives new chat message from server
	void onNewGameMessageReceived(PlayerColor sender, const std::string & messageText, EChatChannel channel = EChatChannel::ALL);

	/// Must be called when client receives new message from "System" sender
	void onNewSystemMessageReceived(const std::string & messageText);
};
