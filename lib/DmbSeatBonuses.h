/*
 * DmbSeatBonuses.h, part of VCMI engine (DMB addition)
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

VCMI_LIB_NAMESPACE_BEGIN

/// DMB (S380, K 11-19: "Yes, build all of them in this order: your four ticked ones first, then the rest"): the number rows of the S330 seat
/// bonus table that a seat stores by name (Handicap::seatValues). The lobby's More Bonuses window shows them and the game start
/// (CGameState) applies them; both read the limits from here. The ranges are the S330 table's, which K approved with it.
struct DmbSeatBonusRow
{
	const char * key;
	int row; ///< the row of the S330 table
	int min;
	int max;
	int neutral; ///< the value that changes nothing; a seat does not store it
};

inline const std::vector<DmbSeatBonusRow> & dmbSeatBonusRows()
{
	static const std::vector<DmbSeatBonusRow> rows = {
		{"manaRegen", 6, 0, 10, 0},
		{"experience", 7, 50, 200, 100},
		{"attack", 8, 0, 5, 0},
		{"defense", 8, 0, 5, 0},
		{"power", 8, 0, 5, 0},
		{"knowledge", 8, 0, 5, 0},
		{"luck", 9, -3, 3, 0},
		{"morale", 9, -3, 3, 0},
		{"scouting", 10, 0, 5, 0},
		{"speed", 11, 0, 2, 0},
		{"health", 12, 0, 25, 0},
		{"damage", 13, -25, 25, 0},
		{"daily0", 14, 0, 2, 0}, // wood; the number is the resource's index
		{"daily1", 14, 0, 2, 0}, // mercury
		{"daily2", 14, 0, 2, 0}, // ore
		{"daily3", 14, 0, 2, 0}, // sulfur
		{"daily4", 14, 0, 2, 0}, // crystal
		{"daily5", 14, 0, 2, 0}, // gems
		{"daily6", 14, 0, 2000, 0}, // gold
		{"necromancy", 15, 0, 20, 0},
		{"creatureExperience", 16, 50, 200, 100},
		{"surrender", 17, 0, 50, 0},
		{"roughGround", 18, 0, 1, 0},
		{"freeBoarding", 18, 0, 1, 0},
		{"waterWalking", 18, 0, 1, 0},
		{"flying", 18, 0, 1, 0},
		{"thievesGuild", 19, 0, 1, 0},
		{"heroLevel", 22, 1, 10, 1},
		{"extraHeroes", 25, 0, 2, 0},
		{"garrisonWeeks", 27, 0, 2, 0},
		{"stackCount", 29, 0, 9999, 0},
		// S495 (K, November 24th, ticked all twenty of the seat options list; S496: "make sure the bonuses among these get put into the bonus
		// settings page"): items 1 to 15, the "row" is the item's number on that list; ranges chosen by DMB Dev from the stock skills they mirror
		{"spellDamage", 101, 0, 100, 0}, // percent, Sorcery gives 5, 10, 15
		{"spellDuration", 102, 0, 10, 0}, // rounds, the Cape of Conjuring gives 3
		{"magicResistance", 103, 0, 100, 0}, // percent, Resistance gives 5, 10, 20
		{"learnBattleSpells", 104, 0, 100, 0}, // percent, Eagle Eye gives 40, 50, 60 (spells up to level 4 as expert)
		{"scholarLevel", 105, 0, 5, 0}, // highest spell level taught between heroes, Scholar gives 2, 3, 4
		{"whirlpool", 106, 0, 1, 0},
		{"noTerrainPenalty", 107, 0, 1, 0},
		{"diplomacy", 108, 0, 3, 0}, // Diplomacy gives 1, 2, 3
		{"markets", 109, 0, 9, 0}, // marketplaces counted on top of the seat's own for trade rates
		{"twoSpells", 110, 0, 1, 0},
		{"tomeSchool", 111, 0, 4, 0}, // 0 none, 1 air, 2 fire, 3 water, 4 earth
		{"wisdom", 112, 0, 3, 0}, // spell levels added to the 2 a hero can learn, Wisdom gives 1, 2, 3
		{"legionGrowth", 113, 0, 10, 0}, // creatures a week for every dwelling level, the Legion pieces give 1 to 5
		{"maxMorale", 114, 0, 1, 0},
		{"maxLuck", 114, 0, 1, 0},
		{"manaPercentRegen", 115, 0, 100, 0}, // percent of the spell points a day, the Wizard's Well gives 100
		{"ballista", 116, 0, 1, 0}, // item 16: the war machines the first hero starts with
		{"ammoCart", 116, 0, 1, 0},
		{"firstAidTent", 116, 0, 1, 0},
	};
	return rows;
}

inline const DmbSeatBonusRow * dmbSeatBonusRow(const std::string & key)
{
	for(const auto & row : dmbSeatBonusRows())
		if(key == row.key)
			return &row;
	return nullptr;
}

VCMI_LIB_NAMESPACE_END
