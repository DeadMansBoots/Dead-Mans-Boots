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
double townWeight(int remaining)
{
	if(remaining <= 3)
		return 0.0;
	return std::min(1.0, (remaining - 3) / 7.0);
}

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

	// heroes: movement points spent over the points the heroes had when the turn started
	int total = 0;
	int spent = 0;
	for(const auto & start : heroes)
	{
		if(start.movement <= 0)
			continue;
		const HeroNow * hero = findHero(start.id);
		total += start.movement;
		if(!hero)
		{
			spent += start.movement; // lost or dismissed during the turn: it has nothing left to move
			continue;
		}
		spent += hero->canMove ? std::clamp(start.movement - hero->movement, 0, start.movement) : start.movement;
	}
	const double heroWeight = total > 0 ? 1.0 : 0.0;
	const double heroShare = total > 0 ? static_cast<double>(spent) / total : 0.0;

	// towns: each town that could build at dawn, weighted by what is left on its list; done when it built today or nothing can be built there now
	double weights = 0.0;
	double doneWeights = 0.0;
	for(const auto & start : towns)
	{
		if(!start.canBuild)
			continue;
		const TownNow * town = findTown(start.id);
		const double weight = townWeight(start.remaining);
		if(!town || weight <= 0.0)
			continue;
		weights += weight;
		if(town->built > 0 || !town->canBuild)
			doneWeights += weight;
	}
	const double townsWeight = std::min(1.0, weights);
	const double townShare = weights > 0.0 ? doneWeights / weights : 0.0;

	const double denominator = heroWeight + townsWeight;
	if(denominator <= 0.0)
		return 100; // nothing to move and nothing worth building
	return std::clamp(static_cast<int>(100.0 * (heroShare * heroWeight + townShare * townsWeight) / denominator + 0.5), 0, 100);
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
