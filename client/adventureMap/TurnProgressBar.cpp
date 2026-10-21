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
#include "../render/IRenderHandler.h"
#include "../widgets/TextControls.h"

#include "../../lib/CPlayerState.h"
#include "../../lib/StartInfo.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/building/CBuilding.h"
#include "../../lib/entities/faction/CTown.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/spells/CSpellHandler.h"

#include <algorithm>

namespace
{
/// the frame's size, and how far the diamond end pieces reach past its left and right sides
const Point FRAME_SIZE(120, 18);
const int END_PIECE = 9;
/// the player's name is centred this far above the frame's top
const int NAME_ABOVE = 6;
}

TurnProgressBar::TurnProgressBar(const Point & position, PlayerColor player)
	: CIntObject(TIME)
	, player(player)
{
	// the name line above the frame: its centre NAME_ABOVE over the frame, half a line and the outline above that
	const int nameHeight = static_cast<int>(ENGINE->renderHandler().loadFont(FONT_TINY)->getLineHeight());
	const int above = std::max(14, NAME_ABOVE + nameHeight / 2 + 2);
	pos += position;
	frame = Rect(END_PIECE, above, FRAME_SIZE.x, FRAME_SIZE.y);
	pos.w = FRAME_SIZE.x + 2 * END_PIECE;
	pos.h = above + FRAME_SIZE.y;
	recActions &= ~DEACTIVATE;
}

Point TurnProgressBar::frameOffset() const
{
	return frame.topLeft();
}

void TurnProgressBar::fitName()
{
	// the name is centred over the frame; a wider one starts at pos's left edge and pos reaches as far right as it does, with its outline
	const int nameWidth = playerName.empty() ? 0 : static_cast<int>(ENGINE->renderHandler().loadFont(FONT_TINY)->getStringWidth(playerName)) + 2;
	const int frameReach = frame.x + frame.w + END_PIECE;
	const int nameReach = std::max(frame.x + frame.w / 2 + nameWidth / 2, nameWidth) + 1;
	// pos never narrows: the map paints fresh only the tiles under pos, so a longer name drawn before (a teammate's) would stay on the map
	// past a narrower pos until those tiles changed
	pos.w = std::max({pos.w, frameReach, nameReach});
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
		TurnProgressMath::HeroStart start;
		start.id = hero->id.getNum();
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
		TurnProgressMath::TownStart start;
		start.id = town->id.getNum();
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
			// the level counts when the player can pay for any creature of it, the basic one included (not only its last, upgraded form)
			const bool affordable = std::any_of(level.second.begin(), level.second.end(), [&resources](const CreatureID & id)
			{
				const auto * creature = id.toCreature();
				return creature && resources.canAfford(creature->getFullRecruitCost());
			});
			if(affordable)
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
	{
		// the name the lobby shows for this player
		const auto & infos = cb->getStartInfo()->playerInfos;
		const auto found = infos.find(player);
		playerName = found != infos.end() && !found->second.name.empty() ? found->second.name : player.toString();
	}
	fitName();
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
	std::vector<TurnProgressMath::HeroNow> heroesNow;
	for(const CGHeroInstance * hero : state->getHeroes())
		heroesNow.push_back({hero->id.getNum(), hero->movementPointsRemaining(), static_cast<int>(hero->mana)});
	std::vector<TurnProgressMath::TownNow> townsNow;
	for(const CGTownInstance * town : state->getTowns())
	{
		TurnProgressMath::TownNow now;
		now.id = town->id.getNum();
		now.built = town->built;
		for(const auto & level : town->creatures)
			now.available.push_back(static_cast<int>(level.first));
		townsNow.push_back(now);
	}
	return TurnProgressMath::percent(heroes, towns, heroesNow, townsNow);
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
	if(percent != loggedPercent)
	{
		loggedPercent = percent;
		logGlobal->info("BARPERCENT player=%s name=%s day=%d percent=%d", player.toString(), playerName, GAME->interface()->cb->getDate(Date::DAY), percent);
	}
}

void TurnProgressBar::drawOutlined(Canvas & to, OutlinedText & kept, const std::string & text, const Point & centre)
{
	if(!kept.image || kept.text != text)
	{
		const auto & font = ENGINE->renderHandler().loadFont(FONT_TINY);
		kept.text = text;
		kept.size = Point(static_cast<int>(font->getStringWidth(text)) + 2, static_cast<int>(font->getLineHeight()) + 2);
		kept.image = std::make_shared<Canvas>(kept.size, CanvasScalingPolicy::AUTO);
		for(int dx = -1; dx <= 1; ++dx)
			for(int dy = -1; dy <= 1; ++dy)
				if(dx != 0 || dy != 0)
					kept.image->drawText(Point(1 + dx, 1 + dy), FONT_TINY, Colors::BLACK, ETextAlignment::TOPLEFT, text);
		kept.image->drawText(Point(1, 1), FONT_TINY, Colors::WHITE, ETextAlignment::TOPLEFT, text);
		kept.image->applyTransparency(true);
	}
	to.draw(*kept.image, centre - kept.size / 2);
}

void TurnProgressBar::showAll(Canvas & to)
{
	show(to);
}

void TurnProgressBar::show(Canvas & to)
{
	ColorRGBA base(150, 150, 150, 255);
	if(player.getNum() >= 0 && player.getNum() < 8)
		base = graphics->playerColors[player.getNum()];
	const auto shade = [](const ColorRGBA & c, double factor, uint8_t alpha)
	{
		return ColorRGBA(static_cast<uint8_t>(std::clamp(c.r * factor, 0.0, 255.0)), static_cast<uint8_t>(std::clamp(c.g * factor, 0.0, 255.0)), static_cast<uint8_t>(std::clamp(c.b * factor, 0.0, 255.0)), alpha);
	};

	const Rect outer = frame + pos.topLeft();
	const Rect inner(outer.x + 3, outer.y + 3, outer.w - 6, outer.h - 6);

	// the map has painted the terrain under pos fresh in this frame (AdventureMapInterface::show): the glass darkens it 30 percent, so 70 percent of the terrain shows
	to.drawColorBlended(inner, ColorRGBA(0, 0, 0, 77));

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
			to.drawColor(Rect(inner.x, inner.y + 1, body, 1), shade(base, 1.75, 255)); // the soft highlight under the surface
		}
	}

	// the trim: a gold frame in three lines (dark, gold, deep bronze) with a light edge on the top and left, a row of pearls along the top and bottom, flourishes at the corners and
	// diamond end pieces
	const ColorRGBA dark(45, 28, 8, 255), gold(214, 170, 70, 255), light(250, 222, 140, 255), deep(120, 82, 22, 255), pearl(255, 240, 190, 255);
	to.drawBorder(outer, dark, 1);
	to.drawBorder(Rect(outer.x + 1, outer.y + 1, outer.w - 2, outer.h - 2), gold, 1);
	to.drawBorder(Rect(outer.x + 2, outer.y + 2, outer.w - 4, outer.h - 4), deep, 1);
	to.drawLine(Point(outer.x + 2, outer.y + 1), Point(outer.x + outer.w - 3, outer.y + 1), light, light);
	to.drawLine(Point(outer.x + 1, outer.y + 2), Point(outer.x + 1, outer.y + outer.h - 3), light, light);
	for(int x = outer.x + 8; x < outer.x + outer.w - 8; x += 5)
	{
		to.drawColor(Rect(x, outer.y + 1, 1, 1), pearl); // pearls on the gold line, top and bottom
		to.drawColor(Rect(x, outer.y + outer.h - 2, 1, 1), pearl);
		to.drawColor(Rect(x + 2, outer.y + 1, 1, 1), dark); // a dark bead between each pair
		to.drawColor(Rect(x + 2, outer.y + outer.h - 2, 1, 1), dark);
	}
	for(const bool right : {false, true})
	{
		for(const bool bottom : {false, true})
		{
			// a corner flourish: a bronze plate with a gold border, a bright diagonal stroke and a pearl
			const int cx = right ? outer.x + outer.w - 6 : outer.x;
			const int cy = bottom ? outer.y + outer.h - 6 : outer.y;
			to.drawColor(Rect(cx, cy, 6, 6), deep);
			to.drawBorder(Rect(cx, cy, 6, 6), dark, 1);
			to.drawBorder(Rect(cx + 1, cy + 1, 4, 4), gold, 1);
			to.drawLine(Point(cx + (right ? 4 : 1), cy + (bottom ? 4 : 1)), Point(cx + (right ? 1 : 4), cy + (bottom ? 1 : 4)), light, light);
			to.drawPoint(Point(cx + 2 + (right ? 1 : 0), cy + 2 + (bottom ? 1 : 0)), pearl);
		}
	}
	for(const bool right : {false, true})
	{
		// a diamond end piece in the middle of each end: bronze outline, gold body, a pearl in the middle and short bars either side
		const int mx = right ? outer.x + outer.w + 3 : outer.x - 4;
		const int my = outer.y + outer.h / 2;
		for(int d = 0; d <= 5; ++d)
		{
			const int half = 5 - d;
			to.drawLine(Point(mx - half, my - d), Point(mx + half, my - d), d == 5 ? dark : gold, d == 5 ? dark : gold);
			to.drawLine(Point(mx - half, my + d), Point(mx + half, my + d), d == 5 ? dark : gold, d == 5 ? dark : gold);
		}
		to.drawBorder(Rect(mx - 2, my - 2, 5, 5), deep, 1);
		to.drawColor(Rect(mx - 1, my - 1, 3, 3), light);
		to.drawPoint(Point(mx, my), pearl);
		const int inward = right ? -6 : 6;
		to.drawColor(Rect(mx + (right ? -7 : 5), my - 1, 3, 3), gold); // a bar joining the diamond to the frame
		(void)inward;
	}

	if(!emptyFrame)
	{
		// the percent and the player's name, white with a solid black outline so they read over any terrain
		drawOutlined(to, percentText, std::to_string(static_cast<int>(std::lround(shown))) + "%", Point(outer.x + outer.w / 2, outer.y + outer.h / 2));
		if(!playerName.empty())
		{
			// centred over the frame, or from pos's left edge when wider (fitName)
			const int nameWidth = static_cast<int>(ENGINE->renderHandler().loadFont(FONT_TINY)->getStringWidth(playerName)) + 2;
			drawOutlined(to, nameText, playerName, Point(std::max(outer.x + outer.w / 2, pos.x + 1 + nameWidth / 2), outer.y - NAME_ABOVE));
		}
	}
}
