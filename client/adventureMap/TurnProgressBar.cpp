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
#include "../GameEngine.h"
#include "../gui/WindowHandler.h"
#include "AdventureMapInterface.h"
#include "../GameInstance.h"
#include "../render/Colors.h"
#include "../render/Graphics.h"
#include "../render/Canvas.h"
#include "../render/IFont.h"
#include "../widgets/TextControls.h"

#include "../../lib/CPlayerState.h"
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
	, barSize(120, 18)
{
	pos += position;
	pos.w = barSize.x;
	pos.h = barSize.y;
	recActions &= ~DEACTIVATE;
}

void TurnProgressBar::startTurn(PlayerColor newPlayer)
{
	player = newPlayer;
	turnEnded = false;
	emptyFrame = false;
	takeSnapshot();
}

void TurnProgressBar::takeSnapshot()
{
	heroes.clear();
	towns.clear();
	snapshotTaken = false;

	if(!GAME->interface() || !GAME->interface()->cb)
		return;
	const auto & cb = GAME->interface()->cb;
	// this client's game state holds every player's heroes and towns (stock sends every pack to every client); the callback hands out the state of a player who is not this
	// client's enemy, so it is the player's own and a teammate's
	const PlayerState * state = cb->getPlayerState(player, false);
	if(!state)
		return;

	for(const CGHeroInstance * hero : state->getHeroes())
	{
		HeroStart start;
		start.id = hero->id;
		start.movement = hero->movementPointsRemaining();
		start.mana = hero->mana;
		if(hero->hasSpellbook())
		{
			for(const SpellID & id : hero->getSpellsInSpellbook())
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

	const ResourceSet resources = state->resources;
	for(const CGTownInstance * town : state->getTowns())
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
		for(size_t index = 0; index < town->creatures.size(); ++index)
		{
			const auto & level = town->creatures[index];
			if(level.second.empty() || level.first == 0)
				continue;
			const auto * creature = level.second.back().toCreature();
			if(creature && resources.canAfford(creature->getFullRecruitCost()))
			{
				start.recruitable += static_cast<int>(level.first);
				start.levels.push_back(index);
			}
		}
		towns.push_back(start);
	}
	snapshotTaken = true;
	percent = 0;
	shown = 0;
}

void TurnProgressBar::endTurn()
{
	turnEnded = true;
	percent = 100;
}

void TurnProgressBar::remoteTurn(PlayerColor active, bool human)
{
	const auto * cb = GAME->interface() ? GAME->interface()->cb.get() : nullptr;
	// a human who is not this client's enemy (a teammate): tracked, in that player's colour; any other turn, an enemy's or an AI's: the empty frame
	if(human && cb && cb->getPlayerState(active, false))
	{
		player = active;
		emptyFrame = false;
		turnEnded = false;
		takeSnapshot();
		return;
	}
	emptyFrame = true;
	snapshotTaken = false;
	heroes.clear();
	towns.clear();
	percent = 0;
	shown = 0;
}

int TurnProgressBar::compute() const
{
	if(turnEnded)
		return 100;
	if(!snapshotTaken || !GAME->interface() || !GAME->interface()->cb)
		return 0;
	const auto & cb = GAME->interface()->cb;
	const PlayerState * state = cb->getPlayerState(player, false);
	if(!state)
		return 0;
	const auto heroList = state->getHeroes();
	const auto townList = state->getTowns();
	const auto findHero = [&heroList](ObjectInstanceID id) -> const CGHeroInstance *
	{
		for(const CGHeroInstance * hero : heroList)
			if(hero->id == id)
				return hero;
		return nullptr;
	};
	const auto findTown = [&townList](ObjectInstanceID id) -> const CGTownInstance *
	{
		for(const CGTownInstance * town : townList)
			if(town->id == id)
				return town;
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
			const CGHeroInstance * hero = findHero(start.id);
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
			const CGTownInstance * town = findTown(start.id);
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
			const CGTownInstance * town = findTown(start.id);
			if(!town)
				continue;
			int now = 0;
			for(const size_t index : start.levels)
				if(index < town->creatures.size())
					now += static_cast<int>(town->creatures[index].first);
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
			const CGHeroInstance * hero = findHero(start.id);
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

void TurnProgressBar::tick(uint32_t msPassed)
{
	clockMs += msPassed;
	sinceUpdate += msPassed;
	// the drawn percent eases toward the real one: the liquid rises instead of jumping
	shown += (percent - shown) * std::min(1.0, msPassed / 180.0);
	if(sinceUpdate < 250)
		return;
	sinceUpdate = 0;
	if(emptyFrame)
		return;
	// a turn loaded from a save has no snapshot (the turn did not start in this run): it is taken now, and the bar reads what is done since
	if(!snapshotTaken && !turnEnded && GAME->interface() && GAME->interface()->cb)
		takeSnapshot();
	percent = compute();
}

void TurnProgressBar::show(Canvas & to)
{
	// the map under the top left corner is repainted every frame, so the bar is too: a window over it (a town screen, a dialog) is the top window and the adventure map is not shown at all then
	showAll(to);
}

void TurnProgressBar::showAll(Canvas & to)
{
	ColorRGBA base(150, 150, 150, 255);
	if(player.getNum() >= 0 && player.getNum() < 8)
		base = graphics->playerColors[player.getNum()];
	const auto shade = [](const ColorRGBA & c, double factor, uint8_t alpha)
	{
		return ColorRGBA(static_cast<uint8_t>(std::clamp(c.r * factor, 0.0, 255.0)), static_cast<uint8_t>(std::clamp(c.g * factor, 0.0, 255.0)), static_cast<uint8_t>(std::clamp(c.b * factor, 0.0, 255.0)), alpha);
	};

	// a patch of colour laid over what is there with real transparency: drawColorBlended on the screen canvas draws opaque (its surface has no alpha channel)
	const auto blend = [&to](const Rect & area, const ColorRGBA & color)
	{
		if(area.w <= 0 || area.h <= 0)
			return;
		Canvas patch(Point(area.w, area.h), CanvasScalingPolicy::AUTO);
		patch.drawColor(Rect(0, 0, area.w, area.h), ColorRGBA(color.r, color.g, color.b, 255));
		to.drawTransparent(patch, Point(area.x, area.y), color.a / 255.0);
	};

	const Rect outer(pos.x, pos.y, pos.w, pos.h);
	const Rect inner(pos.x + 3, pos.y + 3, pos.w - 6, pos.h - 6);

	// the empty part: a dark glass, semi-transparent, the map or the marble shows through
	blend(inner, ColorRGBA(0, 0, 0, 105));

	const double t = clockMs / 1000.0;
	if(!emptyFrame)
	{
		const int width = std::clamp(static_cast<int>(std::lround(inner.w * shown / 100.0)), 0, inner.w);
		if(width > 0)
		{
			// the liquid: lighter at the top, darker at the bottom; its leading edge is a few columns whose surface ripples
			const int ripple = std::min(width, 5);
			const int body = width - ripple;
			for(int row = 0; row < inner.h; ++row)
			{
				const double depth = static_cast<double>(row) / std::max(1, inner.h - 1);
				const ColorRGBA tone = shade(base, 1.30 - 0.85 * depth, 255);
				to.drawColor(Rect(inner.x, inner.y + row, body, 1), tone);
				for(int i = 0; i < ripple; ++i)
				{
					const double lift = 1.4 * std::sin(t * 4.0 + i * 1.1) * (1.0 + i * 0.25);
					const int top = std::clamp(static_cast<int>(std::lround((ripple - 1 - i) * 0.0 + (i + 1) * 0.5 + lift)), 0, inner.h - 3);
					if(row >= top)
						to.drawColor(Rect(inner.x + body + i, inner.y + row, 1, 1), tone);
				}
			}
			blend(Rect(inner.x, inner.y + 1, body, 1), ColorRGBA(255, 255, 255, 90)); // the soft highlight under the surface
		}
	}

	// the trim: a gold frame with a dark outer line, a light inner line and small ornaments at the corners and the middle of each end
	const ColorRGBA dark(45, 28, 8, 255), gold(214, 170, 70, 255), light(250, 222, 140, 255), deep(120, 82, 22, 255);
	to.drawBorder(outer, dark, 1);
	to.drawBorder(Rect(outer.x + 1, outer.y + 1, outer.w - 2, outer.h - 2), gold, 1);
	to.drawBorder(Rect(outer.x + 2, outer.y + 2, outer.w - 4, outer.h - 4), deep, 1);
	to.drawLine(Point(outer.x + 2, outer.y + 1), Point(outer.x + outer.w - 3, outer.y + 1), light, light);
	to.drawLine(Point(outer.x + 1, outer.y + 2), Point(outer.x + 1, outer.y + outer.h - 3), light, light);
	for(const Point & corner : {Point(outer.x, outer.y), Point(outer.x + outer.w - 3, outer.y), Point(outer.x, outer.y + outer.h - 3), Point(outer.x + outer.w - 3, outer.y + outer.h - 3)})
	{
		to.drawColor(Rect(corner.x, corner.y, 3, 3), gold);
		to.drawPoint(Point(corner.x + 1, corner.y + 1), light);
	}
	for(const int x : {outer.x - 2, outer.x + outer.w + 1})
	{
		to.drawColor(Rect(x, outer.y + outer.h / 2 - 2, 2, 5), gold);
		to.drawPoint(Point(x, outer.y + outer.h / 2), light);
	}

	if(!emptyFrame)
		to.drawText(Point(outer.x + outer.w / 2, outer.y + outer.h / 2), FONT_TINY, Colors::WHITE, ETextAlignment::CENTER, std::to_string(static_cast<int>(std::lround(shown))) + "%");
}
