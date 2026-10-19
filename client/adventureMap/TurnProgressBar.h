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

/// DMB (K, October 16th and 17th): a bar at the top left of the adventure map showing how much of the player's turn is done, in the player's colour over the game's
/// own marble texture. Display only: it reads the game state the client already holds and changes no rule, timer or AI. Measured against a snapshot taken when the
/// player's turn starts: hero movement spent, a structure built in each town that can build one, creatures recruited where some can be, mana spent by heroes who can
/// cast an adventure spell. The mean of the categories that apply; empty when none does; full when the player has ended the turn.
/// Its pos is everything it draws: the frame, the end pieces either side and the player's name above. The map view paints the tiles under pos fresh every frame
/// (AdventureMapInterface::show asks it to), and the bar is drawn over them: the glass darkens the terrain as it is in that frame, and nothing drawn earlier stays.
class TurnProgressBar : public CIntObject
{
	struct HeroStart
	{
		ObjectInstanceID id;
		int movement = 0;
		int mana = 0;
		bool canCast = false;
	};
	struct TownStart
	{
		ObjectInstanceID id;
		bool canBuild = false;
		int recruitable = 0;
		/// the dwelling levels counted in recruitable (the ones whose creature the player could afford at the start): the same levels are summed again later
		std::vector<size_t> levels;
	};

	PlayerColor player;
	std::vector<HeroStart> heroes;
	std::vector<TownStart> towns;
	bool turnEnded = false;
	bool snapshotTaken = false;
	int percent = 0;
	uint32_t sinceUpdate = 0;
	/// the gold frame and what it holds, relative to pos: the end pieces reach past its sides and the name sits above it
	Rect frame;

	/// the bar shows the empty frame: an enemy's turn or an AI's (no colour, no fill)
	bool emptyFrame = false;
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

	int compute() const;
	/// pos widens to the right for a name wider than the frame and its end pieces, so pos stays all the bar draws
	void fitName();
	/// the snapshot of what the player's heroes and towns have now, taken when the turn starts, or at the first tick of a turn that was loaded from a save
	void takeSnapshot();
	void tick(uint32_t msPassed) override;
	void showAll(Canvas & to) override;
	void show(Canvas & to) override;

public:
	/// position is the top left of all the bar draws (its pos)
	TurnProgressBar(const Point & position, PlayerColor player);

	/// where the frame sits inside pos
	Point frameOffset() const;

	/// the player's turn starts: the snapshot is taken and the bar is empty
	void startTurn(PlayerColor newPlayer);
	/// the player ended the turn: the bar shows full
	void endTurn();
	/// another player's turn begins on this client: a human teammate's is tracked (their colour, their progress from the game state this client holds), an enemy's or an AI's shows the empty frame
	void remoteTurn(PlayerColor active, bool human);
};
