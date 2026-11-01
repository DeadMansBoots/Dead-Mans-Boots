/*
 * TurnProgressMath.h, part of VCMI engine (DMB addition)
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <cstddef>
#include <vector>

/// DMB: the turn progress bar's arithmetic on plain values, apart from the game's objects, so the one formula serves every player's bar and a test can run it
/// without the game's data. TurnProgressBar reads the values from the game state and draws the result.
namespace TurnProgressMath
{
/// a hero as the player's turn starts
struct HeroStart
{
	int id = 0;
	int movement = 0;
};

/// a town as the player's turn starts
struct TownStart
{
	int id = 0;
	/// a structure is allowed in the town and paid for
	bool canBuild = false;
	/// the structures and upgrades still on the town's list: not built, and not forbidden there (affordable or not)
	int remaining = 0;
};

/// a hero as it is now; a hero the player no longer has is left out
struct HeroNow
{
	int id = 0;
	int movement = 0;
	/// a step to a neighbouring tile is still affordable with the movement left; false counts the hero's movement as spent
	bool canMove = true;
};

/// a town as it is now; a town the player no longer has is left out
struct TownNow
{
	int id = 0;
	/// structures built in the town this turn
	int built = 0;
	/// a structure can still be built in the town now (allowed and paid for); false counts the town as done
	bool canBuild = true;
};

/// a town's weight in the bar from what is left on its list (K, October 31st 19:49: "the town build ratio should shrink on a per town basis when it gets down
/// to just 3-4 more buildings left to be built, and go away completely when all that can be built are built"): full at 5 or more, then remaining / 5
/// (0.8 at 4, 0.6 at 3, 0.4 at 2, 0.2 at 1), and 0 when nothing is left
double townWeight(int remaining);

/// how much of the turn is done, 0 to 100 (K, October 23rd 13:0x: "i mainly just want the turn progress bar to track: 1. Heroes to move 2. Towns that can
/// be built in"; roster\NOTE-turnbar-formula-1023.md). H: the movement the heroes have spent of what they had at dawn (a hero with no affordable step
/// counts as spent). T: the weighted share of towns that could build at dawn and have built today or can build nothing now; W: their weights summed, at
/// most 1. The bar is (H + T * W) / (1 + W), or T alone when no hero had movement at dawn; at most 99, and 99 when neither applies (K, October 31st: the meter is never 100% unless the player ended the turn).
int percent(const std::vector<HeroStart> & heroes, const std::vector<TownStart> & towns, const std::vector<HeroNow> & heroesNow, const std::vector<TownNow> & townsNow);

/// a bar's size and where its top left goes, in screen pixels
struct Size
{
	int w = 0;
	int h = 0;
};
struct Place
{
	int x = 0;
	int y = 0;
};

/// where each bar goes, in order: stacked downward from origin with gap pixels between them; a bar whose bottom would pass maxBottom starts a new column to the right
/// of the widest bar so far in the column, again at origin's height. A column always takes at least one bar, so every bar is placed, even when a single bar is taller
/// than the space allowed
std::vector<Place> layout(const std::vector<Size> & bars, const Place & origin, int gap, int maxBottom);
}
