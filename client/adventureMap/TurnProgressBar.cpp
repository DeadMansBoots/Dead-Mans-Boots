/*
 * TurnProgressBar.cpp, part of VCMI engine (DMB addition)
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "TurnProgressBar.h"

#include "../CPlayerInterface.h"
#include "../GameInstance.h"
#include "../render/Colors.h"
#include "../render/Graphics.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/Images.h"
#include "../widgets/TextControls.h"

#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/building/CBuilding.h"
#include "../../lib/entities/faction/CTown.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/spells/CSpellHandler.h"

#include <algorithm>

TurnProgressBar::TurnProgressBar(const Point & position, PlayerColor player)
	: CIntObject(TIME)
	, player(player)
	, barSize(110, 16)
{
	OBJECT_CONSTRUCTION;
	pos += position;
	pos.w = barSize.x;
	pos.h = barSize.y;
	recActions &= ~DEACTIVATE;
	// the game's own marble (the player coloured box texture the other panels use), harvested at run time from the player's files
	background = std::make_shared<FilledTexturePlayerColored>(Rect(0, 0, barSize.x, barSize.y));
	background->setPlayerColor(player);
	frame = std::make_shared<TransparentFilledRectangle>(Rect(0, 0, barSize.x, barSize.y), ColorRGBA(0, 0, 0, 0), ColorRGBA(212, 175, 55, 255), 1);
	refresh();
}

void TurnProgressBar::startTurn(PlayerColor newPlayer)
{
	player = newPlayer;
	turnEnded = false;
	heroes.clear();
	towns.clear();
	snapshotTaken = false;

	if(!GAME->interface() || !GAME->interface()->cb)
		return;
	const auto & cb = GAME->interface()->cb;

	for(const CGHeroInstance * hero : cb->getHeroesInfo())
	{
		HeroStart start;
		start.id = hero->id;
		start.movement = hero->movementPointsRemaining();
		start.mana = hero->mana;
		if(hero->hasSpellbook())
		{
			for(const SpellID & id : hero->spells)
			{
				const auto * spell = id.toSpell();
				if(spell && spell->isAdventure() && hero->getSpellCost(spell) <= hero->mana)
				{
					start.canCast = true;
					break;
				}
			}
		}
		heroes.push_back(start);
	}

	const ResourceSet resources = cb->getResourceAmount();
	for(const CGTownInstance * town : cb->getTownsInfo())
	{
		TownStart start;
		start.id = town->id;
		if(town->built == 0)
		{
			for(const auto & building : town->getTown()->buildings)
			{
				if(cb->canBuildStructure(town, building.first) == EBuildingState::ALLOWED)
				{
					start.canBuild = true;
					break;
				}
			}
		}
		for(const auto & level : town->creatures)
		{
			if(level.second.empty() || level.first == 0)
				continue;
			const auto * creature = level.second.back().toCreature();
			if(creature && resources.canAfford(creature->getFullRecruitCost()))
				start.recruitable += static_cast<int>(level.first);
		}
		towns.push_back(start);
	}
	snapshotTaken = true;
	percent = 0;
	refresh();
}

void TurnProgressBar::endTurn()
{
	turnEnded = true;
	percent = 100;
	refresh();
}

int TurnProgressBar::compute() const
{
	if(turnEnded)
		return 100;
	if(!snapshotTaken || !GAME->interface() || !GAME->interface()->cb)
		return 0;
	const auto & cb = GAME->interface()->cb;

	std::vector<double> categories;

	// hero movement: points spent over the points the heroes had when the turn started
	{
		int total = 0;
		int spent = 0;
		for(const auto & start : heroes)
		{
			if(start.movement <= 0)
				continue;
			const CGHeroInstance * hero = cb->getHero(start.id);
			if(!hero)
				continue;
			total += start.movement;
			spent += std::clamp(start.movement - hero->movementPointsRemaining(), 0, start.movement);
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
			const CGTownInstance * town = cb->getTown(start.id);
			if(!town)
				continue;
			possible++;
			if(town->built > 0)
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
			const CGTownInstance * town = cb->getTown(start.id);
			if(!town)
				continue;
			int now = 0;
			for(const auto & level : town->creatures)
				now += static_cast<int>(level.first);
			total += start.recruitable;
			taken += std::clamp(start.recruitable - now, 0, start.recruitable);
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
			const CGHeroInstance * hero = cb->getHero(start.id);
			if(!hero)
				continue;
			total += start.mana;
			spent += std::clamp(start.mana - static_cast<int>(hero->mana), 0, start.mana);
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

void TurnProgressBar::refresh()
{
	OBJECT_CONSTRUCTION;
	fill.reset();
	label.reset();
	const int width = ((barSize.x - 4) * percent) / 100;
	if(width > 0)
		fill = std::make_shared<TransparentFilledRectangle>(Rect(2, 2, width, barSize.y - 4), graphics->playerColors[player.getNum()]);
	label = std::make_shared<CLabel>(barSize.x / 2, barSize.y / 2, FONT_TINY, ETextAlignment::CENTER, Colors::WHITE, std::to_string(percent) + "%");
	redraw();
}

void TurnProgressBar::tick(uint32_t msPassed)
{
	sinceUpdate += msPassed;
	if(sinceUpdate < 250)
		return;
	sinceUpdate = 0;
	const int now = compute();
	if(now != percent)
	{
		percent = now;
		refresh();
	}
}
