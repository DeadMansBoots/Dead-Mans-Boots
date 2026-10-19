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
	remote = false;
	hidden = false;
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

	for(const CGHeroInstance * hero : cb->getHeroesInfo())
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

/// Whether another human's turn is shown on this client (that player's colour, a travelling band, no percent). On (K decided, October 18th 17:36: the waiting client shows the active human's colour and progress); the percent for another player arrives by a display-only message (not built yet), until then a travelling band.
static constexpr bool showOtherHumansTurn = true;

void TurnProgressBar::remoteTurn(PlayerColor active, bool human)
{
	if(!showOtherHumansTurn)
		return;
	player = active;
	remote = human;
	hidden = !human;
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

void TurnProgressBar::tick(uint32_t msPassed)
{
	clockMs += msPassed;
	sinceUpdate += msPassed;
	// the drawn percent eases toward the real one: the liquid rises instead of jumping
	shown += (percent - shown) * std::min(1.0, msPassed / 180.0);
	if(sinceUpdate < 250)
		return;
	sinceUpdate = 0;
	if(remote || hidden)
		return;
	// a turn loaded from a save has no snapshot (the turn did not start in this run): it is taken now, and the bar reads what is done since
	if(!snapshotTaken && !turnEnded && GAME->interface() && GAME->interface()->cb && GAME->interface()->playerID == player)
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
	if(hidden)
		return;
	ColorRGBA base(150, 150, 150, 255);
	if(player.getNum() >= 0 && player.getNum() < 8)
		base = graphics->playerColors[player.getNum()];
	const auto shade = [](const ColorRGBA & c, double factor, uint8_t alpha)
	{
		return ColorRGBA(static_cast<uint8_t>(std::clamp(c.r * factor, 0.0, 255.0)), static_cast<uint8_t>(std::clamp(c.g * factor, 0.0, 255.0)), static_cast<uint8_t>(std::clamp(c.b * factor, 0.0, 255.0)), alpha);
	};

	const Rect outer(pos.x, pos.y, pos.w, pos.h);
	const Rect inner(pos.x + 3, pos.y + 3, pos.w - 6, pos.h - 6);

	// the empty part: a dark glass, semi-transparent, the map or the marble shows through
	to.drawColorBlended(inner, ColorRGBA(0, 0, 0, 95));

	const double t = clockMs / 1000.0;
	if(remote)
	{
		// another human's turn: a band of that player's colour travels across the glass; no percent is known here
		const int band = inner.w / 3;
		const int travel = inner.w + band;
		const int start = static_cast<int>(std::fmod(t * 45.0, static_cast<double>(travel))) - band;
		for(int x = 0; x < band; ++x)
		{
			const int px = start + x;
			if(px < 0 || px >= inner.w)
				continue;
			const double edge = 1.0 - std::abs(2.0 * x / band - 1.0); // soft ends
			to.drawColorBlended(Rect(inner.x + px, inner.y, 1, inner.h), shade(base, 1.0, static_cast<uint8_t>(40 + 110 * edge)));
		}
	}
	else
	{
		const int width = std::clamp(static_cast<int>(std::lround(inner.w * shown / 100.0)), 0, inner.w);
		if(width > 0)
		{
			// the liquid: lighter at the top, darker at the bottom, a bright thin highlight under the top edge
			for(int row = 0; row < inner.h; ++row)
			{
				const double depth = static_cast<double>(row) / std::max(1, inner.h - 1);
				to.drawColorBlended(Rect(inner.x, inner.y + row, width, 1), shade(base, 1.30 - 0.85 * depth, 235));
			}
			to.drawColorBlended(Rect(inner.x, inner.y + 1, width, 1), ColorRGBA(255, 255, 255, 80));
			// the leading edge: a few columns that ripple, the surface of the liquid shimmering
			const int ripple = std::min(width, 5);
			for(int i = 0; i < ripple; ++i)
			{
				const int x = inner.x + width - 1 - i;
				const double lift = 1.6 * std::sin(t * 4.0 + i * 1.1) * (1.0 + i * 0.25);
				const int top = std::clamp(static_cast<int>(std::lround(i * 0.7 + lift)), 0, inner.h - 2);
				to.drawColorBlended(Rect(x, inner.y, 1, top), ColorRGBA(0, 0, 0, 95)); // the glass shows above the ripple
				to.drawColorBlended(Rect(x, inner.y + top, 1, 1), ColorRGBA(255, 255, 255, 110));
			}
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

	if(!remote)
		to.drawText(Point(outer.x + outer.w / 2, outer.y + outer.h / 2), FONT_TINY, Colors::WHITE, ETextAlignment::CENTER, std::to_string(static_cast<int>(std::lround(shown))) + "%");
}
