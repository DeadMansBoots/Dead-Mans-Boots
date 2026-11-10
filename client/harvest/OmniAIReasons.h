/*
 * OmniAIReasons.h, part of VCMI engine (Dead Man's Boots)
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "Harvest.h"

namespace Harvest
{
/// DMB (S093 P4, S077 R6): OmniAI's reasons for its moves. OmniAI writes one decision file per seat it plays,
/// <user data>/OmniAI/decisions-<colour>.jsonl (and .log); its first event carries the game id (gameStartTime and the map, the pair
/// the recorder's game_start line has). At game end this exporter copies every OmniAI file whose start event names this game into
/// <game folder>/omniai/, and nothing else. A game without OmniAI writes nothing.
class OmniAIReasons : public Exporter
{
	std::vector<std::string> copied;

public:
	std::string id() const override { return "omniai.reasons"; }
	std::string name() const override { return "OmniAI's reasons"; }
	std::string description() const override { return "Why each OmniAI seat did what it did, joined to the actions by game and day."; }
	std::string version() const override { return "1"; }

	void gameStart(const Game & game) override;
	void gameEnd(const Game & game, const std::string & why) override;
	std::vector<std::string> manifest() const override { return copied; }
};
}
