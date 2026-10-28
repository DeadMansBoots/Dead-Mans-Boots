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
}
