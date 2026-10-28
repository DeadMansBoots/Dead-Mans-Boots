/*
 * DmbAllyTake.h, part of VCMI engine (Dead Man's Boots)
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "NetPacksBase.h"

VCMI_LIB_NAMESPACE_BEGIN

/// DMB: diplomacy control A. The whole table of who lets which ally take troops and artifacts, sent to every client when it changes, when a turn starts and
/// when a game starts or resumes. Each pair is (owner, taker): the owner lets the taker take from the owner's heroes and towns.
struct DLL_LINKAGE AllyTakeRules : public CPackForClient
{
	std::vector<std::pair<PlayerColor, PlayerColor>> rules;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & rules;
	}
};

VCMI_LIB_NAMESPACE_END
