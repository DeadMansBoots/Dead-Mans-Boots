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
};

/// a town as it is now; a town the player no longer has is left out
struct TownNow
{
	int id = 0;
	/// structures built in the town this turn
	int built = 0;
	/// creatures available in each dwelling level
	std::vector<int> available;
};

/// how much of the turn is done, 0 to 100: the mean of the categories that apply (hero movement spent, a structure built in each town that could build one,
/// creatures recruited where some could be, mana spent by heroes who could cast an adventure spell); 0 when none applies
int percent(const std::vector<HeroStart> & heroes, const std::vector<TownStart> & towns, const std::vector<HeroNow> & heroesNow, const std::vector<TownNow> & townsNow);
}
