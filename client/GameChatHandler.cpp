/*
 * GameChatHandler.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "GameChatHandler.h"
#include "GameInstance.h"
#include "CServerHandler.h"
#include "CPlayerInterface.h"
#include "PlayerLocalState.h"
#include "globalLobby/GlobalLobbyClient.h"
#include "lobby/CLobbyScreen.h"

#include "adventureMap/CInGameConsole.h"

#include "../lib/callback/CCallback.h"
#include "../lib/networkPacks/PacksForLobby.h"
#include "../lib/mapObjects/army/CArmedInstance.h"
#include "../lib/CConfigHandler.h"
#include "../lib/GameLibrary.h"
#include "../lib/texts/CGeneralTextHandler.h"
#include "../lib/texts/TextOperations.h"

const std::vector<GameChatMessage> & GameChatHandler::getChatHistory() const
{
	return chatHistory;
}

void GameChatHandler::resetMatchState()
{
	chatHistory.clear();
	unreadLines = 0;
	consoleWindowOpen = false;
}

JsonNode GameChatHandler::toJson() const
{
	JsonNode result;
	// the player's local state goes into the game state every client and the host's save receive: only the Everyone and System lines are put in it, so that a Team or Spectators line is never readable outside its audience (the server decides who receives a line)
	std::vector<const GameChatMessage *> kept;
	for(const auto & entry : chatHistory)
		if(entry.channel == EChatChannel::ALL)
			kept.push_back(&entry);
	const size_t first = kept.size() > 2000 ? kept.size() - 2000 : 0;
	for(size_t i = first; i < kept.size(); ++i)
	{
		JsonNode line;
		line["s"].String() = kept[i]->senderName;
		line["t"].String() = kept[i]->messageText;
		line["d"].String() = kept[i]->dateFormatted;
		line["c"].Integer() = static_cast<int>(kept[i]->channel);
		result.Vector().push_back(line);
	}
	logGlobal->info("Chat saved with the game: %d line(s), the last one \"%s\"", result.Vector().size(), result.Vector().empty() ? std::string() : result.Vector().back()["t"].String());
	return result;
}

void GameChatHandler::fromJson(const JsonNode & node)
{
	if(!chatHistory.empty() || !node.isVector())
		return;
	for(const auto & line : node.Vector())
	{
		GameChatMessage message;
		message.senderName = line["s"].String();
		message.messageText = line["t"].String();
		message.dateFormatted = line["d"].String();
		message.channel = static_cast<EChatChannel>(line["c"].Integer());
		chatHistory.push_back(std::move(message));
	}
	logGlobal->info("Chat restored from the save: %d line(s), the last one \"%s\"", chatHistory.size(), chatHistory.empty() ? std::string() : chatHistory.back().messageText);
}

void GameChatHandler::sendMessageGameplay(const std::string & messageText, EChatChannel channel)
{
	GAME->interface()->cb->sendChatMessage(messageText, GAME->interface()->localState->getCurrentArmy(), channel);
	if(channel == EChatChannel::ALL)
		GAME->server().getGlobalLobby().sendMatchChatMessage(messageText);
}

void GameChatHandler::sendMessageLobby(const std::string & senderName, const std::string & messageText)
{
	LobbyChatMessage lcm;
	MetaString txt;
	txt.appendRawString(messageText);
	lcm.message = txt;
	lcm.playerName = senderName;
	GAME->server().sendLobbyPack(lcm);
	GAME->server().getGlobalLobby().sendMatchChatMessage(messageText);
}

void GameChatHandler::onNewLobbyMessageReceived(const std::string & senderName, const std::string & messageText)
{
	if (!SEL)
	{
		logGlobal->debug("Received chat message for lobby but lobby not yet exists!");
		return;
	}

	auto * lobby = dynamic_cast<CLobbyScreen*>(SEL);

	// FIXME: when can this happen?
	assert(lobby);
	assert(lobby->card);

	if(lobby && lobby->card)
	{
		MetaString formatted = MetaString::createFromRawString("[%s] %s: %s");
		formatted.replaceRawString(TextOperations::getCurrentFormattedTimeLocal());
		formatted.replaceRawString(senderName);
		formatted.replaceRawString(messageText);

		lobby->card->chat->addNewMessage(formatted.toString());
		if (!lobby->card->showChat)
				lobby->toggleChat();
	}

	chatHistory.push_back({senderName, messageText, TextOperations::getCurrentFormattedTimeLocal()});
}

void GameChatHandler::onNewGameMessageReceived(PlayerColor sender, const std::string & messageText, EChatChannel channel)
{

	std::string timeFormatted = TextOperations::getCurrentFormattedTimeLocal();
	std::string playerName = "<UNKNOWN>";

	if (sender.isValidPlayer())
		playerName = GAME->interface()->cb->getStartInfo()->playerInfos.at(sender).name;

	if (sender.isSpectator())
		playerName = LIBRARY->generaltexth->translate("vcmi.lobby.login.spectator");

	chatHistory.push_back({playerName, messageText, timeFormatted, channel});
	if(!consoleWindowOpen)
		unreadLines++;

	GAME->interface()->cingconsole->addMessage(timeFormatted, playerName, messageText, channel);
}

void GameChatHandler::onNewSystemMessageReceived(const std::string & messageText)
{
	chatHistory.push_back({"System", messageText, TextOperations::getCurrentFormattedTimeLocal()});

	if(GAME->interface() && !settings["session"]["hideSystemMessages"].Bool())
		GAME->interface()->cingconsole->addMessage(TextOperations::getCurrentFormattedTimeLocal(), "System", messageText);
}
