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

class CLabel;
class FilledTexturePlayerColored;
class TransparentFilledRectangle;
class SimpleLine;

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
	};

	PlayerColor player;
	std::vector<HeroStart> heroes;
	std::vector<TownStart> towns;
	bool turnEnded = false;
	bool snapshotTaken = false;
	int percent = 0;
	uint32_t sinceUpdate = 0;
	Point barSize;

	std::shared_ptr<FilledTexturePlayerColored> background;
	std::shared_ptr<TransparentFilledRectangle> shade;
	std::shared_ptr<FilledTexturePlayerColored> fill;
	std::vector<std::shared_ptr<SimpleLine>> bevel;
	std::shared_ptr<CLabel> label;

	int compute() const;
	void refresh();
	void tick(uint32_t msPassed) override;

public:
	TurnProgressBar(const Point & position, PlayerColor player);

	/// the player's turn starts: the snapshot is taken and the bar is empty
	void startTurn(PlayerColor newPlayer);
	/// the player ended the turn: the bar shows full
	void endTurn();
};
