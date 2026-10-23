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
	int mana = 0;
	/// the hero has an adventure spell in its book it can pay for
	bool canCast = false;
};

/// a town as the player's turn starts
struct TownStart
{
	int id = 0;
	/// a structure is allowed in the town
	bool canBuild = false;
	/// creatures in the dwelling levels below, summed
	int recruitable = 0;
	/// the dwelling levels counted in recruitable (the ones with a creature, basic or upgraded, the player could afford at the start): the same levels are summed again later
	std::vector<size_t> levels;
};

/// a hero as it is now; a hero the player no longer has is left out
struct HeroNow
{
	int id = 0;
	int movement = 0;
	int mana = 0;
	/// a step to a neighbouring tile is still affordable with the movement left; false counts the hero's movement as spent
	bool canMove = true;
	/// an adventure spell the hero can pay for is still in its book; false counts the hero's mana as spent
	bool canCast = true;
};

/// a town as it is now; a town the player no longer has is left out
struct TownNow
{
	int id = 0;
	/// structures built in the town this turn
	int built = 0;
	/// creatures available in each dwelling level
	std::vector<int> available;
	/// a structure can still be built in the town now (allowed and paid for); false counts the town's building as done
	bool canBuild = true;
	/// a creature of a level counted at the start is still in its dwelling and the player can pay for it now; false counts the town's recruiting as done
	bool canRecruit = true;
};

/// how much of the turn is done, 0 to 100: the mean of the categories that apply (hero movement spent, a structure built in each town that could build one,
/// creatures recruited where some could be, mana spent by heroes who could cast an adventure spell); 0 when none applies. A category is done when nothing
/// useful is left in it (K's bar read 67 percent with the gold spent on a building and nothing left to recruit, October 22nd): a hero with no affordable
/// step, a town where nothing can be built or recruited now, a hero with no spell it can pay for counts as finished
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
