/*
 * DmbTradePointer.h, part of VCMI engine (Dead Man's Boots)
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "NetPacksBase.h"

#include "../Point.h"

VCMI_LIB_NAMESPACE_BEGIN

/// DMB: a player's pointer and selected stack in a shared trade window (the project owner, October 24th: "I SHOULD BE ABLE TO SEE EVERYTHING THE OTHER PLAYER IS
/// DOING"). The server passes it to the other player of the trade and answers nothing; a client sends it at most 25 times a second.
struct DLL_LINKAGE TradePointer : public CPackForServer
{
	QueryID trade;
	/// in the window's own coordinates; (-1, -1) when the pointer is not in the window
	Point position = Point(-1, -1);
	/// the selected stack: its slot, plus 7 for the lower army; -1 when none
	int32_t selection = -1;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & static_cast<CPackForServer &>(*this);
		h & trade;
		h & position;
		h & selection;
	}
};

/// DMB: the pointer of TradePointer, as the server passes it on to the other player of the trade
struct DLL_LINKAGE TradePointerShown : public CPackForClient
{
	QueryID trade;
	/// whose pointer it is
	PlayerColor player;
	/// who is shown it
	PlayerColor viewer;
	Point position = Point(-1, -1);
	int32_t selection = -1;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		h & trade;
		h & player;
		h & viewer;
		h & position;
		h & selection;
	}
};

VCMI_LIB_NAMESPACE_END
