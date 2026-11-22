/*
 * DmbSeatColours.cpp, part of VCMI engine (DMB addition)
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "DmbSeatColours.h"

#include "StartInfo.h"

#include <atomic>

VCMI_LIB_NAMESPACE_BEGIN

namespace
{
	// index: the seat's PlayerColor; value: the colour it is drawn in (-1 = itself)
	std::array<std::atomic<int>, PlayerColor::PLAYER_LIMIT_I> table = {-1, -1, -1, -1, -1, -1, -1, -1};
}

PlayerColor DmbSeatColours::shown(PlayerColor seat)
{
	if(!seat.isValidPlayer())
		return seat;
	const int picked = table[seat.getNum()].load(std::memory_order_relaxed);
	return picked >= 0 ? PlayerColor(picked) : seat;
}

void DmbSeatColours::update(const StartInfo * info)
{
	for(auto & entry : table)
		entry.store(-1, std::memory_order_relaxed);
	if(!info)
		return;
	for(const auto & seat : info->playerInfos)
		if(seat.first.isValidPlayer() && seat.second.shownColor.isValidPlayer() && seat.second.shownColor != seat.first)
			table[seat.first.getNum()].store(seat.second.shownColor.getNum(), std::memory_order_relaxed);
}

VCMI_LIB_NAMESPACE_END
