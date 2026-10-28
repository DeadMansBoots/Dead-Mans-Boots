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

VCMI_LIB_NAMESPACE_END
