/*
 * DmbSeatColours.h, part of VCMI engine (DMB addition)
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "constants/EntityIdentifiers.h"

VCMI_LIB_NAMESPACE_BEGIN

struct StartInfo;

/// DMB (S303, S424; K 11-17: "the ability to swap any color out for any player seat, regardless of how many seats are present in the game. If
/// I or another user want to have a game that's purple vs teal only, they should be able to"; K 11-21: "damn you guys didn't even get color
/// picking per seat in here?!"): a seat keeps its PlayerColor (the map's owner of its towns, heroes and objects, the server's id), and is
/// DRAWN in the colour the host picked for it (PlayerSettings::shownColor). Everything that turns a PlayerColor into a picture, a palette, a
/// text colour or a colour name asks shown() first. The table is per process (the client and, for a host, its server) and is refreshed
/// from the StartInfo whenever the lobby or the game hands over a new one; with no table every colour is itself (stock).
namespace DmbSeatColours
{
	/// the colour a seat is drawn in; a neutral or invalid colour is returned unchanged
	DLL_LINKAGE PlayerColor shown(PlayerColor seat);
	/// take the picks of every seat of this StartInfo (nullptr: back to stock)
	DLL_LINKAGE void update(const StartInfo * info);
}

VCMI_LIB_NAMESPACE_END
