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
	Point barSize;

	/// another human's turn, on this client: that player's colour, no percent (this client cannot read the other player's heroes and towns)
	bool remote = false;
	/// no human's turn (an AI plays): nothing is drawn
	bool hidden = false;
	/// the percent as drawn: it eases toward `percent`
	double shown = 0;
	uint32_t clockMs = 0;

	int compute() const;
	/// the snapshot of what the player's heroes and towns have now, taken when the turn starts, or at the first tick of a turn that was loaded from a save
	void takeSnapshot();
	void tick(uint32_t msPassed) override;
	void showAll(Canvas & to) override;
	void show(Canvas & to) override;

public:
	TurnProgressBar(const Point & position, PlayerColor player);

	/// the player's turn starts: the snapshot is taken and the bar is empty
	void startTurn(PlayerColor newPlayer);
	/// the player ended the turn: the bar shows full
	void endTurn();
	/// another player's turn begins on this client: a human's is shown in that player's colour without a percent, an AI's shows nothing
	void remoteTurn(PlayerColor active, bool human);
};
