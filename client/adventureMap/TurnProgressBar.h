/*
 * TurnProgressBar.h, part of VCMI engine (DMB addition)
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../gui/CIntObject.h"

#include "../../lib/constants/EntityIdentifiers.h"

#include "TurnProgressMath.h"

/// DMB (K, October 16th and 17th): a bar at the top left of the adventure map showing how much of a player's turn is done, in the player's colour over the game's
/// own marble texture. Display only: it reads the game state the client already holds and changes no rule, timer or AI. Measured against a snapshot taken when the
/// day starts (the new day resets every player's movement and building at once, whatever the turn order): K, October 23rd 13:0x, "1. Heroes to move 2. Towns
/// that can be built in": hero movement spent, and a structure built in each town that can build one, a town weighted by what is left on its list
/// (TurnProgressMath::percent and townWeight); full when neither applies, and when the player has ended the turn this day. Bright while the game state counts the
/// player as acting, dimmed otherwise.
/// Its pos is everything it draws: the frame, the end pieces either side and the player's name above. The map view paints the tiles under pos fresh every frame
/// (AdventureMapInterface::show asks it to), and the bar is drawn over them: the glass darkens the terrain as it is in that frame, and nothing drawn earlier stays.
class IImage;

VCMI_LIB_NAMESPACE_BEGIN
class CGHeroInstance;
VCMI_LIB_NAMESPACE_END

class TurnProgressBar : public CIntObject
{
	PlayerColor player;
	std::vector<TurnProgressMath::HeroStart> heroes;
	std::vector<TurnProgressMath::TownStart> towns;
	bool snapshotTaken = false;
	/// the day the snapshot was taken on; a different day in the game state takes a new one
	int snapshotDay = -1;
	/// the day the player ended the turn on; the bar is full while it is the snapshot's day
	int endedDay = -2;
	/// the game state counts the player as acting (CGameState::actingPlayers)
	bool acting = false;
	int percent = 0;
	/// the last percent written to the log (BARPERCENT lines: the same player on the same day must read the same on every client)
	int loggedPercent = -1;
	uint32_t sinceUpdate = 0;
	/// the gold frame and what it holds, relative to pos: the end pieces reach past its sides and the name sits above it
	Rect frame;
	/// the percent as drawn: it eases toward `percent`
	double shown = 0;
	uint32_t clockMs = 0;
	/// the name of the tracked player, drawn above the bar
	std::string playerName;
	/// a text drawn white with a black outline, kept as an image until it changes: the outline is the text drawn nine times, laid out glyph by glyph
	struct OutlinedText
	{
		std::string text;
		std::shared_ptr<Canvas> image;
		Point size;
	};
	OutlinedText percentText;
	OutlinedText nameText;
	/// draws `text` centred on `centre`, from the kept image when it is the same text as last time
	static void drawOutlined(Canvas & to, OutlinedText & kept, const std::string & text, const Point & centre);
	/// which trim is drawn (0 the first trim, 1 and 2 the game's own frame pieces); the corner jewels, rails and gold lines come from AdvMap.pcx at run time
	static int trimStyle(const PlayerColor & player);
	void drawStockTrim(Canvas & to, const Rect & outer, int style);
	/// the screen frame's own picture in this player's colour, as the adventure screen's frame wears it (loaded when first drawn)
	std::shared_ptr<IImage> trimArt;

	/// reads the player's heroes and towns as they are now and hands them, with the snapshot, to TurnProgressMath::percent
	int compute() const;
	/// a step to a neighbouring tile is still affordable with the hero's movement left
	static bool canStillMove(const CGHeroInstance * hero);
	/// the name the lobby shows for the player, and pos fitted to it
	void readName();
	/// pos widens to the right for a name wider than the frame and its end pieces, so pos stays all the bar draws
	void fitName();
	/// the snapshot of what the player's heroes and towns have now, taken when the day changes, or at the first look of a game loaded from a save
	void takeSnapshot(int day);
	void tick(uint32_t msPassed) override;
	void showAll(Canvas & to) override;
	void show(Canvas & to) override;

public:
	/// position is the top left of all the bar draws (its pos)
	TurnProgressBar(const Point & position, PlayerColor player);

	PlayerColor getPlayer() const;
	/// where the frame sits inside pos
	Point frameOffset() const;

	/// reads the game state now: a new day takes a new snapshot, and whether the player is acting
	void update();
	/// the player ended the turn (the server's PlayerEndsTurn, or this client's own end of turn): the bar shows full until the day changes
	void endTurn();
};

/// DMB (K, October 18th: "it should show my allies, their names, their colors, their progress"): the viewing player's bar first, then one for each ally in colour order,
/// AI allies included, stacked downward from the top left of the map, under the turn timer when there is one. Who is listed is read from the game state at every
/// frame: an ally who is eliminated loses the bar, and in hot seat the list follows the player whose screen it is. Shown when more than one human plays or the
/// viewer has an ally. A column ends before half of the map view's height and the next starts to its right.
class TurnProgressBars : public CIntObject
{
	/// the turn timer's area when there is one: the list starts 6 pixels under it, its frames lined up with the timer's left edge
	std::optional<Rect> timer;
	/// the map view's area: the list stays in its top half
	Rect mapArea;
	int humans;
	std::vector<std::shared_ptr<TurnProgressBar>> bars;
	/// where bars were before they moved or went: the map paints those tiles fresh once
	std::vector<Rect> vacated;

	/// the players listed now: the viewer, then the viewer's allies in colour order, each still in the game
	std::vector<PlayerColor> members() const;
	/// matches the bars to the members, keeping each bar that stays, and places them
	void refresh();
	void tick(uint32_t msPassed) override;

public:
	TurnProgressBars(std::optional<Rect> timer, const Rect & mapArea, int humans);

	/// the screen areas whose tiles the map paints fresh this frame: every bar, and where a bar was before it moved or went
	std::vector<Rect> takeRepaintAreas();
	/// reads the game state now (the members, and each bar's day and acting): called when a player's turn starts, before that player can act
	void update();
	/// a player ended the turn
	void endTurn(PlayerColor player);
};
