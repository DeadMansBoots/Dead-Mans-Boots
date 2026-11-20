/*
 * MenuColour.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../lib/CConfigHandler.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include "../../lib/constants/StringConstants.h"

/// DMB (S339, K November 19th: "the color under the gold/the outline should follow whatever background menu color the player is using. this
/// is blue by default ... make sure they are dynamically linked"): the colour of the menus outside a game, from the one setting
/// general.menuColour (blue by default). Every window that drew a fixed blue when no player owns the screen reads this instead, so a
/// change of the setting reaches the frames, the lobby's backgrounds and the seat windows' stock fallback together
inline PlayerColor menuColour()
{
	const std::string & name = settings["general"]["menuColour"].String();
	for(int i = 0; i < PlayerColor::PLAYER_LIMIT_I; ++i)
		if(name == GameConstants::PLAYER_COLOR_NAMES[i])
			return PlayerColor(i);
	return PlayerColor(1);
}
