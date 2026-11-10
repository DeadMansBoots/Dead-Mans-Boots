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
#include "../lib/VCMIDirs.h"
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
	aiTakenSeats.clear();
}

JsonNode GameChatHandler::toJson(bool allChannels) const
{
	JsonNode result;
	// the player's local state goes into the game state every client and the host's save receive: only the Everyone and System lines are put in it, so that a Team or Spectators line is never readable outside its audience (the server decides who receives a line)
	std::vector<const GameChatMessage *> kept;
	for(const auto & entry : chatHistory)
		if(allChannels || entry.channel == EChatChannel::ALL)
			kept.push_back(&entry);
	const size_t first = kept.size() > 2000 ? kept.size() - 2000 : 0;
	for(size_t i = first; i < kept.size(); ++i)
	{
		JsonNode line;
		line["s"].String() = kept[i]->senderName;
		line["t"].String() = kept[i]->messageText;
		line["d"].String() = kept[i]->dateFormatted;
		line["c"].Integer() = static_cast<int>(kept[i]->channel);
		// DMB (S100): the sender's player, so a reloaded game shows the same colours; a version before 19.4 reads past the key
		if(kept[i]->sender.isValidPlayer())
			line["p"].Integer() = kept[i]->sender.getNum();
		result.Vector().push_back(line);
	}
	if(!allChannels)
		logGlobal->info("Chat saved with the game: %d line(s), the last one \"%s\"", result.Vector().size(), result.Vector().empty() ? std::string() : result.Vector().back()["t"].String());
	return result;
}

namespace
{
boost::filesystem::path chatSidecarPath(const std::string & saveName)
{
	std::string name = saveName;
	const std::string extension = ".vsgm1";
	if(name.size() > extension.size() && name.compare(name.size() - extension.size(), extension.size(), extension) == 0)
		name.erase(name.size() - extension.size());
	boost::filesystem::path path(name);
	if(path.is_relative())
		path = VCMIDirs::get().userDataPath() / path;
	path += ".chat";
	return path;
}
}

void GameChatHandler::writeSidecar(const std::string & saveName) const
{
	try
	{
		const auto path = chatSidecarPath(saveName);
		boost::filesystem::create_directories(path.parent_path());
		const std::string text = toJson(true).toString();
		std::ofstream file(path.string(), std::ios::binary | std::ios::trunc);
		file << text;
		logGlobal->info("Chat file written: %s (%d byte(s))", path.string(), text.size());
	}
	catch(const std::exception & e)
	{
		logGlobal->warn("Chat file not written for %s: %s", saveName, e.what());
	}
}

bool GameChatHandler::readSidecar()
{
	if(!chatHistory.empty() || !GAME || !GAME->server().si)
		return false;
	try
	{
		const auto path = chatSidecarPath(GAME->server().si->mapname);
		logGlobal->info("Chat file looked for: %s", path.string());
		if(!boost::filesystem::exists(path))
			return false;
		std::ifstream file(path.string(), std::ios::binary);
		const std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		const JsonNode node(reinterpret_cast<const std::byte *>(data.data()), data.size(), path.string());
		fromJson(node);
		return !chatHistory.empty();
	}
	catch(const std::exception & e)
	{
		logGlobal->warn("Chat file not read: %s", e.what());
		return false;
	}
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
		if(line["p"].isNumber())
			message.sender = PlayerColor(static_cast<int>(line["p"].Integer()));
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

	chatHistory.push_back({playerName, messageText, timeFormatted, channel, sender});
	if(!consoleWindowOpen)
		unreadLines++;

	// DMB (S072, K 11-07: "omniai's messages don't make the heroes 3 message sound...that's so annoying getting spammed constantly with
	// them", then "should have the ability to toggle sound for ai entries in the chat (off by default)"): a line from a seat an AI plays
	// chimes only when general.chatSoundAI is on (the chat window's Options); the text always stays. A human's line keeps the stock sound
	bool fromAI = false;
	if(sender.isValidPlayer())
	{
		const auto & seats = GAME->interface()->cb->getStartInfo()->playerInfos;
		const auto seat = seats.find(sender);
		fromAI = (seat != seats.end() && seat->second.isControlledByAI()) || aiTakenSeats.count(sender); // the Reviewer 10:57: a dropped human's seat played by an AI counts as AI
	}
	if(fromAI && !settings["general"]["chatSoundAI"].Bool())
		GAME->interface()->cingconsole->addMessageSilent(timeFormatted, playerName, messageText, channel, sender);
	else
		GAME->interface()->cingconsole->addMessage(timeFormatted, playerName, messageText, channel, sender);
}

void GameChatHandler::onNewSystemMessageReceived(const std::string & messageText)
{
	chatHistory.push_back({"System", messageText, TextOperations::getCurrentFormattedTimeLocal()});

	if(GAME->interface() && !settings["session"]["hideSystemMessages"].Bool())
		GAME->interface()->cingconsole->addMessage(TextOperations::getCurrentFormattedTimeLocal(), "System", messageText);
}
