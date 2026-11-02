/*
 * DmbRejoin.h, part of VCMI engine (Dead Man's Boots)
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "PacksForLobby.h"

VCMI_LIB_NAMESPACE_BEGIN

/// DMB: hot join step 1. The secret a client must show to take its seats back after its connection dropped (setting server.allowRejoin). The server makes it
/// and sends it to that client alone, once its interface is ready; it is in no broadcast, no lobby state and no log.
struct DLL_LINKAGE RejoinSecret : public CPackForClient
{
	std::string secret;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & secret;
	}
};

/// DMB: hot join step 1. A client that holds a secret sends it first when it connects, before its LobbyClientConnected. A server in a lobby, or with the
/// setting off, does nothing with it (the permission check refuses every lobby pack for the server that is not from the host).
struct DLL_LINKAGE RejoinProof : public CLobbyPackToServer
{
	std::string secret;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & secret;
	}
};

/// DMB: hot join step 1. A seat has its player again: the others' "player left" windows for that colour close
struct DLL_LINKAGE PlayerBack : public CPackForClient
{
	PlayerColor player;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
	}
};

/// DMB: a client that came back to a running game is asked, on its own screen, whether it takes its seats back (K, October 31st: "rejoins should be a prompt to the
/// player that dropped"). The server sends the colours it kept for that client.
struct DLL_LINKAGE RejoinOffer : public CLobbyPackToPropagate
{
	std::vector<std::string> colours;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & colours;
	}
};

/// DMB: the player's answer to the offer; only a yes gives the seats back, a no leaves them kept for the next time
struct DLL_LINKAGE RejoinAnswer : public CLobbyPackToServer
{
	bool accept = false;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & accept;
	}
};

/// DMB (K, October 31st: a player who left stops the game for the others): the seat of a player who left is played by an AI from the host's client until the player
/// comes back. Sent to the host's connection alone: take = install an AI interface for the seat and give it the turn; not take = the player is back, let the AI go.
struct DLL_LINKAGE AiTakeOver : public CPackForClient
{
	PlayerColor player;
	bool take = true;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & player;
		h & take;
	}
};

/// DMB hot join: a new player connected to the running game; the host is asked (server to the host's client)
struct DLL_LINKAGE JoinRequestNotice : public CPackForClient
{
	uint32_t requestId = 0;
	std::string name;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & requestId;
		h & name;
	}
};

/// DMB hot join: the host's answer to a join request (client to server)
struct DLL_LINKAGE JoinRequestAnswer : public CLobbyPackToServer
{
	uint32_t requestId = 0;
	bool accept = false;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & requestId;
		h & accept;
	}
};

/// DMB hot join: the newcomer was let in and watches (server to the host's client): the host can give it a seat
struct DLL_LINKAGE JoinAdmitted : public CPackForClient
{
	int32_t spectator = 0;
	std::string name;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & spectator;
		h & name;
	}
};

/// DMB hot join: the host gives a watching newcomer the seat of an AI (client to server)
struct DLL_LINKAGE AssignSeat : public CLobbyPackToServer
{
	int32_t spectator = 0;
	PlayerColor seat;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & spectator;
		h & seat;
	}
};

VCMI_LIB_NAMESPACE_END
