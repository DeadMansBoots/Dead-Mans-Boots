/*
 * DmbRejoin.h, part of VCMI client (Dead Man's Boots)
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <chrono>
#include <string>

/// DMB: hot join step 1. The secret the server sent this client (RejoinSecret); shown first when the client connects (RejoinProof). Empty when the game it
/// was in did not have server.allowRejoin on.
namespace DmbRejoin
{
	inline std::string & secret()
	{
		static std::string value;
		return value;
	}

	/// DMB: set when this client answered a RejoinOffer yes (by hand or by itself): the game it comes back to shows its map at once, as no turn start may be coming for it
	inline bool & returning()
	{
		static bool value = false;
		return value;
	}

	/// DMB (automatic reconnect): until this time a RejoinOffer is answered yes without asking (the player lost the connection and the client is bringing it back by itself)
	inline std::chrono::steady_clock::time_point & autoAnswerUntil()
	{
		static std::chrono::steady_clock::time_point value;
		return value;
	}
}
