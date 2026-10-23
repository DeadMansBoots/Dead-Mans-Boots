/*
 * TurnProgressMath.cpp, part of VCMI engine (DMB addition)
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "TurnProgressMath.h"

#include <algorithm>

namespace TurnProgressMath
{
int percent(const std::vector<HeroStart> & heroes, const std::vector<TownStart> & towns, const std::vector<HeroNow> & heroesNow, const std::vector<TownNow> & townsNow)
{
	const auto findHero = [&heroesNow](int id) -> const HeroNow *
	{
		for(const HeroNow & hero : heroesNow)
			if(hero.id == id)
				return &hero;
		return nullptr;
	};
	const auto findTown = [&townsNow](int id) -> const TownNow *
	{
		for(const TownNow & town : townsNow)
			if(town.id == id)
				return &town;
		return nullptr;
	};

	std::vector<double> categories;

	// hero movement: points spent over the points the heroes had when the turn started
	{
		int total = 0;
		int spent = 0;
		for(const auto & start : heroes)
		{
			if(start.movement <= 0)
				continue;
			const HeroNow * hero = findHero(start.id);
			if(!hero)
				continue;
			total += start.movement;
			spent += hero->canMove ? std::clamp(start.movement - hero->movement, 0, start.movement) : start.movement;
		}
		if(total > 0)
			categories.push_back(static_cast<double>(spent) / total);
	}

	// building: one structure a turn in each town that could build one
	{
		int possible = 0;
		int done = 0;
		for(const auto & start : towns)
		{
			if(!start.canBuild)
				continue;
			const TownNow * town = findTown(start.id);
			if(!town)
				continue;
			possible++;
			if(town->built > 0 || !town->canBuild)
				done++;
		}
		if(possible > 0)
			categories.push_back(static_cast<double>(done) / possible);
	}

	// recruiting: creatures taken out of the dwellings the player could afford at the start
	{
		int total = 0;
		int taken = 0;
		for(const auto & start : towns)
		{
			if(start.recruitable <= 0)
				continue;
			const TownNow * town = findTown(start.id);
			if(!town)
				continue;
			int now = 0;
			for(const size_t index : start.levels)
				if(index < town->available.size())
					now += town->available[index];
			total += start.recruitable;
			taken += town->canRecruit ? std::clamp(start.recruitable - now, 0, start.recruitable) : start.recruitable;
		}
		if(total > 0)
			categories.push_back(static_cast<double>(taken) / total);
	}

	// spells: mana spent by the heroes who could cast an adventure spell
	{
		int total = 0;
		int spent = 0;
		for(const auto & start : heroes)
		{
			if(!start.canCast || start.mana <= 0)
				continue;
			const HeroNow * hero = findHero(start.id);
			if(!hero)
				continue;
			total += start.mana;
			spent += hero->canCast ? std::clamp(start.mana - hero->mana, 0, start.mana) : start.mana;
		}
		if(total > 0)
			categories.push_back(static_cast<double>(spent) / total);
	}

	if(categories.empty())
		return 0;
	double sum = 0;
	for(double value : categories)
		sum += value;
	return std::clamp(static_cast<int>(100.0 * sum / categories.size() + 0.5), 0, 100);
}

std::vector<Place> layout(const std::vector<Size> & bars, const Place & origin, int gap, int maxBottom)
{
	std::vector<Place> places;
	places.reserve(bars.size());
	int x = origin.x;
	int y = origin.y;
	int columnWidth = 0;
	bool columnEmpty = true;
	for(const Size & bar : bars)
	{
		if(!columnEmpty && y + bar.h > maxBottom)
		{
			// the next column starts right of the widest bar in this one, at the top again
			x += columnWidth + gap;
			y = origin.y;
			columnWidth = 0;
			columnEmpty = true;
		}
		places.push_back({x, y});
		y += bar.h + gap;
		columnWidth = std::max(columnWidth, bar.w);
		columnEmpty = false;
	}
	return places;
}
}
