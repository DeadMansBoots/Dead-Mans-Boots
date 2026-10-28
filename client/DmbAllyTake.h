/*
 * DmbAllyTake.h, part of VCMI client (Dead Man's Boots)
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../lib/constants/EntityIdentifiers.h"

#include <set>

/// DMB: diplomacy control A, the client's copy of the table the server sends (AllyTakeRules). (owner, taker): the owner lets the taker take.
namespace DmbAllyTake
{
	inline std::set<std::pair<PlayerColor, PlayerColor>> & table()
	{
		static std::set<std::pair<PlayerColor, PlayerColor>> rules;
		return rules;
	}

	/// true when `owner` lets `taker` take troops and artifacts from the owner's heroes and towns
	inline bool allowed(PlayerColor owner, PlayerColor taker)
	{
		return owner != taker && table().count(std::make_pair(owner, taker)) != 0;
	}
}
