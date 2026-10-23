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
#include "../render/IImage.h"
#include "../render/IRenderHandler.h"
#include "../widgets/TextControls.h"

#include "../../lib/CPlayerState.h"
#include "../../lib/StartInfo.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/building/CBuilding.h"
#include "../../lib/entities/faction/CTown.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapping/TerrainTile.h"
#include "../../lib/pathfinder/CPathfinder.h"
#include "../../lib/pathfinder/PathfinderOptions.h"
#include "../../lib/spells/CSpellHandler.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace
{
/// the frame's size, and how far the diamond end pieces reach past its left and right sides
const Point FRAME_SIZE(120, 18);
const int END_PIECE = 9;
/// the player's name is centred this far above the frame's top
const int NAME_ABOVE = 8;   // the small font's line is taller than the tiny one's: its bottom stays clear of the frame
/// the name and the percent use the font of the resource bar and the date beside it (FONT_TINY drew a smooth sans face at 150 and 200 percent, K's
/// screen, while the stock text around it is the game's serif: the Reviewer's pre-read, October 22nd)
const EFonts LABEL_FONT = FONT_SMALL;
}

TurnProgressBar::TurnProgressBar(const Point & position, PlayerColor player)
	: CIntObject(TIME)
	, player(player)
{
	// the name line above the frame: its centre NAME_ABOVE over the frame, half a line and the outline above that
	const int nameHeight = static_cast<int>(ENGINE->renderHandler().loadFont(LABEL_FONT)->getLineHeight());
	const int above = std::max(16, NAME_ABOVE + nameHeight / 2 + 2);
	pos += position;
	frame = Rect(END_PIECE, above, FRAME_SIZE.x, FRAME_SIZE.y);
	pos.w = FRAME_SIZE.x + 2 * END_PIECE;
	pos.h = above + FRAME_SIZE.y;
	recActions &= ~DEACTIVATE;
	readName();
}

PlayerColor TurnProgressBar::getPlayer() const
{
	return player;
}

Point TurnProgressBar::frameOffset() const
{
	return frame.topLeft();
}

void TurnProgressBar::readName()
{
	// the name the lobby shows for this player; the colour's name when the lobby has none
	playerName = player.toString();
	if(GAME->interface() && GAME->interface()->cb)
	{
		const auto & infos = GAME->interface()->cb->getStartInfo()->playerInfos;
		const auto found = infos.find(player);
		if(found != infos.end() && !found->second.name.empty())
			playerName = found->second.name;
	}
	fitName();
}

void TurnProgressBar::fitName()
{
	// the name is centred over the frame; a wider one starts at pos's left edge and pos reaches as far right as it does, with its outline
	const int nameWidth = playerName.empty() ? 0 : static_cast<int>(ENGINE->renderHandler().loadFont(LABEL_FONT)->getStringWidth(playerName)) + 2;
	const int frameReach = frame.x + frame.w + END_PIECE;
	const int nameReach = std::max(frame.x + frame.w / 2 + nameWidth / 2, nameWidth) + 1;
	// pos never narrows: the map paints fresh only the tiles under pos, so a longer name drawn before (a teammate's) would stay on the map
	// past a narrower pos until those tiles changed
	pos.w = std::max({pos.w, frameReach, nameReach});
}

void TurnProgressBar::takeSnapshot(int day)
{
	heroes.clear();
	towns.clear();
	snapshotTaken = false;
	snapshotDay = day;
	percent = 0;
	shown = 0;

	const auto & cb = GAME->interface()->cb;
	// this client's game state holds every player's heroes and towns (stock sends every pack to every client); the callback hands out the state of a player who is not this
	// client's enemy, so it is the player's own and an ally's (CGameInfoCallback::hasAccess)
	const PlayerState * state = cb->getPlayerState(player, false);
	if(!state)
	{
		// the list holds only players the callback grants (TurnProgressBars::members), so this is a fault: said in the log, and the bar stays empty until the next day
		logGlobal->error("Turn bar: no game state for player %s on day %d; the bar stays empty this day", player.toString(), day);
		return;
	}

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
}

void TurnProgressBar::update()
{
	if(!GAME->interface() || !GAME->interface()->cb)
		return;
	const auto & cb = GAME->interface()->cb;
	// the new day has reset every player's movement, mana and building in the game state (NewTurn), before any player acts on it: one snapshot a day serves
	// simultaneous, sequential and hot-seat turns alike. A game loaded from a save has no snapshot yet: it is taken now, and the bar reads what is done since
	const int day = cb->getDate(Date::DAY);
	if(day != snapshotDay)
		takeSnapshot(day);
	acting = cb->isPlayerMakingTurn(player);
}

void TurnProgressBar::endTurn()
{
	if(!GAME->interface() || !GAME->interface()->cb)
		return;
	endedDay = GAME->interface()->cb->getDate(Date::DAY);
	if(endedDay == snapshotDay)
		percent = 100;
}

int TurnProgressBar::compute() const
{
	if(endedDay == snapshotDay)
		return 100;
	if(!snapshotTaken || !GAME->interface() || !GAME->interface()->cb)
		return 0;
	const auto & cb = GAME->interface()->cb;
	const PlayerState * state = cb->getPlayerState(player, false);
	if(!state)
		return 0;
	// a category is done when nothing useful is left in it (the lead for K, October 23rd): what is still possible is read from the game state every client has,
	// so the same player reads the same on every client
	const ResourceSet resources = state->resources;
	std::vector<TurnProgressMath::HeroNow> heroesNow;
	for(const CGHeroInstance * hero : state->getHeroes())
	{
		TurnProgressMath::HeroNow now;
		now.id = hero->id.getNum();
		now.movement = hero->movementPointsRemaining();
		now.mana = static_cast<int>(hero->mana);
		now.canMove = canStillMove(hero);
		now.canCast = false;
		if(hero->hasSpellbook())
		{
			for(const SpellID & id : hero->getSpellsInSpellbook())
			{
				const auto * spell = id.toSpell();
				if(spell && spell->isAdventure() && hero->getSpellCost(spell) <= hero->mana)
				{
					now.canCast = true;
					break;
				}
			}
		}
		heroesNow.push_back(now);
	}
	std::vector<TurnProgressMath::TownNow> townsNow;
	for(const CGTownInstance * town : state->getTowns())
	{
		TurnProgressMath::TownNow now;
		now.id = town->id.getNum();
		now.built = town->built;
		for(const auto & level : town->creatures)
			now.available.push_back(static_cast<int>(level.first));
		now.canBuild = false;
		if(town->built == 0)
		{
			for(const auto & building : town->getTown()->buildings)
			{
				if(cb->canBuildStructure(town, building.first) == EBuildingState::ALLOWED)
				{
					now.canBuild = true;
					break;
				}
			}
		}
		now.canRecruit = false;
		for(const auto & level : town->creatures)
		{
			if(level.first == 0)
				continue;
			const bool affordable = std::any_of(level.second.begin(), level.second.end(), [&resources](const CreatureID & id)
			{
				const auto * creature = id.toCreature();
				return creature && resources.canAfford(creature->getFullRecruitCost());
			});
			if(affordable)
			{
				now.canRecruit = true;
				break;
			}
		}
		townsNow.push_back(now);
	}
	return TurnProgressMath::percent(heroes, towns, heroesNow, townsNow);
}

bool TurnProgressBar::canStillMove(const CGHeroInstance * hero)
{
	// a step to one of the eight neighbouring tiles that the movement left pays for, by the pathfinder's own cost rule (the diagonal exception included);
	// blocked tiles are not looked at, so a hero next to a free tile always counts as able to move
	const int left = hero->movementPointsRemaining();
	if(left <= 0 || !GAME->interface() || !GAME->interface()->cb)
		return false;
	const auto & cb = GAME->interface()->cb;
	const int3 at = hero->visitablePos();
	const TerrainTile * from = cb->getTile(at, false);
	if(!from)
		return true;
	PathfinderOptions options(*cb);
	CPathfinderHelper helper(*cb, hero, options);
	for(int dx = -1; dx <= 1; ++dx)
	{
		for(int dy = -1; dy <= 1; ++dy)
		{
			if(dx == 0 && dy == 0)
				continue;
			const int3 to(at.x + dx, at.y + dy, at.z);
			const TerrainTile * tile = cb->isInTheMap(to) ? cb->getTile(to, false) : nullptr;
			if(tile && helper.getMovementCost(at, to, from, tile, left, false) <= left)
				return true;
		}
	}
	return false;
}

void TurnProgressBar::tick(uint32_t msPassed)
{
	clockMs += msPassed;
	sinceUpdate += msPassed;
	// the drawn percent eases toward the real one: the liquid rises instead of jumping
	shown += (percent - shown) * std::min(1.0, msPassed / 180.0);
	// the day and whether the player acts, at every frame; the percent four times a second
	update();
	if(sinceUpdate < 250)
		return;
	sinceUpdate = 0;
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
		const auto & font = ENGINE->renderHandler().loadFont(LABEL_FONT);
		kept.text = text;
		kept.size = Point(static_cast<int>(font->getStringWidth(text)) + 2, static_cast<int>(font->getLineHeight()) + 2);
		kept.image = std::make_shared<Canvas>(kept.size, CanvasScalingPolicy::AUTO);
		for(int dx = -1; dx <= 1; ++dx)
			for(int dy = -1; dy <= 1; ++dy)
				if(dx != 0 || dy != 0)
					kept.image->drawText(Point(1 + dx, 1 + dy), LABEL_FONT, Colors::BLACK, ETextAlignment::TOPLEFT, text);
		kept.image->drawText(Point(1, 1), LABEL_FONT, Colors::WHITE, ETextAlignment::TOPLEFT, text);
		kept.image->applyTransparency(true);
	}
	to.draw(*kept.image, centre - kept.size / 2);
}

int TurnProgressBar::trimStyle(const PlayerColor & player)
{
	// the trim candidates (October 22nd, K: the bar's trim must match the game's own ornate frame): 0 the first trim, 1 the stock red and gold band, 2 the stock gold band;
	// chosen by DMB_BAR_TRIM while the candidates are compared
	static const char * value = std::getenv("DMB_BAR_TRIM");
	if(value && std::string(value) == "mix")
		return player.getNum() % 3;   // the candidates side by side, one per player
	return value ? std::atoi(value) : 1;
}

void TurnProgressBar::drawStockTrim(Canvas & to, const Rect & outer, int style)
{
	// the game's own frame art: AdvMap.pcx is the adventure screen's frame, and the places below are the parts of it that never hold anything else (the top rail, the
	// corner jewel); it is recoloured to this bar's player, as the screen's frame is recoloured to the player whose turn it is
	if(!trimArt)
	{
		trimArt = ENGINE->renderHandler().loadImage(ImagePath::builtin("ADVMAP"), EImageBlitMode::OPAQUE);
		if(trimArt && player.isValidPlayer())
			trimArt->playerColored(player);
	}
	const auto & art = trimArt;
	if(!art)
		return;
	// the top rail of the screen frame, rows 1 to 6 at x 300 and on: a pale gold line, its shade, two rows of red enamel, a light and a mid gold line. A rail is
	// the rows in order, the bottom one in the opposite order, so the light falls from the top as it does on the screen frame
	static const int bandRows[4] = {3, 4, 5, 6};    // style 1: enamel and gold, four rows
	static const int goldRows[4] = {1, 2, 5, 6};    // style 2: gold only (highlight, shade, light, mid)
	const int * rows = style == 1 ? bandRows : goldRows;
	const int srcX = 300;
	// the gems' size: the screen frame's corner jewel, cut as a disc of this radius and centred a pixel outside each side
	const int radius = 7;   // the jewel stays inside the frame's 18 pixels (at 8 its ring stood out by a pixel above and below)
	// the ends (K, October 22nd 17:5x: "the corners of the progress bars look like they need some fill on the corners"): the rails run on to the gems' outer edge
	// and the sides close the frame there, so each gem is set in the frame's metal and the corners above and below it show the rails and the side instead of the
	// terrain under the bar
	const int reach = radius + 1;
	const Rect closed(outer.x - reach, outer.y, outer.w + 2 * reach, outer.h);
	for(int i = 0; i < 4; ++i)
	{
		to.draw(art, Point(closed.x, closed.y + i), Rect(srcX, rows[i], closed.w, 1));
		to.draw(art, Point(closed.x, closed.y + closed.h - 1 - i), Rect(srcX, rows[i], closed.w, 1));
	}
	// the sides: the screen frame's left rail, seven columns wide; its first four columns (shade, gold, light gold, enamel) go left, mirrored on the right
	static const int sideCols[4] = {1, 2, 3, 4};
	for(int i = 0; i < 4; ++i)
	{
		to.draw(art, Point(closed.x + i, closed.y + 4), Rect(sideCols[i], 200, 1, closed.h - 8));
		to.draw(art, Point(closed.x + closed.w - 1 - i, closed.y + 4), Rect(sideCols[i], 200, 1, closed.h - 8));
	}
	// the gems: the screen frame's corner jewel, a round red stone in a silver and gold ring, cut as a disc and set over each end, centred a pixel outside the
	// frame's side on both ends (the right one stood a pixel further out than the left)
	const Point jewel(15, 14);    // the centre of the stone in AdvMap.pcx
	for(const bool right : {false, true})
	{
		const int cx = right ? outer.x + outer.w : outer.x - 1;
		const int cy = outer.y + outer.h / 2;
		for(int dy = -radius; dy <= radius; ++dy)
		{
			const int half = static_cast<int>(std::lround(std::sqrt(static_cast<double>(radius * radius - dy * dy) + 0.25)));
			to.draw(art, Point(cx - half, cy + dy), Rect(jewel.x - half, jewel.y + dy, 2 * half + 1, 1));
		}
	}
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
	const int style = trimStyle(player);
	const int rim = style == 0 ? 3 : 4;
	const Rect inner(outer.x + rim, outer.y + rim, outer.w - 2 * rim, outer.h - 2 * rim);

	// the map has painted the terrain under pos fresh in this frame (AdventureMapInterface::show): the glass darkens it 30 percent, so 70 percent of the terrain shows
	to.drawColorBlended(inner, ColorRGBA(0, 0, 0, 77));

	const double t = clockMs / 1000.0;
	// the liquid is bright while the player acts and dimmed while the player waits or has ended the turn
	const double dim = acting ? 1.0 : 0.55;
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
				const ColorRGBA tone = shade(base, (1.30 - 0.85 * depth) * dim, 255);
				to.drawColor(Rect(inner.x, inner.y + row, body, 1), tone);
				for(int i = 0; i < ripple; ++i)
				{
					const double lift = 1.4 * std::sin(t * 4.0 + i * 1.1) * (1.0 + i * 0.25);
					const int top = std::clamp(static_cast<int>(std::lround((ripple - 1 - i) * 0.0 + (i + 1) * 0.5 + lift)), 0, inner.h - 3);
					if(row >= top)
						to.drawColor(Rect(inner.x + body + i, inner.y + row, 1, 1), tone);
				}
			}
			to.drawColor(Rect(inner.x, inner.y + 1, body, 1), shade(base, 1.75 * dim, 255)); // the soft highlight under the surface
		}
	}

	if(style == 0)
	{
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

	}
	else
		drawStockTrim(to, outer, style);

	{
		// the percent and the player's name, white with a solid black outline so they read over any terrain
		drawOutlined(to, percentText, std::to_string(static_cast<int>(std::lround(shown))) + "%", Point(outer.x + outer.w / 2, outer.y + outer.h / 2));
		if(!playerName.empty())
		{
			// centred over the frame, or from pos's left edge when wider (fitName)
			const int nameWidth = static_cast<int>(ENGINE->renderHandler().loadFont(LABEL_FONT)->getStringWidth(playerName)) + 2;
			drawOutlined(to, nameText, playerName, Point(std::max(outer.x + outer.w / 2, pos.x + 1 + nameWidth / 2), outer.y - NAME_ABOVE));
		}
	}
}

TurnProgressBars::TurnProgressBars(std::optional<Rect> timer, const Rect & mapArea, int humans)
	: CIntObject(TIME)
	, timer(timer)
	, mapArea(mapArea)
	, humans(humans)
{
	recActions &= ~DEACTIVATE;
	refresh();
}

std::vector<PlayerColor> TurnProgressBars::members() const
{
	std::vector<PlayerColor> result;
	if(!GAME->interface() || !GAME->interface()->cb)
		return result;
	const auto & cb = GAME->interface()->cb;
	const PlayerColor viewer = GAME->interface()->playerID;
	// a spectator plays no turn of its own (its colour is no player's, and the game holds no state for it): no bar
	if(!viewer.isValidPlayer())
		return result;
	// a player still in the game whose state the callback grants (the viewer and the viewer's allies, CGameInfoCallback::hasAccess); an eliminated one loses the bar
	const auto inGame = [&cb](PlayerColor player)
	{
		const PlayerState * state = cb->getPlayerState(player, false);
		return state && state->status == EPlayerStatus::INGAME;
	};
	if(inGame(viewer))
		result.push_back(viewer);
	for(auto player = PlayerColor(0); player < PlayerColor::PLAYER_LIMIT; ++player)
		if(player != viewer && cb->getPlayerRelations(viewer, player) == PlayerRelations::ALLIES && inGame(player))
			result.push_back(player);
	// one human and no ally of the viewer: no bar, as in beta 15
	if(humans <= 1 && result.size() <= 1)
		result.clear();
	return result;
}

void TurnProgressBars::refresh()
{
	const auto wanted = members();
	bool changed = wanted.size() != bars.size();
	for(size_t i = 0; !changed && i < wanted.size(); ++i)
		changed = bars[i]->getPlayer() != wanted[i];
	if(!changed)
		return;

	OBJECT_CONSTRUCTION;
	std::vector<std::shared_ptr<TurnProgressBar>> next;
	std::vector<bool> kept;
	for(const PlayerColor player : wanted)
	{
		// a bar that stays keeps its snapshot and its kept text
		const auto found = std::find_if(bars.begin(), bars.end(), [player](const auto & bar){ return bar->getPlayer() == player; });
		kept.push_back(found != bars.end());
		next.push_back(found != bars.end() ? *found : std::make_shared<TurnProgressBar>(Point(0, 0), player));
	}
	for(const auto & bar : bars)
		if(std::find(next.begin(), next.end(), bar) == next.end())
			vacated.push_back(bar->pos);
	bars = std::move(next);
	if(bars.empty())
		return;

	// placed by what each draws: with the timer, the first bar's top is 6 pixels below the timer's bottom and its frame lines up with the timer's left edge; without
	// one, its frame is where the timer would be
	const Point frame = bars.front()->frameOffset();
	const Point origin = timer ? Point(timer->x - frame.x, timer->y + timer->h + 6) : Point(24, 24) - frame;
	std::vector<TurnProgressMath::Size> sizes;
	for(const auto & bar : bars)
		sizes.push_back({bar->pos.w, bar->pos.h});
	const auto places = TurnProgressMath::layout(sizes, {origin.x, origin.y}, 6, mapArea.y + mapArea.h / 2);
	for(size_t i = 0; i < bars.size(); ++i)
	{
		const Point place(places[i].x, places[i].y);
		if(bars[i]->pos.topLeft() == place)
			continue;
		if(kept[i])
			vacated.push_back(bars[i]->pos);
		bars[i]->moveTo(place);
	}
}

void TurnProgressBars::tick(uint32_t)
{
	refresh();
}

std::vector<Rect> TurnProgressBars::takeRepaintAreas()
{
	std::vector<Rect> areas = std::move(vacated);
	vacated.clear();
	for(const auto & bar : bars)
		areas.push_back(bar->pos);
	return areas;
}

void TurnProgressBars::update()
{
	for(const auto & bar : bars)
		bar->update();
}

void TurnProgressBars::endTurn(PlayerColor player)
{
	for(const auto & bar : bars)
		if(bar->getPlayer() == player)
			bar->endTurn();
}
