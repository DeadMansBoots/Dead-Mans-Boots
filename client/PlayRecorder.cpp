/*
 * PlayRecorder.cpp, part of VCMI engine (Dead Man's Boots)
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "PlayRecorder.h"

#include "CPlayerInterface.h"
#include "GameInstance.h"
#include "CServerHandler.h"
#include "Client.h"

#include "../lib/CConfigHandler.h"
#include "../lib/CPlayerState.h"
#include "../lib/GameLibrary.h"
#include "../lib/VCMIDirs.h"
#include "../lib/battle/BattleAction.h"
#include "../lib/battle/BattleInfo.h"
#include "../lib/callback/CCallback.h"
#include "../lib/entities/artifact/CArtifact.h"
#include "../lib/entities/artifact/CArtifactInstance.h"
#include "../lib/entities/building/CBuilding.h"
#include "../lib/entities/faction/CTown.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/gameState/GameStatistics.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include "../lib/mapObjects/CGTownInstance.h"
#include "../lib/mapObjects/army/CStackInstance.h"
#include "../lib/networkPacks/NetPackVisitor.h"
#include "../lib/networkPacks/PacksForClient.h"
#include "../lib/networkPacks/PacksForClientBattle.h"
#include "../lib/networkPacks/PacksForServer.h"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

namespace
{
constexpr uint64_t FILE_LIMIT_BYTES = 100ull * 1024 * 1024; // one game's file stops here
constexpr uint64_t FOLDER_LIMIT_BYTES = 1024ull * 1024 * 1024; // the whole folder
constexpr size_t FILE_LIMIT_COUNT = 40;

std::string escape(const std::string & text)
{
	std::string out;
	out.reserve(text.size() + 8);
	for(unsigned char c : text)
	{
		switch(c)
		{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if(c < 0x20)
				{
					char buf[8];
					snprintf(buf, sizeof(buf), "\\u%04x", c);
					out += buf;
				}
				else
					out += static_cast<char>(c);
		}
	}
	return out;
}

/// Builds the key/value pairs of a JSON object, without the braces
class Line
{
	std::string text;

	void key(const std::string & k)
	{
		if(!text.empty())
			text += ",";
		text += "\"" + escape(k) + "\":";
	}

public:
	Line & str(const std::string & k, const std::string & v)
	{
		key(k);
		text += "\"" + escape(v) + "\"";
		return *this;
	}
	Line & num(const std::string & k, int64_t v)
	{
		key(k);
		text += std::to_string(v);
		return *this;
	}
	Line & flag(const std::string & k, bool v)
	{
		key(k);
		text += v ? "true" : "false";
		return *this;
	}
	/// v is already JSON (an array or an object)
	Line & raw(const std::string & k, const std::string & v)
	{
		key(k);
		text += v;
		return *this;
	}
	const std::string & get() const { return text; }
};

std::string array(const std::vector<std::string> & items)
{
	std::string out = "[";
	for(size_t i = 0; i < items.size(); ++i)
		out += (i ? "," : "") + items[i];
	return out + "]";
}

std::string jsonString(const std::string & text)
{
	return "\"" + escape(text) + "\"";
}

std::string object(const Line & line)
{
	return "{" + line.get() + "}";
}

const CGObjectInstance * objectOf(ObjectInstanceID id)
{
	if(!GAME || !GAME->interface() || !GAME->interface()->cb || !id.hasValue())
		return nullptr;
	return GAME->interface()->cb->getObj(id, false);
}

std::string objectName(ObjectInstanceID id)
{
	if(!id.hasValue())
		return "none";
	std::string name = "#" + std::to_string(id.getNum());
	if(const auto * obj = objectOf(id))
	{
		if(const auto * hero = dynamic_cast<const CGHeroInstance *>(obj))
			name = hero->getNameTranslated() + name;
		else if(const auto * town = dynamic_cast<const CGTownInstance *>(obj))
			name = town->getNameTranslated() + name;
		else
			name = obj->getObjectName() + name;
	}
	return name;
}

std::string creatureName(CreatureID id)
{
	if(!id.hasValue())
		return "none";
	const auto * creature = id.toCreature();
	return creature ? creature->getNamePluralTranslated() : "creature" + std::to_string(id.getNum());
}

std::string artifactName(ArtifactID id)
{
	if(!id.hasValue())
		return "none";
	const auto * art = id.toArtifact();
	return art ? art->getNameTranslated() : "artifact" + std::to_string(id.getNum());
}

std::string pos(const int3 & p)
{
	return std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z);
}

/// What a slot of an army holds, e.g. "slot2(12 Hounds)"
std::string slotText(ObjectInstanceID army, SlotID slot)
{
	std::string text = "slot" + std::to_string(slot.getNum());
	const auto * armed = dynamic_cast<const CArmedInstance *>(objectOf(army));
	if(armed && armed->hasStackAtSlot(slot))
	{
		const auto & stack = armed->getStack(slot);
		text += "(" + std::to_string(stack.getCount()) + " " + creatureName(stack.getCreatureID()) + ")";
	}
	else
		text += "(empty)";
	return text;
}

std::string armyList(const CCreatureSet & army)
{
	std::vector<std::string> items;
	for(const auto & slot : army.Slots())
		items.push_back(object(Line().num("slot", slot.first.getNum()).str("creature", creatureName(slot.second->getCreatureID())).num("count", slot.second->getCount())));
	return array(items);
}

std::string artifactName(const CArtifactInstance * art)
{
	return art && art->getType() ? art->getType()->getNameTranslated() : "?";
}

std::string artifactLocation(const ArtifactLocation & al)
{
	std::string text = objectName(al.artHolder) + " slot " + std::to_string(al.slot.getNum());
	if(al.creature)
		text += " (creature slot " + std::to_string(al.creature->getNum()) + ")";
	else if(const auto * holder = dynamic_cast<const CArtifactSet *>(objectOf(al.artHolder)))
		if(const auto * art = holder->getArt(al.slot, false))
			text += " holding " + artifactName(art);
	return text;
}

/// A request the player's client sends
class RequestDescriber : public ICPackVisitor
{
public:
	Line line;
	bool described = false;

	void set(const std::string & type)
	{
		line.str("type", type);
		described = true;
	}

	void visitEndTurn(EndTurn &) override { set("EndTurn"); }
	void visitGamePause(GamePause &) override { set("GamePause"); }
	void visitMoveHero(MoveHero & pack) override
	{
		set("MoveHero");
		line.str("hero", objectName(pack.hid)).flag("transit", pack.transit);
		if(!pack.path.empty())
			line.str("from", pos(pack.path.back())).str("to", pos(pack.path.front())).num("steps", static_cast<int64_t>(pack.path.size()));
	}
	void visitDismissHero(DismissHero & pack) override { set("DismissHero"); line.str("hero", objectName(pack.hid)); }
	void visitCastleTeleportHero(CastleTeleportHero & pack) override { set("CastleTeleportHero"); line.str("hero", objectName(pack.hid)).str("town", objectName(pack.dest)); }
	void visitArrangeStacks(ArrangeStacks & pack) override
	{
		static const char * names[] = { "?", "swap", "merge", "split" };
		set("ArrangeStacks");
		line.str("what", pack.what < 4 ? names[pack.what] : "?")
			.str("from", objectName(pack.id1) + " " + slotText(pack.id1, pack.p1))
			.str("to", objectName(pack.id2) + " " + slotText(pack.id2, pack.p2))
			.num("val", pack.val);
	}
	void visitBulkMoveArmy(BulkMoveArmy & pack) override
	{
		set("BulkMoveArmy");
		line.str("from", objectName(pack.srcArmy)).num("slot", pack.srcSlot.getNum()).str("to", objectName(pack.destArmy));
	}
	void visitBulkSplitStack(BulkSplitStack & pack) override
	{
		set("BulkSplitStack");
		line.str("army", objectName(pack.srcOwner) + " " + slotText(pack.srcOwner, pack.src)).num("amount", pack.amount);
	}
	void visitBulkMergeStacks(BulkMergeStacks & pack) override
	{
		set("BulkMergeStacks");
		line.str("army", objectName(pack.srcOwner) + " " + slotText(pack.srcOwner, pack.src));
	}
	void visitBulkSplitAndRebalanceStack(BulkSplitAndRebalanceStack & pack) override
	{
		set("BulkSplitAndRebalanceStack");
		line.str("army", objectName(pack.srcOwner) + " " + slotText(pack.srcOwner, pack.src));
	}
	void visitDisbandCreature(DisbandCreature & pack) override
	{
		set("DisbandCreature");
		line.str("army", objectName(pack.id) + " " + slotText(pack.id, pack.pos));
	}
	void visitBuildStructure(BuildStructure & pack) override { set("BuildStructure"); line.str("town", objectName(pack.tid)).num("building", pack.bid.getNum()); }
	void visitRazeStructure(RazeStructure & pack) override { set("RazeStructure"); line.str("town", objectName(pack.tid)).num("building", pack.bid.getNum()); }
	void visitVisitTownBuilding(VisitTownBuilding & pack) override { set("VisitTownBuilding"); line.str("town", objectName(pack.tid)).num("building", pack.bid.getNum()); }
	void visitSpellResearch(SpellResearch & pack) override { set("SpellResearch"); line.str("town", objectName(pack.tid)).flag("accepted", pack.accepted); }
	void visitRecruitCreatures(RecruitCreatures & pack) override
	{
		set("RecruitCreatures");
		line.str("from", objectName(pack.tid)).str("to", objectName(pack.dst)).str("creature", creatureName(pack.crid)).num("amount", pack.amount).num("level", pack.level);
	}
	void visitUpgradeCreature(UpgradeCreature & pack) override
	{
		set("UpgradeCreature");
		line.str("army", objectName(pack.id) + " " + slotText(pack.id, pack.pos)).str("to", creatureName(pack.cid));
	}
	void visitGarrisonHeroSwap(GarrisonHeroSwap & pack) override { set("GarrisonHeroSwap"); line.str("town", objectName(pack.tid)); }
	void visitExchangeArtifacts(ExchangeArtifacts & pack) override
	{
		set("ExchangeArtifacts");
		line.str("from", artifactLocation(pack.src)).str("to", artifactLocation(pack.dst));
	}
	void visitBulkExchangeArtifacts(BulkExchangeArtifacts & pack) override
	{
		set("BulkExchangeArtifacts");
		line.str("from", objectName(pack.srcHero)).str("to", objectName(pack.dstHero)).flag("swap", pack.swap).flag("equipped", pack.equipped).flag("backpack", pack.backpack);
	}
	void visitManageBackpackArtifacts(ManageBackpackArtifacts & pack) override
	{
		static const char * names[] = { "scroll left", "scroll right", "sort by slot", "sort by class", "sort by cost" };
		set("ManageBackpackArtifacts");
		const int cmd = static_cast<int>(pack.cmd);
		line.str("hero", objectName(pack.artHolder)).str("command", cmd >= 0 && cmd < 5 ? names[cmd] : "?");
	}
	void visitManageEquippedArtifacts(ManageEquippedArtifacts & pack) override
	{
		set("ManageEquippedArtifacts");
		line.str("hero", objectName(pack.artHolder)).num("costume", pack.costumeIdx).flag("saveCostume", pack.saveCostume);
	}
	void visitAssembleArtifacts(AssembleArtifacts & pack) override
	{
		set("AssembleArtifacts");
		line.str("hero", objectName(pack.heroID)).num("slot", pack.artifactSlot.getNum());
	}
	void visitEraseArtifactByClient(EraseArtifactByClient & pack) override { set("EraseArtifactByClient"); line.str("artifact", artifactLocation(pack.al)); }
	void visitBuyArtifact(BuyArtifact & pack) override { set("BuyArtifact"); line.str("hero", objectName(pack.hid)).str("artifact", artifactName(pack.aid)); }
	void visitTradeOnMarketplace(TradeOnMarketplace & pack) override
	{
		set("TradeOnMarketplace");
		line.str("market", objectName(pack.marketId)).str("hero", objectName(pack.heroId)).num("mode", static_cast<int>(pack.mode)).num("sellItems", static_cast<int64_t>(pack.r1.size())).num("buyItems", static_cast<int64_t>(pack.r2.size()));
		std::vector<std::string> amounts;
		for(auto v : pack.val)
			amounts.push_back(std::to_string(v));
		line.raw("amounts", array(amounts));
	}
	void visitSetFormation(SetFormation & pack) override { set("SetFormation"); line.str("hero", objectName(pack.hid)); }
	void visitSetTactics(SetTactics & pack) override { set("SetTactics"); line.str("hero", objectName(pack.hid)).flag("enabled", pack.enabled); }
	void visitSetTownName(SetTownName & pack) override { set("SetTownName"); line.str("town", objectName(pack.tid)).str("name", pack.name); }
	void visitHireHero(HireHero & pack) override { set("HireHero"); line.str("tavern", objectName(pack.tid)).num("hero", pack.hid.getNum()); }
	void visitBuildBoat(BuildBoat &) override { set("BuildBoat"); }
	void visitQueryReply(QueryReply & pack) override
	{
		set("QueryReply");
		line.num("query", pack.qid.getNum());
		if(pack.reply)
			line.num("reply", *pack.reply);
	}
	void visitMakeAction(MakeAction & pack) override
	{
		set("MakeAction");
		line.num("battle", pack.battleID.getNum()).num("action", static_cast<int>(pack.ba.actionType)).num("stack", static_cast<int64_t>(pack.ba.stackNumber)).num("spell", pack.ba.spell.getNum());
		std::vector<std::string> hexes;
		for(const auto & target : pack.ba.target)
			hexes.push_back(object(Line().num("unit", target.unitValue).num("hex", target.hexValue.toInt())));
		line.raw("targets", array(hexes));
	}
	void visitDigWithHero(DigWithHero & pack) override { set("DigWithHero"); line.str("hero", objectName(pack.id)); }
	void visitCastAdvSpell(CastAdvSpell & pack) override
	{
		set("CastAdvSpell");
		line.str("hero", objectName(pack.hid)).num("spell", pack.sid.getNum()).str("tile", pos(pack.pos));
	}
	void visitSaveGame(SaveGame & pack) override { set("SaveGame"); line.str("file", pack.fname); }
	void visitPlayerMessage(PlayerMessage & pack) override { set("PlayerMessage"); line.str("text", pack.text); }
};

const char * modeText(ChangeValueMode mode)
{
	return mode == ChangeValueMode::ABSOLUTE ? "absolute" : "relative";
}

/// What the server tells the client (called before the client applies it, so the game state still holds the old values)
class ClientDescriber : public ICPackVisitor
{
public:
	Line line;
	bool described = false;

	void set(const std::string & type)
	{
		line.str("type", type);
		described = true;
	}

	void visitPlayerStartsTurn(PlayerStartsTurn & pack) override { set("PlayerStartsTurn"); line.str("player", pack.player.toString()); }
	void visitPlayerEndsTurn(PlayerEndsTurn & pack) override { set("PlayerEndsTurn"); line.str("player", pack.player.toString()); }
	void visitPlayerEndsGame(PlayerEndsGame & pack) override { set("PlayerEndsGame"); line.str("player", pack.player.toString()).flag("silent", pack.silentEnd); }
	void visitNewTurn(NewTurn & pack) override
	{
		set("NewTurn");
		line.num("day", pack.day).num("specialWeek", static_cast<int>(pack.specialWeek));
		std::vector<std::string> income;
		for(const auto & entry : pack.playerIncome)
			income.push_back(object(Line().str("player", entry.first.toString()).num("gold", entry.second[EGameResID::GOLD])));
		line.raw("income", array(income));
	}
	void visitTryMoveHero(TryMoveHero & pack) override
	{
		static const char * results[] = { "failed", "success", "teleport", "embark", "disembark", "blocked_visit" };
		set("TryMoveHero");
		const int result = static_cast<int>(pack.result);
		line.str("hero", objectName(pack.id)).str("result", result >= 0 && result < 6 ? results[result] : "?").str("start", pos(pack.start)).str("end", pos(pack.end)).num("movePoints", pack.movePoints);
	}
	void visitSetResources(SetResources & pack) override
	{
		set("SetResources");
		std::vector<std::string> values;
		for(int i = 0; i < GameConstants::RESOURCE_QUANTITY; ++i)
			values.push_back(std::to_string(pack.res[i]));
		line.str("player", pack.player.toString()).str("mode", modeText(pack.mode)).raw("res", array(values));
	}
	void visitSetPrimarySkill(SetPrimarySkill & pack) override
	{
		set("SetPrimarySkill");
		line.str("hero", objectName(pack.id)).num("skill", static_cast<int>(pack.which)).num("value", pack.val).str("mode", modeText(pack.mode));
	}
	void visitSetHeroExperience(SetHeroExperience & pack) override
	{
		set("SetHeroExperience");
		line.str("hero", objectName(pack.id)).num("value", pack.val).str("mode", modeText(pack.mode));
	}
	void visitHeroLevelUp(HeroLevelUp & pack) override
	{
		set("HeroLevelUp");
		std::vector<std::string> skills;
		for(const auto & skill : pack.skills)
			skills.push_back(std::to_string(skill.getNum()));
		line.str("hero", objectName(pack.heroId)).num("primary", static_cast<int>(pack.primskill)).raw("secondaryChoices", array(skills));
	}
	void visitSetSecSkill(SetSecSkill & pack) override
	{
		set("SetSecSkill");
		line.str("hero", objectName(pack.id)).num("skill", pack.which.getNum()).num("value", pack.val);
	}
	void visitSetMana(SetMana & pack) override { set("SetMana"); line.str("hero", objectName(pack.hid)).num("value", pack.val).str("mode", modeText(pack.mode)); }
	void visitRemoveObject(RemoveObject & pack) override { set("RemoveObject"); line.str("object", objectName(pack.objectID)).str("by", pack.initiator.toString()); }
	void visitHeroVisit(HeroVisit & pack) override
	{
		set("HeroVisit");
		line.str("hero", objectName(pack.heroId)).str("object", objectName(pack.objId)).flag("starting", pack.starting);
	}
	void visitHeroVisitCastle(HeroVisitCastle & pack) override { set("HeroVisitCastle"); line.str("town", objectName(pack.tid)).str("hero", objectName(pack.hid)).flag("start", pack.startVisit); }
	void visitNewStructures(NewStructures & pack) override
	{
		set("NewStructures");
		std::vector<std::string> ids;
		for(const auto & id : pack.bid)
			ids.push_back(std::to_string(id.getNum()));
		line.str("town", objectName(pack.tid)).raw("buildings", array(ids));
	}
	void visitRazeStructures(RazeStructures & pack) override
	{
		set("RazeStructures");
		std::vector<std::string> ids;
		for(const auto & id : pack.bid)
			ids.push_back(std::to_string(id.getNum()));
		line.str("town", objectName(pack.tid)).raw("buildings", array(ids));
	}
	void visitChangeStackCount(ChangeStackCount & pack) override
	{
		set("ChangeStackCount");
		line.str("army", objectName(pack.army) + " " + slotText(pack.army, pack.slot)).num("count", pack.count).str("mode", modeText(pack.mode));
	}
	void visitSetStackType(SetStackType & pack) override { set("SetStackType"); line.str("army", objectName(pack.army) + " " + slotText(pack.army, pack.slot)).str("type", creatureName(pack.type)); }
	void visitEraseStack(EraseStack & pack) override { set("EraseStack"); line.str("army", objectName(pack.army) + " " + slotText(pack.army, pack.slot)); }
	void visitSwapStacks(SwapStacks & pack) override
	{
		set("SwapStacks");
		line.str("from", objectName(pack.srcArmy) + " " + slotText(pack.srcArmy, pack.srcSlot)).str("to", objectName(pack.dstArmy) + " " + slotText(pack.dstArmy, pack.dstSlot));
	}
	void visitInsertNewStack(InsertNewStack & pack) override { set("InsertNewStack"); line.str("army", objectName(pack.army)).num("slot", pack.slot.getNum()).str("creature", creatureName(pack.type)).num("count", pack.count); }
	void visitRebalanceStacks(RebalanceStacks & pack) override
	{
		set("RebalanceStacks");
		line.str("from", objectName(pack.srcArmy) + " " + slotText(pack.srcArmy, pack.srcSlot)).str("to", objectName(pack.dstArmy) + " " + slotText(pack.dstArmy, pack.dstSlot)).num("count", pack.count);
	}
	void visitBulkRebalanceStacks(BulkRebalanceStacks & pack) override { set("BulkRebalanceStacks"); line.num("moves", static_cast<int64_t>(pack.moves.size())); }
	void visitPutArtifact(PutArtifact & pack) override { set("PutArtifact"); line.str("to", artifactLocation(pack.al)); }
	void visitBulkMoveArtifacts(BulkMoveArtifacts & pack) override
	{
		set("BulkMoveArtifacts");
		line.str("from", objectName(pack.srcArtHolder)).str("to", objectName(pack.dstArtHolder)).num("count", static_cast<int64_t>(pack.artsPack0.size() + pack.artsPack1.size()));
	}
	void visitBulkEraseArtifacts(BulkEraseArtifacts & pack) override { set("BulkEraseArtifacts"); line.str("holder", objectName(pack.artHolder)).num("count", static_cast<int64_t>(pack.posPack.size())); }
	void visitAssembledArtifact(AssembledArtifact & pack) override { set("AssembledArtifact"); line.str("holder", objectName(pack.al.artHolder)).str("artifact", artifactName(pack.artId)); }
	void visitDisassembledArtifact(DisassembledArtifact & pack) override { set("DisassembledArtifact"); line.str("holder", artifactLocation(pack.al)); }
	void visitNewArtifact(NewArtifact & pack) override { set("NewArtifact"); line.str("holder", objectName(pack.artHolder)).str("artifact", artifactName(pack.artId)); }
	void visitInfoWindow(InfoWindow & pack) override { set("InfoWindow"); line.str("player", pack.player.toString()).str("text", pack.text.toString()); }
	void visitBlockingDialog(BlockingDialog & pack) override { set("BlockingDialog"); line.str("player", pack.player.toString()).str("text", pack.text.toString()); }
	void visitOpenWindow(OpenWindow & pack) override { set("OpenWindow"); line.num("window", static_cast<int>(pack.window)).str("object", objectName(pack.object)).str("visitor", objectName(pack.visitor)); }
	void visitChangeObjPos(ChangeObjPos & pack) override { set("ChangeObjPos"); line.str("object", objectName(pack.objid)).str("to", pos(pack.nPos)); }
	void visitHeroRecruited(HeroRecruited & pack) override { set("HeroRecruited"); line.num("hero", pack.hid.getNum()).str("town", objectName(pack.tid)).str("player", pack.player.toString()); }
	void visitGiveHero(GiveHero & pack) override { set("GiveHero"); line.str("hero", objectName(pack.id)).str("player", pack.player.toString()); }
	void visitSetHeroesInTown(SetHeroesInTown & pack) override { set("SetHeroesInTown"); line.str("town", objectName(pack.tid)).str("visiting", objectName(pack.visiting)).str("garrison", objectName(pack.garrison)); }
	void visitChangeSpells(ChangeSpells & pack) override
	{
		set("ChangeSpells");
		std::vector<std::string> ids;
		for(const auto & id : pack.spells)
			ids.push_back(std::to_string(id.getNum()));
		line.str("hero", objectName(pack.hid)).flag("learn", pack.learn != 0).raw("spells", array(ids));
	}
	void visitBattleStart(BattleStart & pack) override
	{
		set("BattleStart");
		line.num("battle", pack.battleID.getNum());
		if(pack.info)
		{
			const BattleSide sides[] = { BattleSide::LEFT_SIDE, BattleSide::RIGHT_SIDE };
			const char * names[] = { "left", "right" };
			for(int i = 0; i < 2; ++i)
			{
				std::string who = pack.info->getSidePlayer(sides[i]).toString();
				if(const auto * hero = pack.info->getSideHero(sides[i]))
					who += " " + hero->getNameTranslated();
				line.str(names[i], who);
				if(const auto * army = pack.info->getSideArmy(sides[i]))
					line.raw(std::string(names[i]) + "Army", armyList(*army));
			}
			line.str("tile", pos(pack.info->tile));
		}
	}
	void visitBattleResult(BattleResult & pack) override
	{
		set("BattleResult");
		line.num("battle", pack.battleID.getNum()).num("result", static_cast<int>(pack.result)).num("winnerSide", static_cast<int>(pack.winner));
		const BattleSide sides[] = { BattleSide::LEFT_SIDE, BattleSide::RIGHT_SIDE };
		const char * names[] = { "leftLost", "rightLost" };
		for(int i = 0; i < 2; ++i)
		{
			std::vector<std::string> lost;
			for(const auto & entry : pack.casualties[sides[i]])
				lost.push_back(object(Line().str("creature", creatureName(entry.first)).num("count", entry.second)));
			line.raw(names[i], array(lost));
		}
	}
	void visitBattleResultsApplied(BattleResultsApplied & pack) override
	{
		set("BattleResultsApplied");
		line.num("battle", pack.battleID.getNum()).str("victor", pack.victor.toString()).str("loser", pack.loser.toString());
		if(pack.raisedStack.getCreature())
			line.str("raised", std::to_string(pack.raisedStack.getCount()) + " " + creatureName(pack.raisedStack.getId()));
	}
	void visitBattleSpellCast(BattleSpellCast & pack) override
	{
		set("BattleSpellCast");
		line.num("battle", pack.battleID.getNum()).num("spell", pack.spellID.getNum()).flag("byHero", pack.castByHero).num("side", static_cast<int>(pack.side));
	}
	void visitBattleEnded(BattleEnded & pack) override { set("BattleEnded"); line.str("victor", pack.victor.toString()).str("loser", pack.loser.toString()); }
};

std::string withoutPrefix(std::string name)
{
	for(const char * prefix : { "struct ", "class " })
		if(name.rfind(prefix, 0) == 0)
			name = name.substr(strlen(prefix));
	return name;
}

/// Packs that say nothing a reader needs (the game's own bookkeeping and the fog of war)
bool isNoise(const std::string & type)
{
	return type == "PackageApplied" || type == "PackageReceived" || type == "TurnTimeUpdate" || type == "FoWChange" || type == "EntitiesChanged"
		|| type == "SetAvailableCreatures" || type == "PlayerBlocked" || type == "SetMovePoints";
}

void flattenJson(const std::string & prefix, const JsonNode & node, std::map<std::string, std::string> & out)
{
	if(node.isStruct())
	{
		for(const auto & entry : node.Struct())
			flattenJson(prefix.empty() ? entry.first : prefix + "." + entry.first, entry.second, out);
	}
	else if(node.isVector())
	{
		size_t i = 0;
		for(const auto & item : node.Vector())
			flattenJson(prefix + "[" + std::to_string(i++) + "]", item, out);
	}
	else
		out[prefix] = node.toCompactString();
}
}

struct PlayRecorder::Impl
{
	std::mutex mutex;
	boost::filesystem::ofstream file;
	boost::filesystem::path path;
	uint64_t bytes = 0;
	bool open = false;
	bool capped = false;
	int64_t lines = 0;
	std::string seat;
};

PlayRecorder & PlayRecorder::get()
{
	static PlayRecorder instance;
	if(!instance.impl)
		instance.impl = std::make_shared<Impl>();
	return instance;
}

bool PlayRecorder::isOpen() const
{
	return impl && impl->open;
}

void PlayRecorder::pruneOldFiles(const std::string & folder)
{
	try
	{
		std::vector<std::pair<std::string, uint64_t>> files;
		for(boost::filesystem::directory_iterator it(folder), end; it != end; ++it)
			if(it->path().extension() == ".jsonl")
				files.emplace_back(it->path().string(), boost::filesystem::file_size(it->path()));
		std::sort(files.begin(), files.end()); // names start with the date and time: oldest first
		uint64_t total = 0;
		for(const auto & f : files)
			total += f.second;
		size_t first = 0;
		while(first + 1 < files.size() && (files.size() - first >= FILE_LIMIT_COUNT || total > FOLDER_LIMIT_BYTES))
		{
			total -= files[first].second;
			boost::filesystem::remove(files[first].first);
			++first;
		}
	}
	catch(...)
	{
	}
}

void PlayRecorder::startGame(PlayerColor seat)
{
	try
	{
		get();
		std::lock_guard lock(impl->mutex);
		if(impl->open)
			return;
		const auto folder = VCMIDirs::get().userDataPath() / "PlayLogs";
		boost::filesystem::create_directories(folder);
		pruneOldFiles(folder.string());

		std::string map = "game";
		if(GAME && GAME->interface() && GAME->interface()->cb && GAME->interface()->cb->getStartInfo())
		{
			map = GAME->interface()->cb->getStartInfo()->mapname;
			const auto slash = map.find_last_of("/\\");
			if(slash != std::string::npos)
				map = map.substr(slash + 1);
			if(map.empty())
				map = "random";
		}
		for(auto & c : map)
			if(!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_')
				c = '_';
		std::string stamp = boost::posix_time::to_iso_extended_string(boost::posix_time::second_clock::local_time());
		for(auto & c : stamp)
			if(c == 'T')
				c = '_';
			else if(c == ':')
				c = '-';
		impl->path = folder / (stamp + "_" + map.substr(0, 60) + ".jsonl");
		impl->file.open(impl->path, std::ios::out | std::ios::app);
		impl->open = impl->file.is_open();
		impl->bytes = 0;
		impl->capped = false;
		impl->lines = 0;
		impl->seat = seat.isValidPlayer() ? seat.toString() : "";
	}
	catch(...)
	{
		return;
	}

	setSettingsChangeObserver([](const std::string & file, const std::vector<std::string> & path, const JsonNode & before, const JsonNode & after)
	{
		PlayRecorder::get().settingChanged(file, path, before, after);
	});

	Line start;
	start.str("file", impl->path.string()).str("seat", impl->seat);
	try
	{
		if(GAME && GAME->interface() && GAME->interface()->cb && GAME->interface()->cb->getStartInfo())
		{
			const auto * info = GAME->interface()->cb->getStartInfo();
			start.str("map", info->mapname).num("difficulty", info->difficulty).num("mode", static_cast<int>(info->mode));
		}
	}
	catch(...)
	{
	}
	write("game_start", start.get());

	// every setting as the game starts, so a change made between two games (in the launcher, for one) shows in the first line of the next
	try
	{
		std::map<std::string, std::string> flat;
		flattenJson("", settings.toJsonNode(), flat);
		Line values;
		for(const auto & entry : flat)
			values.str(entry.first, entry.second);
		write("settings_at_start", Line().raw("settings", "{" + values.get() + "}").get());
	}
	catch(...)
	{
	}
}

void PlayRecorder::stopGame(const std::string & why)
{
	if(!isOpen())
		return;
	write("game_end", Line().str("why", why).get());
	try
	{
		std::lock_guard lock(impl->mutex);
		impl->file.close();
		impl->open = false;
	}
	catch(...)
	{
	}
}

void PlayRecorder::write(const std::string & kind, const std::string & body)
{
	if(!isOpen())
		return;
	try
	{
		Line head;
		head.str("t", boost::posix_time::to_iso_extended_string(boost::posix_time::microsec_clock::local_time()).substr(0, 23));
		try
		{
			if(GAME && GAME->interface() && GAME->interface()->cb)
				head.num("day", GAME->interface()->cb->getDate(Date::DAY));
		}
		catch(...)
		{
		}
		head.str("seat", impl->seat).str("ev", kind);
		std::string text = "{" + head.get() + (body.empty() ? "" : "," + body) + "}\n";

		std::lock_guard lock(impl->mutex);
		if(!impl->open || impl->capped)
			return;
		if(impl->bytes + text.size() > FILE_LIMIT_BYTES)
		{
			impl->capped = true;
			text = "{\"ev\":\"file_full\",\"limitBytes\":" + std::to_string(FILE_LIMIT_BYTES) + "}\n";
		}
		impl->file << text;
		impl->file.flush();
		impl->bytes += text.size();
		++impl->lines;
	}
	catch(...)
	{
	}
}

void PlayRecorder::event(const std::string & kind, const std::string & body)
{
	write(kind, body);
}

void PlayRecorder::request(const CPackForServer & pack, PlayerColor player)
{
	if(!isOpen())
		return;
	try
	{
		RequestDescriber describer;
		const_cast<CPackForServer &>(pack).visit(describer);
		if(!describer.described)
		{
			if(withoutPrefix(typeid(pack).name()) == "SaveLocalState")
				return; // the interface saving where the map view is: bookkeeping, not an action
			describer.line.str("type", withoutPrefix(typeid(pack).name()));
		}
		describer.line.str("player", player.toString());
		write("request", describer.line.get());
	}
	catch(...)
	{
	}
}

void PlayRecorder::clientPack(CPackForClient & pack)
{
	if(!isOpen())
		return;
	try
	{
		const std::string type = withoutPrefix(typeid(pack).name());
		if(isNoise(type))
			return;
		ClientDescriber describer;
		pack.visit(describer);
		if(!describer.described)
			describer.line.str("type", type);
		write("game", describer.line.get());
	}
	catch(...)
	{
	}
}

void PlayRecorder::windowOpened(const std::string & name)
{
	write("window_open", Line().str("window", withoutPrefix(name)).get());
}

void PlayRecorder::windowClosed(const std::string & name)
{
	write("window_close", Line().str("window", withoutPrefix(name)).get());
}

void PlayRecorder::settingChanged(const std::string & file, const std::vector<std::string> & path, const JsonNode & before, const JsonNode & after)
{
	if(!isOpen())
		return;
	try
	{
		std::string where;
		for(const auto & part : path)
			where += (where.empty() ? "" : ".") + part;
		std::map<std::string, std::string> was, now;
		flattenJson(where, before, was);
		flattenJson(where, after, now);
		for(const auto & entry : now)
		{
			const auto old = was.find(entry.first);
			if(old == was.end() || old->second != entry.second)
				write("setting", Line().str("file", file).str("name", entry.first).str("old", old == was.end() ? "(unset)" : old->second).str("new", entry.second).get());
		}
		for(const auto & entry : was)
			if(!now.count(entry.first))
				write("setting", Line().str("file", file).str("name", entry.first).str("old", entry.second).str("new", "(removed)").get());
	}
	catch(...)
	{
	}
}

void PlayRecorder::snapshot(PlayerColor seat, const std::string & reason)
{
	if(!isOpen())
		return;
	try
	{
		if(!GAME || !GAME->interface() || !GAME->interface()->cb)
			return;
		const auto & cb = GAME->interface()->cb;
		const PlayerState * ps = cb->getPlayerState(seat, false);
		if(!ps)
			return;
		const auto & gs = GAME->server().client->gameState();
		const auto heroes = ps->getHeroes();
		const auto towns = ps->getTowns();

		static const char * resourceNames[] = { "wood", "mercury", "ore", "sulfur", "crystal", "gems", "gold", "mithril" };
		Line line;
		line.str("reason", reason).str("player", seat.toString());
		for(int i = 0; i < GameConstants::RESOURCE_QUANTITY && i < 8; ++i)
			line.num(resourceNames[i], ps->resources[i]);
		line.num("income", Statistic::getIncome(&gs, ps)).num("towns", static_cast<int64_t>(towns.size())).num("heroes", static_cast<int64_t>(heroes.size()));
		line.num("dwellings", Statistic::getNumberOfDwellings(ps));
		int mines = 0;
		for(const auto & m : Statistic::getNumMines(&gs, ps))
			mines += m.second;
		line.num("mines", mines);
		line.num("explored_percent", static_cast<int64_t>(Statistic::getMapExploredRatio(&gs, seat) * 100.0f));
		uint64_t heroArmy = 0, garrison = 0;
		int64_t exp = 0;
		int maxLevel = 0;
		int buildings = 0;
		for(const auto * hero : heroes)
		{
			heroArmy += hero->getArmyStrength();
			exp += hero->exp;
			maxLevel = std::max(maxLevel, static_cast<int>(hero->level));
		}
		for(const auto * town : towns)
		{
			garrison += town->getArmyStrength();
			buildings += static_cast<int>(town->getBuildings().size());
		}
		line.num("buildings", buildings).num("army_field", static_cast<int64_t>(heroArmy)).num("army_towns", static_cast<int64_t>(garrison)).num("exp", exp).num("max_level", maxLevel);

		std::vector<std::string> heroTexts;
		for(const auto * hero : heroes)
		{
			Line h;
			h.str("name", hero->getNameTranslated()).num("id", hero->id.getNum()).num("level", hero->level).num("exp", hero->exp).str("at", pos(hero->visitablePos()));
			h.num("attack", hero->getPrimSkillLevel(PrimarySkill::ATTACK)).num("defense", hero->getPrimSkillLevel(PrimarySkill::DEFENSE))
				.num("power", hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER)).num("knowledge", hero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE));
			h.num("morale", hero->moraleVal()).num("luck", hero->luckVal());
			h.num("mana", hero->mana).num("manaLimit", hero->manaLimit()).num("movement", hero->movementPointsRemaining()).num("movementLimit", hero->movementPointsLimit(true));
			h.num("armyStrength", static_cast<int64_t>(hero->getArmyStrength())).raw("army", armyList(*hero));
			std::vector<std::string> worn, pack, skills;
			for(const auto & slot : hero->artifactsWorn)
				if(const auto * art = slot.second.getArt())
					worn.push_back(object(Line().num("slot", slot.first.getNum()).str("artifact", artifactName(art))));
			for(const auto & slot : hero->artifactsInBackpack)
				if(const auto * art = slot.getArt())
					pack.push_back(jsonString(artifactName(art)));
			for(const auto & skill : hero->secSkills)
				skills.push_back(object(Line().num("skill", skill.first.getNum()).num("level", skill.second)));
			h.raw("artifactsWorn", array(worn)).raw("backpack", array(pack)).raw("secondarySkills", array(skills));
			heroTexts.push_back(object(h));
		}
		line.raw("heroList", array(heroTexts));

		std::vector<std::string> townTexts;
		for(const auto * town : towns)
		{
			Line t;
			t.str("name", town->getNameTranslated()).num("id", town->id.getNum()).str("at", pos(town->visitablePos())).num("fort", town->fortLevel());
			t.str("faction", town->getFaction() ? town->getFaction()->getNameTranslated() : "?");
			std::vector<std::string> built;
			for(const auto & b : town->getBuildings())
			{
				std::string name = std::to_string(b.getNum());
				if(town->getTown())
				{
					const auto found = town->getTown()->buildings.find(b);
					if(found != town->getTown()->buildings.end())
						name += " " + found->second->getNameTranslated();
				}
				built.push_back(jsonString(name));
			}
			t.raw("buildings", array(built)).raw("garrison", armyList(*town));
			t.str("visitingHero", town->getVisitingHero() ? town->getVisitingHero()->getNameTranslated() : "").str("garrisonHero", town->getGarrisonHero() ? town->getGarrisonHero()->getNameTranslated() : "");
			townTexts.push_back(object(t));
		}
		line.raw("townList", array(townTexts));
		write("snapshot", line.get());
	}
	catch(...)
	{
	}
}
