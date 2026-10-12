/*
 * CRewardableObject.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "CRewardableObject.h"

#include "../CPlayerState.h"
#include "../CConfigHandler.h"
#include "../CCreatureHandler.h"
#include "../GameLibrary.h"
#include "../callback/GameRandomizer.h"
#include "../entities/artifact/CArtifact.h"
#include "../spells/CSpellHandler.h"
#include "../texts/CGeneralTextHandler.h"
#include "../IGameSettings.h"
#include "../battle/BattleLayout.h"
#include "../callback/IGameInfoCallback.h"
#include "../callback/IGameEventCallback.h"
#include "../gameState/CGameState.h"
#include "../mapObjectConstructors/AObjectTypeHandler.h"
#include "../mapObjectConstructors/CRewardableConstructor.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../networkPacks/PacksForClient.h"
#include "../networkPacks/PacksForClientBattle.h"
#include "../networkPacks/StackLocation.h"
#include "../serializer/JsonSerializeFormat.h"

#include <vstd/RNG.h>

#include <mutex>

VCMI_LIB_NAMESPACE_BEGIN

const IObjectInterface * CRewardableObject::getObject() const
{
	return this;
}

void CRewardableObject::markAsScouted(IGameEventCallback & gameEvents, const CGHeroInstance * hero) const
{
	ChangeObjectVisitors cov(ChangeObjectVisitors::VISITOR_ADD_PLAYER, id, hero->id);
	gameEvents.sendAndApply(cov);
}

bool CRewardableObject::isGuarded() const
{
	return stacksCount() > 0;
}

void CRewardableObject::onHeroVisit(IGameEventCallback & gameEvents, const CGHeroInstance *hero) const
{
	if(!wasScouted(hero->getOwner()))
	{
		ChangeObjectVisitors cov(ChangeObjectVisitors::VISITOR_SCOUTED, id, hero->id);
		gameEvents.sendAndApply(cov);
	}

	if (!isGuarded())
	{
		doHeroVisit(gameEvents, hero);
	}
	else if (configuration.forceCombat)
	{
		doStartBattle(gameEvents, hero);
	}
	else
	{
		auto guardedIndexes = getAvailableRewards(hero, Rewardable::EEventType::EVENT_GUARDED);
		auto guardedReward = configuration.info.at(guardedIndexes.at(0));

		// ask player to confirm attack
		BlockingDialog bd(true, false);
		bd.player = hero->getOwner();
		bd.text = guardedReward.message;
		bd.components = getPopupComponents(hero->getOwner());

		gameEvents.showBlockingDialog(this, &bd);
	}
}

void CRewardableObject::heroLevelUpDone(IGameEventCallback & gameEvents, const CGHeroInstance *hero) const
{
	grantRewardAfterLevelup(gameEvents, configuration.info.at(selectedReward), this, hero);
}

void CRewardableObject::battleFinished(IGameEventCallback & gameEvents, const CGHeroInstance *hero, const BattleResult &result) const
{
	if (result.winner == BattleSide::ATTACKER)
	{
		while(!stacks.empty())
		{
			logMod->warn("Attacker won, but defender still has remaining units in configurable map objects! Make sure that guards configuration is valid!");
			gameEvents.eraseStack(StackLocation(id, stacks.begin()->first));
		}

		doHeroVisit(gameEvents, hero);
	}
}

void CRewardableObject::garrisonDialogClosed(IGameEventCallback & gameEvents, const CGHeroInstance *hero) const
{
	// if visitor received creatures as rewards, but does not have free slots, he will leave some units
	// inside rewardable object, which might get treated as guards later
	while(!stacks.empty())
		gameEvents.eraseStack(StackLocation(id, stacks.begin()->first));
}

void CRewardableObject::doStartBattle(IGameEventCallback & gameEvents, const CGHeroInstance * hero) const
{
	auto layout = BattleLayout::createLayout(*cb, configuration.guardsLayout, hero, this);
	gameEvents.startBattle(hero, this, visitablePos(), hero, nullptr, layout, nullptr);
}

void CRewardableObject::blockingDialogAnswered(IGameEventCallback & gameEvents, const CGHeroInstance * hero, int32_t answer) const
{
	if(isGuarded())
	{
		if (answer)
			doStartBattle(gameEvents, hero);
	}
	else
	{
		onBlockingDialogAnswered(gameEvents, hero, answer);
	}
}

void CRewardableObject::markAsVisited(IGameEventCallback & gameEvents, const CGHeroInstance * hero) const
{
	gameEvents.setObjPropertyValue(id, ObjProperty::REWARD_CLEARED, true);

	ChangeObjectVisitors cov(ChangeObjectVisitors::VISITOR_ADD_HERO, id, hero->id);
	gameEvents.sendAndApply(cov);
}

void CRewardableObject::grantReward(IGameEventCallback & gameEvents, ui32 rewardID, const CGHeroInstance * hero) const
{
	gameEvents.setObjPropertyValue(id, ObjProperty::REWARD_SELECT, rewardID);
	grantRewardBeforeLevelup(gameEvents, configuration.info.at(rewardID), hero);
	
	// hero is not blocked by levelup dialog - grant remainder immediately
	if(!gameEvents.isVisitCoveredByAnotherQuery(this, hero))
	{
		grantRewardAfterLevelup(gameEvents, configuration.info.at(rewardID), this, hero);
	}
}

bool CRewardableObject::wasVisitedBefore(const CGHeroInstance * contextHero) const
{
	switch (configuration.visitMode)
	{
		case Rewardable::VISIT_UNLIMITED:
			return false;
		case Rewardable::VISIT_ONCE:
			return onceVisitableObjectCleared;
		case Rewardable::VISIT_PLAYER:
			return cb->getPlayerState(contextHero->getOwner())->visitedObjects.count(ObjectInstanceID(id)) != 0;
		case Rewardable::VISIT_PLAYER_GLOBAL:
			return cb->getPlayerState(contextHero->getOwner())->visitedObjectsGlobal.count({ID, subID}) != 0;
		case Rewardable::VISIT_BONUS:
			return contextHero->hasBonusFrom(BonusSource::OBJECT_TYPE, BonusSourceID(ID));
		case Rewardable::VISIT_HERO:
			return contextHero->visitedObjects.count(ObjectInstanceID(id));
		case Rewardable::VISIT_LIMITER:
			return configuration.visitLimiter.heroAllowed(contextHero);
		default:
			return false;
	}
}

bool CRewardableObject::wasVisited(PlayerColor player) const
{
	switch (configuration.visitMode)
	{
		case Rewardable::VISIT_UNLIMITED:
		case Rewardable::VISIT_BONUS:
		case Rewardable::VISIT_HERO:
		case Rewardable::VISIT_LIMITER:
			return false;
		case Rewardable::VISIT_ONCE:
		case Rewardable::VISIT_PLAYER:
			return cb->getPlayerState(player)->visitedObjects.count(ObjectInstanceID(id)) != 0;
		case Rewardable::VISIT_PLAYER_GLOBAL:
			return cb->getPlayerState(player)->visitedObjectsGlobal.count({ID, subID}) != 0;
		default:
			return false;
	}
}

bool CRewardableObject::wasScouted(PlayerColor player) const
{
	return vstd::contains(cb->getPlayerTeam(player)->scoutedObjects, ObjectInstanceID(id));
}

bool CRewardableObject::wasVisited(const CGHeroInstance * h) const
{
	switch (configuration.visitMode)
	{
		case Rewardable::VISIT_BONUS:
			return h->hasBonusFrom(BonusSource::OBJECT_TYPE, BonusSourceID(ID));
		case Rewardable::VISIT_HERO:
			return h->visitedObjects.count(ObjectInstanceID(id));
		case Rewardable::VISIT_LIMITER:
			return wasScouted(h->getOwner()) && configuration.visitLimiter.heroAllowed(h);
		default:
			return wasVisited(h->getOwner());
	}
}

namespace
{
/// DMB (K, October 8th, item 21): what an adventure map building does, shown under its pop-up text. The prize comes from the building TYPE's own reward table,
/// sampled with a randomizer of its own (never the roll this building has: that would give the prize away), as ranges; the guards are named with the game's
/// quantity words, and the threat against the selected hero is worded as upstream words it for wandering monsters.
struct Range
{
	int lo = std::numeric_limits<int>::max();
	int hi = 0;

	void add(int value)
	{
		lo = std::min(lo, value);
		hi = std::max(hi, value);
	}

	bool any() const { return hi > 0; }
	std::string text() const { return lo == hi ? std::to_string(hi) : std::to_string(lo) + " to " + std::to_string(hi); }
};

/// how long a hero bonus lasts, as a phrase that follows its value (nothing for a permanent one)
std::string bonusDuration(const Bonus & bonus)
{
	if(bonus.duration & BonusDuration::ONE_DAY)
		return " until the day ends";
	if(bonus.duration & BonusDuration::ONE_WEEK)
		return " until the week ends";
	if(bonus.duration & BonusDuration::N_DAYS)
		return " for " + std::to_string(bonus.turnsRemain) + (bonus.turnsRemain == 1 ? " day" : " days");
	if(bonus.duration & BonusDuration::ONE_BATTLE)
		return " in the next battle";
	return {};
}

struct PrizeSummary
{
	std::map<int, Range> resources;
	std::map<int, Range> costs;
	Range experience;
	Range levels;
	Range mana;
	int manaPercentage = -1; // refills spell points up to this share of the maximum
	size_t mostResourceKinds = 0; // the most resource kinds one roll of the table gives (a windmill gives one kind of several)
	bool dailyBonus = false; // a hero bonus that lasts until the end of the day: the well's mark that the hero used it today
	Range movement;
	std::map<int, Range> primary; // by primary skill
	bool secondary = false;
	std::set<ArtifactID> artifacts;
	std::set<EArtifactClass> artifactClasses;
	std::set<SpellID> spells;
	std::map<CreatureID, Range> creatures;
	std::set<std::string> bonusTexts; // morale and luck by their signed values
	bool otherBonuses = false;
	bool removesObject = false; // taking the reward makes the object vanish (a chest, a pile)
	bool known = false;
};

PrizeSummary summarizeTypeTable(const CRewardableObject & object, IGameInfoCallback * cb)
{
	static std::mutex lock;
	static std::map<std::pair<MapObjectID, MapObjectSubID>, PrizeSummary> cache;
	std::lock_guard guard(lock);

	const auto key = std::make_pair(object.ID, object.subID);
	if(cache.count(key))
		return cache.at(key);

	PrizeSummary summary;
	try
	{
		const auto handler = object.getObjectHandler();
		const auto * constructor = dynamic_cast<const CRewardableConstructor *>(handler.get());
		if(constructor && cb)
		{
			GameRandomizer randomizer(*cb);
			for(int sample = 0; sample < 120; ++sample)
			{
				const auto configuration = constructor->generateConfiguration(cb, randomizer, object.ID, {});
				for(const auto & info : configuration.info)
				{
					if(info.visitType != Rewardable::EEventType::EVENT_FIRST_VISIT)
						continue;
					summary.known = true;
					const auto & reward = info.reward;
					if(reward.removeObject)
						summary.removesObject = true;
					size_t kinds = 0;
					for(int i = 0; i < static_cast<int>(GameResID::COUNT); ++i)
					{
						const int amount = reward.resources[static_cast<size_t>(i)];
						if(amount > 0)
						{
							summary.resources[i].add(amount);
							++kinds;
						}
						else if(amount < 0)
							summary.costs[i].add(-amount);
					}
					summary.mostResourceKinds = std::max(summary.mostResourceKinds, kinds);
					if(reward.manaPercentage >= 0)
						summary.manaPercentage = std::max(summary.manaPercentage, static_cast<int>(reward.manaPercentage));
					if(reward.heroExperience > 0)
						summary.experience.add(reward.heroExperience);
					if(reward.heroLevel > 0)
						summary.levels.add(reward.heroLevel);
					if(reward.manaDiff > 0)
						summary.mana.add(reward.manaDiff);
					if(reward.movePoints > 0)
						summary.movement.add(reward.movePoints);
					for(size_t i = 0; i < reward.primary.size(); ++i)
						if(reward.primary[i] > 0)
							summary.primary[static_cast<int>(i)].add(reward.primary[i]);
					for(const auto & skill : reward.secondary)
						if(skill.second > 0)
							summary.secondary = true;
					for(const auto & artifact : reward.grantedArtifacts)
					{
						summary.artifacts.insert(artifact);
						if(const auto * art = artifact.toArtifact())
							summary.artifactClasses.insert(art->aClass);
					}
					for(const auto & spell : reward.spells)
						summary.spells.insert(spell);
					for(const auto & scroll : reward.grantedScrolls)
						summary.spells.insert(scroll);
					for(const auto & creature : reward.creatures)
						if(creature.getCount() > 0)
							summary.creatures[creature.getId()].add(creature.getCount());
					for(const auto & bonus : reward.heroBonuses)
					{
						if(bonus->duration & BonusDuration::ONE_DAY)
							summary.dailyBonus = true;
						if(bonus->type == BonusType::NONE)
							continue; // a mark with no effect of its own
						const std::string signedValue = std::string(bonus->val >= 0 ? "+" : "") + std::to_string(bonus->val);
						if(bonus->type == BonusType::MORALE || bonus->type == BonusType::LUCK)
							summary.bonusTexts.insert(signedValue + (bonus->type == BonusType::MORALE ? " morale" : " luck") + bonusDuration(*bonus));
						else if(bonus->type == BonusType::SIGHT_RADIUS)
							summary.bonusTexts.insert(signedValue + " sight radius" + bonusDuration(*bonus));
						else
							summary.otherBonuses = true;
					}
					if(!reward.playerBonuses.empty())
						summary.otherBonuses = true;
				}
			}
		}
	}
	catch(const std::exception & e)
	{
		logGlobal->warn("Building info: the reward table of %d/%d could not be sampled: %s", object.ID.getNum(), object.subID.getNum(), e.what());
		summary = PrizeSummary();
	}
	cache[key] = summary;
	return summary;
}

std::string artifactClassWord(EArtifactClass artifactClass)
{
	switch(artifactClass)
	{
		case EArtifactClass::ART_TREASURE: return "treasure";
		case EArtifactClass::ART_MINOR: return "minor";
		case EArtifactClass::ART_MAJOR: return "major";
		case EArtifactClass::ART_RELIC: return "relic";
		default: return "special";
	}
}

std::string joinWords(const std::vector<std::string> & parts, const std::string & last = "and")
{
	std::string result;
	for(size_t i = 0; i < parts.size(); ++i)
	{
		if(i > 0)
			result += (i + 1 == parts.size()) ? " " + last + " " : ", ";
		result += parts[i];
	}
	return result;
}

std::string prizeText(const PrizeSummary & prize, bool alternatives, bool leaveOutSpells)
{
	std::vector<std::string> gives;
	if(prize.resources.size() > 1 && prize.mostResourceKinds == 1)
	{
		// one roll gives one kind of several (a windmill): the amount, then which kinds it may be
		Range amount;
		std::vector<std::string> kinds;
		for(const auto & [res, range] : prize.resources)
		{
			amount.add(range.lo);
			amount.add(range.hi);
			kinds.push_back(boost::to_lower_copy(LIBRARY->generaltexth->restypes[res]));
		}
		gives.push_back(amount.text() + " of one of " + joinWords(kinds, "or"));
	}
	else
	{
		for(const auto & [res, range] : prize.resources)
			gives.push_back(range.text() + " " + boost::to_lower_copy(LIBRARY->generaltexth->restypes[res]));
	}
	for(const auto & [creature, range] : prize.creatures)
		gives.push_back(range.text() + " " + (range.hi == 1 ? creature.toEntity(LIBRARY)->getNameSingularTranslated() : creature.toEntity(LIBRARY)->getNamePluralTranslated()));
	if(prize.experience.any())
		gives.push_back(prize.experience.text() + " experience");
	if(prize.levels.any())
		gives.push_back(prize.levels.hi == 1 ? "a level" : prize.levels.text() + " levels");
	for(const auto & [skill, range] : prize.primary)
		gives.push_back("+" + range.text() + " " + LIBRARY->generaltexth->primarySkillNames[skill]);
	if(prize.secondary)
		gives.push_back("a secondary skill");
	if(prize.mana.any())
		gives.push_back(prize.mana.text() + " spell points");
	if(prize.manaPercentage >= 100)
		gives.push_back("spell points back to the maximum");
	else if(prize.manaPercentage > 0)
		gives.push_back("spell points up to " + std::to_string(prize.manaPercentage) + "% of the maximum");
	if(prize.movement.any())
		gives.push_back("movement");
	if(!prize.artifacts.empty())
	{
		if(prize.artifacts.size() <= 2)
		{
			for(const auto & artifact : prize.artifacts)
				gives.push_back(artifact.toEntity(LIBRARY)->getNameTranslated());
		}
		else
		{
			std::vector<std::string> classes;
			for(const auto & artifactClass : prize.artifactClasses)
				classes.push_back(artifactClassWord(artifactClass));
			gives.push_back("an artifact (" + joinWords(classes, "or") + " class)");
		}
	}
	if(!prize.spells.empty() && !leaveOutSpells)
		gives.push_back(prize.spells.size() == 1 ? "the spell " + prize.spells.begin()->toEntity(LIBRARY)->getNameTranslated() : "a spell");
	for(const auto & text : prize.bonusTexts)
		gives.push_back(text);
	if(prize.otherBonuses)
		gives.push_back("a bonus");
	return joinWords(gives, alternatives ? "or" : "and");
}

/// a limiter's conditions in words, added to `out`; false when it holds something that is not worded here (the caller then keeps a general sentence)
bool limiterWords(const Rewardable::Limiter & limiter, std::vector<std::string> & out)
{
	if(!limiter.allOf.empty() || !limiter.anyOf.empty() || !limiter.noneOf.empty() || !limiter.heroes.empty() || !limiter.heroClasses.empty() || !limiter.players.empty()
		|| limiter.dayOfWeek > 0 || limiter.movePoints > 0 || limiter.movePercentage > 0 || !limiter.secondary.empty()
		|| !limiter.scrolls.empty() || !limiter.availableSlots.empty() || limiter.commanderAlive || limiter.hasExtraCreatures || !limiter.canReceiveCreatures.empty())
		return false;
	for(const auto value : limiter.primary)
		if(value > 0)
			return false;
	if(limiter.heroLevel > 0)
		out.push_back("hero level " + std::to_string(limiter.heroLevel) + " or higher");
	if(limiter.heroExperience > 0)
		out.push_back(std::to_string(limiter.heroExperience) + " experience");
	if(limiter.daysPassed > 0)
		out.push_back("day " + std::to_string(limiter.daysPassed) + " or later");
	if(limiter.manaPoints > 0)
		out.push_back(std::to_string(limiter.manaPoints) + " spell points");
	if(limiter.manaPercentage > 0)
		out.push_back(std::to_string(limiter.manaPercentage) + "% of the maximum spell points");
	if(limiter.canLearnSkills)
		out.push_back("a free skill slot");
	for(int i = 0; i < static_cast<int>(GameResID::COUNT); ++i)
		if(limiter.resources[static_cast<size_t>(i)] > 0)
			out.push_back(std::to_string(limiter.resources[static_cast<size_t>(i)]) + " " + boost::to_lower_copy(LIBRARY->generaltexth->restypes[i]));
	for(const auto & artifact : limiter.artifacts)
		out.push_back("the " + artifact.toEntity(LIBRARY)->getNameTranslated());
	for(const auto & creature : limiter.creatures)
		out.push_back(std::to_string(creature.getCount()) + " " + (creature.getCount() == 1 ? creature.getCreature()->getNameSingularTranslated() : creature.getCreature()->getNamePluralTranslated()));
	for(const auto & spell : limiter.spells)
		out.push_back("the spell " + spell.toEntity(LIBRARY)->getNameTranslated());
	if(!limiter.canLearnSpells.empty())
	{
		// a hero learns spells up to the 3rd level with no Wisdom (the game's base limit); only a higher spell needs it
		bool needsWisdom = false;
		for(const auto & spell : limiter.canLearnSpells)
			if(spell.toEntity(LIBRARY)->getLevel() > 3)
				needsWisdom = true;
		out.push_back(needsWisdom ? "a spell book and enough Wisdom to learn the spell" : "a spell book");
	}
	return true;
}

std::string costText(const PrizeSummary & prize)
{
	std::vector<std::string> costs;
	for(const auto & [res, range] : prize.costs)
		costs.push_back(range.text() + " " + boost::to_lower_copy(LIBRARY->generaltexth->restypes[res]));
	return joinWords(costs);
}

std::string quantityWord(int count)
{
	const auto quantityId = CCreature::getQuantityID(count);
	if(settings["gameTweaks"]["numericCreaturesQuantities"].Bool())
		return CCreature::getQuantityRangeStringForId(quantityId);
	return LIBRARY->generaltexth->arraytxt[172 + 3 * static_cast<int>(quantityId)];
}

/// upstream's wording of a wandering monster's threat, for any guard army
std::string threatWord(uint64_t guardStrength, uint64_t heroStrength)
{
	const double ratio = heroStrength == 0 ? 1000.0 : static_cast<double>(guardStrength) / static_cast<double>(heroStrength);
	static const double limits[] = {0.1, 0.25, 0.6, 0.9, 1.1, 1.3, 1.8, 2.5, 4.0, 8.0, 20.0};
	int choice = 11;
	for(int i = 0; i < 11; ++i)
		if(ratio < limits[i])
		{
			choice = i;
			break;
		}
	return LIBRARY->generaltexth->translate("vcmi.adventureMap.monsterThreat.levels." + std::to_string(choice));
}
}

std::string CRewardableObject::getBuildingInfoText(PlayerColor player, const CGHeroInstance * hero) const
{
	if(!settings["general"]["buildingInfo"].Bool() || configuration.info.empty())
		return {};

	const bool guarded = isGuarded();
	// the engine marks a building as scouted for the whole team when a hero arrives (before the fight); until then the guards and the prize stay unknown
	const bool hidden = guarded && player.isValidPlayer() && !wasScouted(player);
	const auto prize = summarizeTypeTable(*this, cb);
	// when the visited state is cleared on a period, the use is per week (or per N days), not for good
	const int period = configuration.resetParameters.period;
	const bool renews = period > 0 && configuration.resetParameters.visitors;
	const std::string every = period == 7 ? "each week" : period == 1 ? "each day" : "every " + std::to_string(period) + " days";
	bool learnsSpell = false; // the shrines: the pop-up already names the spell and shows its picture
	std::string kind;
	switch(configuration.visitMode)
	{
		case Rewardable::VISIT_ONCE: kind = renews ? "Only the first hero to visit " + every + " gets it." : "Only the first visitor gets it."; break;
		case Rewardable::VISIT_PLAYER:
		case Rewardable::VISIT_PLAYER_GLOBAL: kind = renews ? "Each player can use it once " + every + "." : "Each player can use it once."; break;
		case Rewardable::VISIT_HERO: kind = renews ? "Each hero can use it once " + every + "." : "Each hero can use it once."; break;
		case Rewardable::VISIT_BONUS: kind = prize.dailyBonus ? "Each hero can use it once a day." : "A hero that already has its bonus gets nothing more."; break;
		case Rewardable::VISIT_LIMITER:
		{
			std::vector<std::string> needs;
			bool worded = true;
			for(const auto & info : configuration.info)
			{
				if(info.visitType != Rewardable::EEventType::EVENT_FIRST_VISIT)
					continue;
				learnsSpell = learnsSpell || !info.limiter.canLearnSpells.empty();
				std::vector<std::string> mine;
				worded = worded && limiterWords(info.limiter, mine);
				for(const auto & need : mine)
					if(!vstd::contains(needs, need))
						needs.push_back(need);
			}
			if(worded && !needs.empty())
			{
				kind = "A hero needs " + joinWords(needs) + ".";
				if(!configuration.visitLimiter.spells.empty())
					kind += " A hero that already knows the spell gets nothing.";
			}
			else
				kind = "Only heroes that meet its condition can use it.";
			break;
		}
		default:
			kind = "It can be used again and again.";
			if(configuration.resetParameters.period > 0)
				kind = "It renews every " + std::to_string(configuration.resetParameters.period) + (configuration.resetParameters.period == 1 ? " day." : " days.");
			break;
	}
	if(prize.removesObject)
		kind = "It disappears once taken.";
	std::string text = "\n\n" + std::string(guarded ? "Guarded: win the fight to claim it. " : "") + kind;

	if(hidden)
		return text + "\nGuards and prize: unknown until a hero of yours or an ally has visited.";

	if(guarded)
	{
		std::map<CreatureID, int> guards;
		for(const auto & slot : stacks)
			guards[slot.second->getCreatureID()] += slot.second->getCount();
		std::vector<std::string> names;
		for(const auto & [creature, count] : guards)
			names.push_back(quantityWord(count) + " " + creature.toEntity(LIBRARY)->getNamePluralTranslated());
		if(!names.empty())
			text += "\nGuards: " + joinWords(names);
	}

	if(prize.known)
	{
		// several rewards that the visitor picks from, or one of which is rolled, are alternatives; otherwise all of them are given
		size_t firstVisitRewards = 0;
		for(const auto & info : configuration.info)
			if(info.visitType == Rewardable::EEventType::EVENT_FIRST_VISIT)
				++firstVisitRewards;
		const bool alternatives = firstVisitRewards > 1 && (configuration.selectMode == Rewardable::SELECT_PLAYER || configuration.selectMode == Rewardable::SELECT_RANDOM);
		const std::string gives = prizeText(prize, alternatives, learnsSpell);
		if(!gives.empty())
			text += std::string("\n") + (guarded ? "Prize: " : "Gives: ") + (alternatives ? (configuration.selectMode == Rewardable::SELECT_PLAYER ? "your pick of " : "one of ") : "") + gives;
		if(configuration.selectMode == Rewardable::SELECT_FIRST && firstVisitRewards > 1)
		{
			// a reward that needs a later day is checked first (a water wheel gives more after the first week)
			for(const auto & info : configuration.info)
				if(info.visitType == Rewardable::EEventType::EVENT_FIRST_VISIT && info.limiter.daysPassed > 0)
				{
					text += "\nThe larger prize is given from day " + std::to_string(info.limiter.daysPassed) + " on.";
					break;
				}
		}
		const std::string costs = costText(prize);
		if(!costs.empty())
			text += "\nCosts: " + costs;
	}

	if(guarded && hero)
		text += "\nThreat: " + threatWord(getArmyStrength(), hero->getTotalStrength());

	return text;
}

std::string CRewardableObject::getDisplayTextImpl(PlayerColor player, const CGHeroInstance * hero, bool includeDescription) const
{
	std::string result = getObjectName();

	if (includeDescription)
	{
		if (!getDescriptionMessage(player, hero).empty())
			result += "\n" + getDescriptionMessage(player, hero);
	}
	else
	{
		// scouted version is included unconditionally into hover text (e.g. Shrines - "learn X spell")
		if (!getScoutedDescriptionMessage(hero).empty())
			result += "\n" + getDescriptionMessage(player, hero);
	}

	if (hero)
	{
		if(configuration.visitMode != Rewardable::VISIT_UNLIMITED)
		{
			if (wasVisited(hero))
				result += "\n" + configuration.visitedTooltip.toString();
			else
				result += "\n" + configuration.notVisitedTooltip.toString();
		}
	}
	else
	{
		if(configuration.visitMode == Rewardable::VISIT_PLAYER || configuration.visitMode == Rewardable::VISIT_ONCE || configuration.visitMode == Rewardable::VISIT_PLAYER_GLOBAL)
		{
			if (wasVisited(player))
				result += "\n" + configuration.visitedTooltip.toString();
			else
				result += "\n" + configuration.notVisitedTooltip.toString();
		}
	}
	// DMB: what the building does, its guards, its prize and the threat (the pop-up only)
	if(includeDescription)
	{
		// an odd mod building must never cost the player the pop-up (or the game): the extra text is left out
		try
		{
			result += getBuildingInfoText(player, hero);
		}
		catch(const std::exception & e)
		{
			logGlobal->warn("Building info: left out for object %d/%d: %s", ID.getNum(), subID.getNum(), e.what());
		}
	}
	return result;
}

std::string CRewardableObject::getHoverText(PlayerColor player) const
{
	return getDisplayTextImpl(player, nullptr, false);
}

std::string CRewardableObject::getHoverText(const CGHeroInstance * hero) const
{
	return getDisplayTextImpl(hero->getOwner(), hero, false);
}

std::string CRewardableObject::getPopupText(PlayerColor player) const
{
	return getDisplayTextImpl(player, nullptr, true);
}

std::string CRewardableObject::getPopupText(const CGHeroInstance * hero) const
{
	return getDisplayTextImpl(hero->getOwner(), hero, true);
}

std::string CRewardableObject::getScoutedDescriptionMessage(const CGHeroInstance * hero) const
{
	if (configuration.info.empty())
		return {};

	auto rewardIndices = getAvailableRewards(hero, Rewardable::EEventType::EVENT_FIRST_VISIT);
	if (rewardIndices.empty() || !configuration.info[0].description.empty())
		return configuration.info[0].description.toString();

	if (!configuration.info[rewardIndices.front()].description.empty())
		return configuration.info[rewardIndices.front()].description.toString();

	return {};
}

std::string CRewardableObject::getGenericDescriptionMessage() const
{
	return configuration.description.toString();
}

std::string CRewardableObject::getDescriptionMessage(PlayerColor player, const CGHeroInstance * hero) const
{
	if (!wasScouted(player) || configuration.info.empty())
		return getGenericDescriptionMessage();

	auto scoutedMessage = getScoutedDescriptionMessage(hero);
	if (!scoutedMessage.empty())
		return scoutedMessage;

	return getGenericDescriptionMessage();
}

std::vector<Component> CRewardableObject::getPopupComponentsImpl(PlayerColor player, const CGHeroInstance * hero) const
{
	if (!wasScouted(player))
		return {};

	if (isGuarded())
	{
		if (!cb->getSettings().getBoolean(EGameSettings::BANKS_SHOW_GUARDS_COMPOSITION))
			return {};

		std::map<CreatureID, int> guardsAmounts;
		std::vector<Component> result;

		for (auto const & slot : Slots())
			if (slot.second)
				guardsAmounts[slot.second->getCreatureID()] += slot.second->getCount();

		for (auto const & guard : guardsAmounts)
		{
			Component comp(ComponentType::CREATURE, guard.first, guard.second);
			result.push_back(comp);
		}
		return result;
	}
	else
	{
		if (!configuration.showScoutedPreview)
			return {};

		auto rewardIndices = getAvailableRewards(hero, Rewardable::EEventType::EVENT_FIRST_VISIT);
		if (rewardIndices.empty() && !configuration.info.empty())
		{
			// Object has valid config, but current hero has no rewards that he can receive.
			// Usually this happens if hero has already visited this object -> show reward using context without any hero
			// since reward may be context-sensitive - e.g. Witch Hut that gives 1 skill, but always at basic level
			return loadComponents(nullptr, {0});
		}

		if (rewardIndices.empty())
			return {};

		return loadComponents(hero, rewardIndices);
	}
}

std::vector<Component> CRewardableObject::getPopupComponents(PlayerColor player) const
{
	return getPopupComponentsImpl(player, nullptr);
}

std::vector<Component> CRewardableObject::getPopupComponents(const CGHeroInstance * hero) const
{
	return getPopupComponentsImpl(hero->getOwner(), hero);
}

void CRewardableObject::setPropertyDer(ObjProperty what, ObjPropertyID identifier)
{
	switch (what)
	{
		case ObjProperty::REWARD_SELECT:
			selectedReward = identifier.getNum();
			break;
		case ObjProperty::REWARD_CLEARED:
			onceVisitableObjectCleared = identifier.getNum();
			break;
	}
}

void CRewardableObject::newTurn(IGameEventCallback & gameEvents, IGameRandomizer & gameRandomizer) const
{
	if (configuration.resetParameters.period != 0 && cb->getDate(Date::DAY) > 1 && ((cb->getDate(Date::DAY)-1) % configuration.resetParameters.period) == 0)
	{
		if (configuration.resetParameters.rewards)
		{
			auto handler = std::dynamic_pointer_cast<const CRewardableConstructor>(getObjectHandler());
			auto newConfiguration = handler->generateConfiguration(cb, gameRandomizer, ID, configuration.variables.preset);
			gameEvents.setRewardableObjectConfiguration(id, newConfiguration);
		}
		if (configuration.resetParameters.visitors)
		{
			gameEvents.setObjPropertyValue(id, ObjProperty::REWARD_CLEARED, false);
			ChangeObjectVisitors cov(ChangeObjectVisitors::VISITOR_CLEAR, id);
			gameEvents.sendAndApply(cov);
		}
	}
}

void CRewardableObject::initObj(IGameRandomizer & gameRandomizer)
{
	getObjectHandler()->configureObject(this, gameRandomizer);
}

CRewardableObject::CRewardableObject(IGameInfoCallback *cb)
	:CArmedInstance(cb)
{}

void CRewardableObject::serializeJsonOptions(JsonSerializeFormat & handler)
{
	CArmedInstance::serializeJsonOptions(handler);
	handler.serializeStruct("rewardable", static_cast<Rewardable::Interface&>(*this));
}

void CRewardableObject::initializeGuards()
{
	clearSlots();

	// Workaround for default creature banks strings that has placeholder for object name
	// TODO: find better location for this code
	for (auto & visitInfo : configuration.info)
		visitInfo.message.replaceRawString(getObjectName());

	for (auto const & visitInfo : configuration.info)
	{
		for (auto const & guard : visitInfo.reward.guards)
		{
			auto slotID = getFreeSlot();
			if (!slotID.validSlot())
				return;

			putStack(slotID, std::make_unique<CStackInstance>(cb, guard.getId(), guard.getCount()));
		}
	}
}

bool CRewardableObject::isCoastVisitable() const
{
	return configuration.coastVisitable;
}

VCMI_LIB_NAMESPACE_END
