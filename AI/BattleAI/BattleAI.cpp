/*
 * BattleAI.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleAI.h"
#include "../../lib/DmbIssueReport.h"
#include "BattleEvaluator.h"
#include "BattleExchangeVariant.h"

#include "StackWithBonuses.h"
#include "EnemyInfo.h"
#include "tbb/parallel_for.h"
#include "../../lib/CStopWatch.h"
#include "../../lib/CThreadHelper.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/IGameInfoCallback.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/BattleStateInfoForRetreat.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/mapObjects/army/CCommanderInstance.h"
#include "../../lib/StartInfo.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/CStack.h" // TODO: remove
                              // Eventually only IBattleInfoCallback and battle::Unit should be used,
                              // CUnitState should be private and CStack should be removed completely
#include "../../lib/logging/VisualLogger.h"

#define LOGL(text) print(text)
#define LOGFL(text, formattingEl) print(boost::str(boost::format(text) % formattingEl))

CBattleAI::CBattleAI()
	: side(BattleSide::NONE),
	wasWaitingForRealize(false)
{
}

CBattleAI::~CBattleAI()
{
	if(cb)
	{
		//Restore previous state of CB - it may be shared with the main AI (like VCAI)
		cb->waitTillRealize = wasWaitingForRealize;
	}
}

void logHexNumbers()
{
#if BATTLE_TRACE_LEVEL >= 1
	logVisual->updateWithLock("hexes", [](IVisualLogBuilder & b)
		{
			for(BattleHex hex = BattleHex(0); hex < GameConstants::BFIELD_SIZE; ++hex)
				b.addText(hex, std::to_string(hex.toInt()));
		});
#endif
}

void CBattleAI::initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB)
{
	env = ENV;
	cb = CB;
	playerID = *CB->getPlayerID();
	wasWaitingForRealize = CB->waitTillRealize;
	CB->waitTillRealize = false;
	movesSkippedByDefense = 0;

	logHexNumbers();
}

void CBattleAI::initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB, AutocombatPreferences autocombatPreferences)
{
	initBattleInterface(ENV, CB);
	autobattlePreferences = autocombatPreferences;
}

BattleAction CBattleAI::useHealingTent(const BattleID & battleID, const CStack *stack)
{
	auto healingTargets = cb->getBattle(battleID)->battleGetStacks(CBattleInfoEssentials::ONLY_MINE);
	std::map<int, const CStack*> woundHpToStack;
	for(const auto * stack : healingTargets)
	{
		if(auto woundHp = stack->getMaxHealth() - stack->getFirstHPleft())
			woundHpToStack[woundHp] = stack;
	}

	if(woundHpToStack.empty())
		return BattleAction::makeDefend(stack);
	else
		return BattleAction::makeHeal(stack, woundHpToStack.rbegin()->second); //last element of the woundHpToStack is the most wounded stack
}

void CBattleAI::yourTacticPhase(const BattleID & battleID, int distance)
{
	cb->battleMakeTacticAction(battleID, BattleAction::makeEndOFTacticPhase(cb->getBattle(battleID)->battleGetTacticsSide()));
}

static float getStrengthRatio(std::shared_ptr<CBattleInfoCallback> cb, BattleSide side)
{
	auto stacks = cb->battleGetAllStacks();
	auto our = 0;
	auto enemy = 0;

	for(auto stack : stacks)
	{
		auto creature = stack->creatureId().toCreature();

		if(!creature)
			continue;

		if(stack->unitSide() == side)
			our += stack->getCount() * creature->getAIValue();
		else
			enemy += stack->getCount() * creature->getAIValue();
	}

	return enemy == 0 ? 1.0f : static_cast<float>(our) / enemy;
}

int getSimulationTurnsCount(const StartInfo * startInfo)
{
	return startInfo->difficulty < 4 ? 2 : 10;
}

void CBattleAI::activeStack(const BattleID & battleID, const CStack * stack )
{
	LOG_TRACE_PARAMS(logAi, "stack: %s", stack->nodeName());

	auto timeElapsed = [](std::chrono::time_point<std::chrono::high_resolution_clock> start) -> uint64_t
	{
		auto end = std::chrono::high_resolution_clock::now();

		return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
	};

	BattleAction result = BattleAction::makeDefend(stack);

	// DMB STOPGAP, NOT A FIX: the cause of the refused-spell loop is not found. The AI asks the server's own applicability functions (CSpell::canBeCast,
	// Mechanics::canBeCastAt), so the loop means the client's and the server's picture of some unit differ; the cause is on the queue. Diagnostics (DMBDIAG lines: the
	// server's per-unit dump when it refuses a hero spell, the AI's dump when it repeats a cast) ran 28 games of the lane game's setup on October 17th and caught no refusal.
	// The guard below only ends a turn that would otherwise never end.
	// DMB: what the battle looks like now (every stack: id, hex, count, hit points of the top creature, and its turn state: moved, waited, defended, still to move). A stack that is
	// offered again while this is the same as at its last offer had nothing it did accepted: a refused spell cast is the known case (see the members in BattleAI.h). The turn state
	// is part of it so that an accepted wait or defend, which changes no hex, count or hit point, is not taken for a refusal (a standoff of two stacks that both defend)
	uint64_t signature = 1469598103934665603ULL;
	for(const auto * unit : cb->getBattle(battleID)->battleGetAllStacks())
	{
		const uint64_t turnState = (unit->defended() ? 1 : 0) | (unit->moved() ? 2 : 0) | (unit->waited() ? 4 : 0) | (unit->willMove() ? 8 : 0);
		for(const uint64_t part : {static_cast<uint64_t>(unit->unitId()), static_cast<uint64_t>(unit->getPosition().toInt() + 2), static_cast<uint64_t>(unit->getCount()), static_cast<uint64_t>(unit->getFirstHPleft()), turnState})
			signature = (signature ^ part) * 1099511628211ULL;
	}
	if(battleID.getNum() == lastOfferedBattle && stack->unitId() == lastOfferedStack && signature == lastOfferedSignature)
		++unchangedOffers;
	else
		unchangedOffers = 0;
	lastOfferedBattle = battleID.getNum();
	lastOfferedStack = stack->unitId();
	lastOfferedSignature = signature;

	auto start = std::chrono::high_resolution_clock::now();

	if(unchangedOffers >= 6)
	{
		// six offers in a row with nothing changed: the decisions are being refused, the turn is ended with the one action that always stands
		logAi->warn("BattleAI: %s was offered %d times with the battle unchanged: defending", stack->nodeName(), unchangedOffers);
		DmbIssueReport::send("battle-ai-guard", "The battle AI forced a defend: " + stack->nodeName() + " was offered " + std::to_string(unchangedOffers) + " times with nothing changed");
		cb->battleMakeUnitAction(battleID, result);
		return;
	}

	if(stack->creatureId() == CreatureID::CATAPULT)
	{
		cb->battleMakeUnitAction(battleID, useCatapult(battleID, stack));
		return;
	}
	if(stack->hasBonusOfType(BonusType::SIEGE_WEAPON) && stack->hasBonusOfType(BonusType::HEALER))
	{
		cb->battleMakeUnitAction(battleID, useHealingTent(battleID, stack));
		return;
	}

#if BATTLE_TRACE_LEVEL>=1
	logAi->trace("Build evaluator and targets");
#endif

	BattleEvaluator evaluator(
		env, cb, stack, playerID, battleID, side,
		getStrengthRatio(cb->getBattle(battleID), side),
		getSimulationTurnsCount(env->game()->getStartInfo()));

	result = evaluator.selectStackAction(stack);

	// after two offers with nothing changed no spell is tried again: the plain action decided above is what is left
	if(autobattlePreferences.enableSpellsUsage && unchangedOffers < 2 && evaluator.canCastSpell())
	{
		auto spelCasted = evaluator.attemptCastingSpell(stack);

		if(spelCasted)
			return;
	}

	logAi->trace("Spellcast attempt completed in %lld", timeElapsed(start));

	if(auto action = considerFleeingOrSurrendering(battleID))
	{
		cb->battleMakeUnitAction(battleID, *action);
		return;
	}

	if(result.actionType == EActionType::DEFEND)
	{
		movesSkippedByDefense++;
	}
	else if(result.actionType != EActionType::WAIT)
	{
		movesSkippedByDefense = 0;
	}

	logAi->trace("BattleAI decision made in %lld", timeElapsed(start));

	cb->battleMakeUnitAction(battleID, result);
}

BattleAction CBattleAI::useCatapult(const BattleID & battleID, const CStack * stack)
{
	BattleAction attack;
	BattleHex targetHex = BattleHex::INVALID;

	if(cb->getBattle(battleID)->battleGetGateState() == EGateState::CLOSED)
	{
		targetHex = cb->getBattle(battleID)->wallPartToBattleHex(EWallPart::GATE);
	}
	else
	{
		std::array wallParts {
			EWallPart::KEEP,
			EWallPart::BOTTOM_TOWER,
			EWallPart::UPPER_TOWER,
			EWallPart::BELOW_GATE,
			EWallPart::OVER_GATE,
			EWallPart::BOTTOM_WALL,
			EWallPart::UPPER_WALL
		};

		for(auto wallPart : wallParts)
		{
			auto wallState = cb->getBattle(battleID)->battleGetWallState(wallPart);

			if(wallState != EWallState::NONE && wallState != EWallState::DESTROYED)
			{
				targetHex = cb->getBattle(battleID)->wallPartToBattleHex(wallPart);
				break;
			}
		}
	}

	if(!targetHex.isValid())
	{
		return BattleAction::makeDefend(stack);
	}

	attack.aimToHex(targetHex);
	attack.actionType = EActionType::CATAPULT;
	attack.side = side;
	attack.stackNumber = stack->unitId();

	movesSkippedByDefense = 0;

	return attack;
}

void CBattleAI::battleStart(const BattleID & battleID, const CCreatureSet *army1, const CCreatureSet *army2, int3 tile, const CGHeroInstance *hero1, const CGHeroInstance *hero2, BattleSide Side, bool replayAllowed)
{
	LOG_TRACE(logAi);
	side = Side;
}

void CBattleAI::print(const std::string &text) const
{
	logAi->trace("%s Battle AI[%p]: %s", playerID.toString(), this, text);
}

// DMB: the basegame's retreat trigger, "if the AI thinks the player is going to lose after the current move" (Heroes 3 wiki, Retreat
// and Combat pages). Each enemy stack that can shoot, or reach a hex beside one of ours within its movement range, deals its estimated
// average damage once, to the stack of ours it hurts most (capped at that stack's remaining health); true when nothing of ours is
// left. Before this no battle AI set the field, so every AI that reads it (OmniAI) never retreated. Only the units that
// battleIsFinished counts are counted, on both sides: a turret, a war machine (SIEGE_WEAPON), a clone, a summoned unit or a
// commander neither keeps a side in the battle nor is counted as an attacker here.
static bool countsForResult(const battle::Unit * unit)
{
	return unit->alive() && !unit->isTurret() && !unit->hasBonusOfType(BonusType::SIEGE_WEAPON) && !unit->isClone()
		&& !unit->acquireState()->summoned && !dynamic_cast<const CCommanderInstance *>(unit);
}

static bool lastTurnBeforeDie(const CPlayerBattleCallback & battle, const battle::Units & ours, const battle::Units & theirs)
{
	std::map<uint32_t, int64_t> left;
	int64_t total = 0;
	for(const auto * unit : ours)
	{
		if(!countsForResult(unit))
			continue;
		left[unit->unitId()] = unit->getAvailableHealth();
		total += unit->getAvailableHealth();
	}
	if(total <= 0)
		return false;
	for(const auto * enemy : theirs)
	{
		if(!countsForResult(enemy))
			continue;
		const bool shooter = battle.battleCanShoot(enemy);
		ReachabilityInfo reach;
		if(!shooter)
			reach = battle.getReachability(enemy);
		const battle::Unit * target = nullptr;
		int64_t targetDamage = 0;
		for(const auto * unit : ours)
		{
			if(!left.count(unit->unitId()))
				continue;
			const int64_t health = left[unit->unitId()];
			if(health <= 0)
				continue;
			if(!shooter && reach.distToNearestNeighbour(unit->getHexes()) > enemy->getMovementRange())
				continue;
			const DamageEstimation estimate = battle.battleEstimateDamage(enemy, unit, 0);
			const int64_t damage = std::min<int64_t>(health, (estimate.damage.min + estimate.damage.max) / 2);
			if(damage > targetDamage)
			{
				targetDamage = damage;
				target = unit;
			}
		}
		if(target)
		{
			left[target->unitId()] -= targetDamage;
			total -= targetDamage;
		}
	}
	return total <= 0;
}

std::optional<BattleAction> CBattleAI::considerFleeingOrSurrendering(const BattleID & battleID)
{
	BattleStateInfoForRetreat bs;

	bs.canFlee = cb->getBattle(battleID)->battleCanFlee();
	bs.canSurrender = cb->getBattle(battleID)->battleCanSurrender(playerID);
	bs.ourSide = cb->getBattle(battleID)->battleGetMySide();
	bs.ourHero = cb->getBattle(battleID)->battleGetMyHero();
	bs.enemyHero = nullptr;

	for(auto stack : cb->getBattle(battleID)->battleGetAllStacks(false))
	{
		if(stack->alive())
		{
			if(stack->unitSide() == bs.ourSide)
				bs.ourStacks.push_back(stack);
			else
			{
				bs.enemyStacks.push_back(stack);
				bs.enemyHero = cb->getBattle(battleID)->battleGetOwnerHero(stack);
			}
		}
	}

	bs.turnsSkippedByDefense = movesSkippedByDefense / bs.ourStacks.size();

	if(!bs.canFlee && !bs.canSurrender)
	{
		return std::nullopt;
	}

	// ai.battleLastTurnEstimate (default true): the OmniAI benchmark lanes keep it false, so that an OmniAI build that retreats on
	// this field does not start doing so in the middle of a measurement (the Opus Reviewer, 2026-10-23).
	if(settings["ai"]["battleLastTurnEstimate"].Bool())
		bs.isLastTurnBeforeDie = lastTurnBeforeDie(*cb->getBattle(battleID), bs.ourStacks, bs.enemyStacks);
	if(bs.isLastTurnBeforeDie)
		logAi->info("Battle AI: round %d, side %d expects to lose after the current move (our strength %d, enemy %d)",
			cb->getBattle(battleID)->battleGetRound(), static_cast<int>(bs.ourSide), bs.getOurStrength(), bs.getEnemyStrength());

	auto result = cb->makeSurrenderRetreatDecision(battleID, bs);
	if(result)
		logAi->info("Battle AI: round %d, side %d retreats or surrenders (our strength %d, enemy %d)",
			cb->getBattle(battleID)->battleGetRound(), static_cast<int>(bs.ourSide), bs.getOurStrength(), bs.getEnemyStrength());

	// DMB: the 30-defend-turn retreat is VCMI's guard against a battle that never ends; the basegame has no such rule and no round
	// limit (a battle ends by destruction, retreat or surrender). A human player's side, in auto or quick combat, never retreats by
	// it: that is the player's decision. AI sides keep the guard.
	const bool humanSide = env->game()->getStartInfo()->getIthPlayersSettings(playerID).isControlledByHuman();
	if(!result && !humanSide && bs.canFlee && bs.turnsSkippedByDefense > 30)
	{
		logAi->info("Battle AI: round %d, side %d retreats after %d defend turns per stack", cb->getBattle(battleID)->battleGetRound(),
			static_cast<int>(bs.ourSide), bs.turnsSkippedByDefense);
		return BattleAction::makeRetreat(bs.ourSide);
	}

	return result;
}



