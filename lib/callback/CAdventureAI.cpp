/*
 * CAdventureAI.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CAdventureAI.h"

#include "CDynLibHandler.h"
#include "IGameInfoCallback.h"
#include "CBattleCallback.h"
#include "../StartInfo.h"
#include <vcmi/Environment.h>

VCMI_LIB_NAMESPACE_BEGIN

CGlobalAI::CGlobalAI()
{
	human = false;
}

void CAdventureAI::battleNewRound(const BattleID & battleID)
{
	battleAI->battleNewRound(battleID);
}

void CAdventureAI::battleCatapultAttacked(const BattleID & battleID, const CatapultAttack & ca)
{
	battleAI->battleCatapultAttacked(battleID, ca);
}

void CAdventureAI::battleStart(const BattleID & battleID, const CCreatureSet * army1, const CCreatureSet * army2, int3 tile,
							   const CGHeroInstance * hero1, const CGHeroInstance * hero2, BattleSide side, bool replayAllowed)
{
	assert(!battleAI);
	assert(cbc);
	// DMB (S002): the seat's own battle AI when the host set one in the lobby and it is installed, else the adventure AI's own choice
	// (Nullkiller 1 and 2 and OmniAI return the launcher's Enemy AI in battles). Every adventure AI that calls this base gets it
	std::string battleName = getBattleAIName();
	const auto me = cbc->getPlayerID();
	const StartInfo * si = env && env->game() ? env->game()->getStartInfo() : nullptr;
	if(si && me)
	{
		const auto seat = si->playerInfos.find(*me);
		if(seat != si->playerInfos.end() && !seat->second.aiBattle.empty())
		{
			if(!CDynLibHandler::findAILibrary(seat->second.aiBattle, true).empty())
				battleName = seat->second.aiBattle;
			else
				logGlobal->warn("Player %s: the seat's battle AI %s is not installed here; %s fights instead", me->toString(), seat->second.aiBattle, battleName);
		}
	}
	battleAI = CDynLibHandler::getNewBattleAI(battleName);
	battleAI->initBattleInterface(env, cbc);
	battleAI->battleStart(battleID, army1, army2, tile, hero1, hero2, side, replayAllowed);
}

void CAdventureAI::battleStacksAttacked(const BattleID & battleID, const std::vector<BattleStackAttacked> & bsa, bool ranged)
{
	battleAI->battleStacksAttacked(battleID, bsa, ranged);
}

void CAdventureAI::actionStarted(const BattleID & battleID, const BattleAction & action)
{
	battleAI->actionStarted(battleID, action);
}

void CAdventureAI::battleNewRoundFirst(const BattleID & battleID)
{
	battleAI->battleNewRoundFirst(battleID);
}

void CAdventureAI::actionFinished(const BattleID & battleID, const BattleAction & action)
{
	battleAI->actionFinished(battleID, action);
}

void CAdventureAI::battleStacksEffectsSet(const BattleID & battleID, const SetStackEffect & sse)
{
	battleAI->battleStacksEffectsSet(battleID, sse);
}

void CAdventureAI::battleTriggerEffect(const BattleID & battleID, const BattleTriggerEffect & bte)
{
    battleAI->battleTriggerEffect(battleID, bte);
}

void CAdventureAI::battleObstaclesChanged(const BattleID & battleID, const std::vector<ObstacleChanges> & obstacles)
{
	battleAI->battleObstaclesChanged(battleID, obstacles);
}

void CAdventureAI::battleStackMoved(const BattleID & battleID, const CStack * stack, const BattleHexArray & dest, int distance, bool teleport)
{
	battleAI->battleStackMoved(battleID, stack, dest, distance, teleport);
}

void CAdventureAI::battleAttack(const BattleID & battleID, const BattleAttack * ba)
{
	battleAI->battleAttack(battleID, ba);
}

void CAdventureAI::battleSpellCast(const BattleID & battleID, const BattleSpellCast * sc)
{
	battleAI->battleSpellCast(battleID, sc);
}

void CAdventureAI::battleEnd(const BattleID & battleID, const BattleResult * br, QueryID queryID)
{
	battleAI->battleEnd(battleID, br, queryID);
	battleAI.reset();
}

void CAdventureAI::battleUnitsChanged(const BattleID & battleID, const std::vector<UnitChanges> & units)
{
	battleAI->battleUnitsChanged(battleID, units);
}

void CAdventureAI::activeStack(const BattleID & battleID, const CStack * stack)
{
	battleAI->activeStack(battleID, stack);
}

void CAdventureAI::yourTacticPhase(const BattleID & battleID, int distance)
{
	battleAI->yourTacticPhase(battleID, distance);
}

VCMI_LIB_NAMESPACE_END
