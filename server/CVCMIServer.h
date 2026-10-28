/*
 * CVCMIServer.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "IGameServer.h"

#include "../lib/network/NetworkInterface.h"
#include "../lib/StartInfo.h"
#include "../lib/DmbChatRouting.h"

VCMI_LIB_NAMESPACE_BEGIN

class CMapInfo;

struct CPack;
struct CPackForLobby;
struct LobbyGuiAction;
struct LobbyClientConnected;

class GameConnection;
struct StartInfo;
struct LobbyInfo;
struct PlayerSettings;
class PlayerColor;
class MetaString;
struct PlayerMessageClient;

VCMI_LIB_NAMESPACE_END

class CGameHandler;
class CBaseForServerApply;
class CBaseForGHApply;
class GlobalLobbyProcessor;

class CVCMIServer : public LobbyInfo, public INetworkServerListener, public INetworkTimerListener, public IServerDiscoveryAnnouncer, public IGameServer
{
	std::chrono::steady_clock::time_point gameplayStartTime;
	std::chrono::steady_clock::time_point lastTimerUpdateTime;

	std::unique_ptr<INetworkHandler> networkHandler;
	/// Network server instance that receives and processes incoming connections on active socket
	std::unique_ptr<INetworkServer> networkServer;

	/// Handles connection with global lobby. Must be constructed and destroyed after network handler
	std::unique_ptr<GlobalLobbyProcessor> lobbyProcessor;

	EServerState state = EServerState::LOBBY;

	std::shared_ptr<GameConnection> findConnection(const std::shared_ptr<INetworkConnection> &);

	/// DMB: requests from players held while the host's observer bar pauses the game (DmbObserverPause), in arrival order
	std::deque<std::pair<std::shared_ptr<GameConnection>, std::unique_ptr<CPack>>> heldPacks;
	void releaseHeldPacks();

	GameConnectionID currentClientId;
	PlayerConnectionID currentPlayerId;
	uint16_t port;
	bool runByClient;

	std::shared_ptr<IServerDiscoveryListener> discoveryListener;

	bool loadSavedGame(CGameHandler & handler, const StartInfo & info);

	/// DMB: hot join step 1, "come back" (setting server.allowRejoin). The seats a dropped connection had, by its client session (uuid); a connection that
	/// arrives in a running game waits in pendingRejoin, outside activeConnections (so no game pack is sent to it), until its first pack names one of them.
	std::map<std::string, std::vector<PlayerColor>> rejoinSeats;
	std::vector<std::shared_ptr<GameConnection>> pendingRejoin;
	bool rejoinOpen() const;
	/// true when the message came from a connection that is waiting to come back (and was dealt with)
	bool handleRejoinPack(const std::shared_ptr<INetworkConnection> & connection, const std::vector<std::byte> & message);
	void rejoinConnected(const std::shared_ptr<GameConnection> & connection, const LobbyClientConnected & request);
public:
	uint16_t getPort() const override
	{
		return port;
	}

	bool isInLobby() const override;

	// IGameServer impl
	void setState(EServerState value) override;
	EServerState getState() const override;
	bool isPlayerHost(const PlayerColor & color) const override;
	bool hasPlayerAt(PlayerColor player, GameConnectionID connectionID) const override;
	bool hasBothPlayersAtSameConnection(PlayerColor left, PlayerColor right) const override;
	void applyPack(CPackForClient & pack) override;
	void sendPack(CPackForClient & pack, GameConnectionID connectionID) override;

	/// DMB: a chat line goes only where its channel reaches (DmbChatRouting.h); the line is stamped with the channel it went out on
	void sendChatMessage(PlayerMessageClient & pack, GameConnectionID senderConnection, EChatChannel requested);

	/// List of all active connections
	std::vector<std::shared_ptr<GameConnection>> activeConnections;

	/// DMB (K, October 9th): the host's last LobbyGuiAction, told to a client that joins after it
	std::shared_ptr<LobbyGuiAction> lastGuiAction;
	std::shared_ptr<LobbyGuiAction> lastGeneratorAction; // the last OPEN_GENERATOR that carried settings
	void replayLobbyView(GameConnectionID clientId);

	uint16_t prepare(bool connectToLobby, bool listenForConnections);

	// INetworkListener impl
	void onDisconnected(const std::shared_ptr<INetworkConnection> & connection, const std::string & errorMessage) override;
	void onPacketReceived(const std::shared_ptr<INetworkConnection> & connection, const std::vector<std::byte> & message) override;
	void onNewConnection(const std::shared_ptr<INetworkConnection> &) override;
	void onTimer() override;

	std::shared_ptr<CGameHandler> gh;

	CVCMIServer(uint16_t port, bool runByClient);
	~CVCMIServer();

	void run();

	bool wasStartedByClient() const;
	bool prepareToStartGame();
	void prepareToRestart();
	void startGameImmediately();
	uint16_t startAcceptingIncomingConnections(bool listenForConnections);

	void threadHandleClient(std::shared_ptr<GameConnection> c);

	void announcePack(CPackForLobby & pack);
	bool passHost(GameConnectionID toConnectionId);

	void announceTxt(const MetaString & txt, const std::string & playerName = "System");
	void announceTxt(const std::string & txt, const std::string & playerName = "System");

	void setPlayerConnectedId(PlayerSettings & pset, PlayerConnectionID player) const;
	void updateStartInfoOnMapChange(std::shared_ptr<CMapInfo> mapInfo, std::shared_ptr<CMapGenOptions> mapGenOpt = {});

	void clientConnected(std::shared_ptr<GameConnection> c, std::vector<std::string> & names, const std::string & uuid, EStartMode mode);
	void clientDisconnected(std::shared_ptr<GameConnection> c);

	void announceMessage(const MetaString & txt);
	void announceMessage(const std::string & txt);

	void handleReceivedPack(std::shared_ptr<GameConnection> connection, CPackForLobby & pack);

	void updateAndPropagateLobbyState();

	INetworkHandler & getNetworkHandler();
	INetworkServer & getNetworkServer();

	// Work with LobbyInfo
	void setPlayer(PlayerColor clickedColor);
	void setPlayerName(PlayerColor player, const std::string & name);
	void setPlayerHandicap(PlayerColor player, Handicap handicap);
	void optionNextHero(PlayerColor player, int dir); //dir == -1 or +1
	void optionSetHero(PlayerColor player, HeroTypeID id);
	HeroTypeID nextAllowedHero(PlayerColor player, HeroTypeID id, int direction);
	bool canUseThisHero(PlayerColor player, HeroTypeID ID);
	std::vector<HeroTypeID> getUsedHeroes();
	void optionNextBonus(PlayerColor player, int dir); //dir == -1 or +1
	void optionSetBonus(PlayerColor player, PlayerStartingBonus id);
	void optionNextCastle(PlayerColor player, int dir); //dir == -1 or +
	void optionSetCastle(PlayerColor player, FactionID id);

	// Campaigns
	void setCampaignMap(CampaignScenarioID mapId);
	void setCampaignBonus(int bonusId);

	PlayerConnectionID getIdOfFirstUnallocatedPlayer() const;

	void multiplayerWelcomeMessage();
	void startDiscoveryListener();
	void stopDiscoveryListener();
};
