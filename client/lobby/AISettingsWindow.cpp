/*
 * AISettingsWindow.cpp, part of VCMI engine (DMB addition)
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "AISettingsWindow.h"

#include "CSelectionBase.h"
#include "../CServerHandler.h"
#include "../GameInstance.h"
#include "../gui/Shortcut.h"
#include "../render/Colors.h"
#include "../widgets/Buttons.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/Images.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/TextControls.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/StartInfo.h"
#include "../../lib/mapping/CMapHeader.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../gui/MenuColour.h"
#include "../../lib/DmbSeatColours.h"
#include "../render/Graphics.h"
#include "../windows/InfoWindows.h"
#include "../windows/GUIClasses.h"
#include "../GameEngine.h"
#include "../gui/WindowHandler.h"
#include "../widgets/CTextInput.h"
#include "../../lib/entities/artifact/CArtHandler.h"
#include "../../lib/entities/artifact/CArtifact.h"
#include "../../lib/DmbSeatBonuses.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/entities/building/CBuilding.h"
#include "../../lib/entities/faction/CFaction.h"
#include "../../lib/entities/faction/CTown.h"
#include "../../lib/entities/faction/CTownHandler.h"

namespace
{
constexpr int ROW_HEIGHT = 24;
constexpr int ROW_WIDTH = 220;
constexpr int LIST_TOP = 72;
constexpr int LIST_ROWS = 7;
constexpr int TIER_TOP = 276;

const ColorRGBA GOLD_FILL(212, 175, 55, 60);
const ColorRGBA GOLD_LINE(212, 175, 55, 255);

bool knownAI(const std::string & name)
{
	return name == "Nullkiller" || name == "Nullkiller2" || name == "OmniAI" || name == "BattleAI" || name == "MMAI" || name == "StupidAI";
}

std::string seatText(const std::string & id)
{
	return LIBRARY->generaltexth->translate("vcmi.dmb.lobby.seatBonuses." + id);
}

/// S380 (K 11-19: "Yes, build all of them in this order: your four ticked ones first, then the rest"; S396 K 11-19: "yes, should implement this
/// in the per seat settings menu"): rows 6 to 19, 22 to 27 of the S330 table and row 29 (a creature stack), opened from Seat Settings. It
/// edits the Seat Settings window's copy of the seat's handicap; its OK sends that copy through the Seat Settings window.
class SeatBonusesWindow final : public CWindowObject
{
	std::shared_ptr<Handicap> pending;
	std::function<void()> send;
	FactionID faction;
	std::shared_ptr<FilledTexturePlayerColored> background;
	std::vector<std::shared_ptr<CIntObject>> parts;
	std::map<std::string, std::shared_ptr<CTextInput>> numbers;
	std::map<std::string, std::shared_ptr<TransparentFilledRectangle>> ticks;
	std::map<std::string, bool> ticked;
	std::shared_ptr<CLabel> spellLabel;
	std::shared_ptr<CLabel> skillLabel;
	std::shared_ptr<CLabel> buildingsLabel;
	std::shared_ptr<CLabel> creatureLabel;
	std::shared_ptr<CButton> okButton;
	SpellID spell;
	SecondarySkill skill;
	std::vector<BuildingID> buildings;
	CreatureID creature;

	static constexpr int LEFT = 20;
	static constexpr int RIGHT = 312;
	static constexpr int TOP = 52;
	static constexpr int STEP = 26;

	void numberField(const std::string & key, int labelX, int fieldX, int y, int width)
	{
		const DmbSeatBonusRow * row = dmbSeatBonusRow(key);
		parts.push_back(std::make_shared<CLabel>(labelX, y + 9, FONT_SMALL, ETextAlignment::CENTERLEFT, Colors::WHITE, seatText(key)));
		const Rect box(fieldX, y + 1, width, 16);
		parts.push_back(std::make_shared<TransparentFilledRectangle>(box.resize(3), ColorRGBA(0, 0, 0, 128), ColorRGBA(64, 64, 64, 64)));
		auto input = std::make_shared<CTextInput>(box, FONT_SMALL, ETextAlignment::CENTERLEFT, true);
		input->setText(std::to_string(pending->seatValue(key, row->neutral)));
		std::weak_ptr<CTextInput> self = input;
		const int maxDigits = static_cast<int>(std::to_string(row->max).size());
		input->setCallback([self, row, maxDigits](const std::string & s)
		{
			// digits only, a minus sign first where the row goes below 0; the top of the range while typing, the bottom on OK
			const bool negative = row->min < 0 && !s.empty() && s[0] == '-';
			std::string digits = s;
			digits.erase(std::remove_if(digits.begin(), digits.end(), [](char c) { return !isdigit(c); }), digits.end());
			digits = digits.substr(0, maxDigits);
			std::string shown = negative ? "-" : "";
			if(!digits.empty())
			{
				const int value = (negative ? -1 : 1) * std::stoi(digits);
				shown = std::to_string(std::clamp(value, std::min(row->min, 0), row->max));
			}
			auto field = self.lock();
			if(field && shown != s)
				field->setText(shown);
		});
		input->setPopupCallback([key](){ CRClickPopup::createAndPush(seatText(key + "Help")); });
		numbers[key] = input;
	}

	void tickBox(const std::string & key, int x, int y)
	{
		parts.push_back(std::make_shared<TransparentFilledRectangle>(Rect(x, y, 18, 18), ColorRGBA(0, 0, 0, 80), GOLD_LINE, 1));
		auto tick = std::make_shared<TransparentFilledRectangle>(Rect(x + 4, y + 4, 10, 10), GOLD_LINE, GOLD_LINE, 1);
		ticked[key] = pending->seatValue(key, 0) != 0;
		tick->setEnabled(ticked[key]);
		ticks[key] = tick;
		parts.push_back(std::make_shared<CLabel>(x + 24, y + 9, FONT_SMALL, ETextAlignment::CENTERLEFT, Colors::WHITE, seatText(key), 120));
		parts.push_back(std::make_shared<LRClickableArea>(Rect(x, y - 2, 140, 22), [this, key]()
		{
			ticked[key] = !ticked[key];
			ticks[key]->setEnabled(ticked[key]);
			redraw();
		}, [key](){ CRClickPopup::createAndPush(seatText(key + "Help")); }));
	}

	std::shared_ptr<CLabel> pickerRow(const std::string & key, int y, std::function<void()> onClick)
	{
		parts.push_back(std::make_shared<CLabel>(RIGHT, y + 9, FONT_SMALL, ETextAlignment::CENTERLEFT, Colors::WHITE, seatText(key)));
		const Rect box(424, y, 156, 18);
		parts.push_back(std::make_shared<TransparentFilledRectangle>(box, ColorRGBA(0, 0, 0, 128), GOLD_LINE, 1));
		auto label = std::make_shared<CLabel>(box.x + 6, y + 9, FONT_SMALL, ETextAlignment::CENTERLEFT, Colors::WHITE, "", box.w - 10);
		parts.push_back(std::make_shared<LRClickableArea>(box, onClick, [key](){ CRClickPopup::createAndPush(seatText(key + "Help")); }));
		return label;
	}

	/// the list of building choices, named as the seat's own town names them (generic names for a random town)
	std::vector<std::pair<BuildingID, std::string>> buildingChoices() const
	{
		std::vector<BuildingID> ids = {BuildingID::FORT, BuildingID::CITADEL, BuildingID::CASTLE, BuildingID::MAGES_GUILD_1, BuildingID::MAGES_GUILD_2,
			BuildingID::MAGES_GUILD_3, BuildingID::MAGES_GUILD_4, BuildingID::MAGES_GUILD_5, BuildingID::TOWN_HALL, BuildingID::CITY_HALL,
			BuildingID::CAPITOL, BuildingID::MARKETPLACE, BuildingID::RESOURCE_SILO, BuildingID::BLACKSMITH};
		for(int level = 0; level < 7; ++level)
			ids.push_back(BuildingID::getDwellingFromLevel(level, 0));
		const CTown * own = faction.isValid() && faction.toFaction() ? faction.toFaction()->town.get() : nullptr;
		const CTown * generic = nullptr;
		for(const auto & f : LIBRARY->townh->objects)
			if(f && f->town && !generic)
				generic = f->town.get();
		std::vector<std::pair<BuildingID, std::string>> choices;
		for(size_t i = 0; i < ids.size(); ++i)
		{
			const bool dwelling = i >= ids.size() - 7;
			const CTown * named = own ? own : (dwelling ? nullptr : generic);
			std::string name;
			if(named && named->buildings.count(ids[i]) && named->buildings.at(ids[i]))
				name = named->buildings.at(ids[i])->getNameTranslated();
			else if(dwelling)
				name = seatText("dwelling") + " " + std::to_string(i - (ids.size() - 7) + 1);
			if(!name.empty())
				choices.emplace_back(ids[i], name);
		}
		return choices;
	}

	void showPicks()
	{
		spellLabel->setText(spell == SpellID::NONE ? seatText("none") : spell.toEntity(LIBRARY)->getNameTranslated());
		skillLabel->setText(skill == SecondarySkill::NONE ? seatText("none") : skill.toEntity(LIBRARY)->getNameTranslated());
		creatureLabel->setText(creature == CreatureID::NONE ? seatText("none") : creature.toEntity(LIBRARY)->getNamePluralTranslated());
		std::string names;
		for(const auto & choice : buildingChoices())
			if(vstd::contains(buildings, choice.first))
				names += (names.empty() ? "" : ", ") + choice.second;
		buildingsLabel->setText(names.empty() ? seatText("none") : names);
		redraw();
	}

	template<typename ID>
	void openList(const std::string & key, const std::vector<ID> & ids, const std::vector<std::string> & names, ID current, std::function<void(ID)> onPick)
	{
		size_t selected = 0;
		for(size_t i = 0; i < ids.size(); ++i)
			if(ids[i] == current)
				selected = i;
		ENGINE->windows().createAndPushWindow<CObjectListWindow>(names, nullptr, seatText(key), "", [this, ids, onPick](int index)
		{
			if(index < 0 || index >= static_cast<int>(ids.size()))
				return;
			onPick(ids[index]);
			showPicks();
		}, selected, std::vector<std::shared_ptr<IImage>>{}, true);
	}

	void chooseSpell()
	{
		std::vector<SpellID> ids{SpellID::NONE};
		std::vector<std::string> names{seatText("none")};
		for(const auto & s : LIBRARY->spellh->objects)
			if(s && !s->isSpecial() && !s->isCreatureAbility() && (s->isAdventure() || s->isCombat()))
			{
				ids.push_back(s->getId());
				names.push_back(s->getNameTranslated());
			}
		openList<SpellID>("spell", ids, names, spell, [this](SpellID id) { spell = id; });
	}

	void chooseSkill()
	{
		std::vector<SecondarySkill> ids{SecondarySkill::NONE};
		std::vector<std::string> names{seatText("none")};
		for(const auto & s : LIBRARY->skillh->objects)
			if(s)
			{
				ids.push_back(s->getId());
				names.push_back(s->getNameTranslated());
			}
		openList<SecondarySkill>("skill", ids, names, skill, [this](SecondarySkill id) { skill = id; });
	}

	void chooseCreature()
	{
		std::vector<CreatureID> ids{CreatureID::NONE};
		std::vector<std::string> names{seatText("none")};
		for(const auto & c : LIBRARY->creh->objects)
			if(c && !c->special)
			{
				ids.push_back(c->getId());
				names.push_back(c->getNamePluralTranslated());
			}
		openList<CreatureID>("creature", ids, names, creature, [this](CreatureID id) { creature = id; });
	}

	void chooseBuilding()
	{
		// a pick adds the building, or takes it away when it is already chosen; the first entry clears them all
		std::vector<BuildingID> ids{BuildingID::NONE};
		std::vector<std::string> names{seatText("none")};
		for(const auto & choice : buildingChoices())
		{
			ids.push_back(choice.first);
			names.push_back((vstd::contains(buildings, choice.first) ? "[x] " : "[  ] ") + choice.second);
		}
		openList<BuildingID>("buildings", ids, names, BuildingID::NONE, [this](BuildingID id)
		{
			if(id == BuildingID::NONE)
				buildings.clear();
			else if(vstd::contains(buildings, id))
				vstd::erase(buildings, id);
			else
				buildings.push_back(id);
		});
	}

	void resetAll()
	{
		for(const auto & field : numbers)
			field.second->setText(std::to_string(dmbSeatBonusRow(field.first)->neutral));
		for(auto & tick : ticked)
		{
			tick.second = false;
			ticks[tick.first]->setEnabled(false);
		}
		spell = SpellID::NONE;
		skill = SecondarySkill::NONE;
		creature = CreatureID::NONE;
		buildings.clear();
		showPicks();
	}

	void accept()
	{
		logGlobal->info("S380: More Bonuses OK");
		for(const auto & field : numbers)
		{
			const DmbSeatBonusRow * row = dmbSeatBonusRow(field.first);
			const std::string text = field.second->getText();
			const int value = (text.empty() || text == "-") ? row->neutral : std::clamp(std::stoi(text), row->min, row->max);
			if(value == row->neutral)
				pending->seatValues.erase(field.first);
			else
				pending->seatValues[field.first] = value;
		}
		for(const auto & tick : ticked)
		{
			if(tick.second)
				pending->seatValues[tick.first] = 1;
			else
				pending->seatValues.erase(tick.first);
		}
		pending->startSpell = spell;
		pending->startSkill = skill;
		pending->startBuildings = buildings;
		pending->stackCreature = creature;
		send();
		close();
	}

public:
	SeatBonusesWindow(PlayerColor color, std::shared_ptr<Handicap> seatHandicap, FactionID seatFaction, std::function<void()> sendAll)
		: CWindowObject(BORDERED)
		, pending(std::move(seatHandicap))
		, send(std::move(sendAll))
		, faction(seatFaction)
		, spell(pending->startSpell)
		, skill(pending->startSkill)
		, buildings(pending->startBuildings)
		, creature(pending->stackCreature)
	{
		OBJECT_CONSTRUCTION;
		addUsedEvents(LCLICK);
		pos = Rect(0, 0, 600, 540);
		background = std::make_shared<FilledTexturePlayerColored>(pos);
		background->setPlayerColor(settings["lobby"]["seatColourWindows"].Bool() ? DmbSeatColours::shown(color) : menuColour());
		parts.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("ITGFLAGS"), DmbSeatColours::shown(color).getNum(), 0, 12, 8));
		parts.push_back(std::make_shared<CLabel>(pos.w / 2 + 10, 18, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, seatText("more")));

		// left: the hero and battle rows (6 to 17 without 14)
		const std::vector<std::string> left = {"manaRegen", "experience", "attack", "defense", "power", "knowledge", "luck", "morale", "scouting",
			"speed", "health", "damage", "necromancy", "creatureExperience", "surrender"};
		for(size_t i = 0; i < left.size(); ++i)
			numberField(left[i], LEFT, 240, TOP + static_cast<int>(i) * STEP, 40);

		// right: daily resources (row 14), the ticks (18, 19), the town and hero rows (22 to 27) and the creature stack (29)
		int y = TOP;
		numberField("daily6", RIGHT, 392, y, 48);
		y += STEP;
		const std::vector<std::string> dailies = {"daily0", "daily2", "daily1", "daily3", "daily4", "daily5"}; // wood, ore, mercury, sulfur, crystal, gems
		for(size_t i = 0; i < dailies.size(); ++i)
			numberField(dailies[i], RIGHT + static_cast<int>(i % 3) * 92, RIGHT + static_cast<int>(i % 3) * 92 + 60, y + static_cast<int>(i / 3) * STEP, 22);
		y += 2 * STEP;
		tickBox("roughGround", RIGHT, y);
		tickBox("freeBoarding", RIGHT + 140, y);
		y += STEP;
		tickBox("waterWalking", RIGHT, y);
		tickBox("flying", RIGHT + 140, y);
		y += STEP;
		tickBox("thievesGuild", RIGHT, y);
		y += STEP;
		numberField("heroLevel", RIGHT, 520, y, 40);
		y += STEP;
		numberField("extraHeroes", RIGHT, 520, y, 40);
		y += STEP;
		numberField("garrisonWeeks", RIGHT, 520, y, 40);
		y += STEP;
		spellLabel = pickerRow("spell", y, [this]() { chooseSpell(); });
		y += STEP;
		skillLabel = pickerRow("skill", y, [this]() { chooseSkill(); });
		y += STEP;
		buildingsLabel = pickerRow("buildings", y, [this]() { chooseBuilding(); });
		y += STEP;
		creatureLabel = pickerRow("creature", y, [this]() { chooseCreature(); });
		y += STEP;
		numberField("stackCount", RIGHT, 520, y, 48);

		// the Reset framed like the pickers, and the OK check mark where Seat Settings has it
		const Rect resetBox(LEFT, 494, 160, 22);
		parts.push_back(std::make_shared<TransparentFilledRectangle>(resetBox, ColorRGBA(0, 0, 0, 128), GOLD_LINE, 1));
		parts.push_back(std::make_shared<CLabel>(resetBox.center().x, resetBox.center().y, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, seatText("resetMore")));
		parts.push_back(std::make_shared<LRClickableArea>(resetBox, [this]() { resetAll(); }, [](){ CRClickPopup::createAndPush(seatText("resetMoreHelp")); }));
		okButton = std::make_shared<CButton>(Point(pos.w - 20 - 64, 488), AnimationPath::builtin("MuBchck"), CButton::tooltip(), [this]() { accept(); }, EShortcut::GLOBAL_RETURN);

		showPicks();
		updateShadow();
		center();
	}

	void notFocusedClick() override
	{
		logGlobal->info("S380: a click outside More Bonuses closes it, nothing kept");
		close();
	}
};
}

AISettingsWindow::AISettingsWindow(PlayerColor seatColor)
	: CWindowObject(BORDERED)
	, color(seatColor)
{
	OBJECT_CONSTRUCTION;

	addUsedEvents(LCLICK);

	// S312: wider and taller than before, so the two gold bars carry their words in the lobby buttons' own lettering
	pos = Rect(0, 0, 600, 540); // S380: 80 taller for the seat bonuses row
	backgroundTexture = std::make_shared<FilledTexturePlayerColored>(pos);
	backgroundTexture->setPlayerColor(settings["lobby"]["seatColourWindows"].Bool() ? DmbSeatColours::shown(color) : menuColour()); // S302: the setting, on by default

	anim.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("ITGFLAGS"), DmbSeatColours::shown(color).getNum(), 0, 12, 8));
	labels.push_back(std::make_shared<CLabel>(pos.w / 2 + 10, 18, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, LIBRARY->generaltexth->translate("vcmi.dmb.lobby.seatSettings")));
	labels.push_back(std::make_shared<CLabel>(20, 48, FONT_MEDIUM, ETextAlignment::TOPLEFT, Colors::YELLOW, LIBRARY->generaltexth->translate("vcmi.lobby.aiSettings.adventure")));
	labels.push_back(std::make_shared<CLabel>(340, 48, FONT_MEDIUM, ETextAlignment::TOPLEFT, Colors::YELLOW, LIBRARY->generaltexth->translate("vcmi.lobby.aiSettings.battle")));
	labels.push_back(std::make_shared<CLabel>(20, TIER_TOP - 26, FONT_MEDIUM, ETextAlignment::TOPLEFT, Colors::YELLOW, LIBRARY->generaltexth->translate("vcmi.lobby.aiSettings.tier")));

	buildChoices(false, adventureChoices, 20);
	buildChoices(true, battleChoices, 340);

	// the tier: "Game Difficulty", then the five chess pieces of the lobby's own difficulty row
	// S312 (K, November 17th, on the S306 buttons: "COME ON GUYS, DID YOU EVEN TRY?!", ""As the Map" is also terribly worded", "the buttons look
	// awful"): the lobby's own RESET widget, the RANSHOW gold bar with its word engraved in the big font (OmniMapGen's mapGenTab.json
	// defaultsButton: RanShowButton166, "font": "big", "style": "engraved"), which draws the lobby buttons' lettering; at 360 pixels for these words.
	// The chess pieces' gold mark shows round it when chosen
	// S313 (K, November 17th: "Game Difficulty is a better wording, but the button needs a pass with our actual font, and probably to shrink it's
	// size"): the same lettering with 16-pixel capitals on a 310-pixel bar; "Reset This Seat" keeps the lobby's 22 ("looks about right")
	// S319 (K, November 17th: "the gold buttons are WAYYYYY TOO BIG"): the bar as tall as the check mark button (32) and the lettering at
	// 14-pixel capitals, both buttons alike
	tierMarks.push_back(std::make_shared<TransparentFilledRectangle>(Rect(17, TIER_TOP - 1, 286, 38), GOLD_FILL, GOLD_LINE, 1));
	auto asMap = std::make_shared<CButton>(Point(20, TIER_TOP), AnimationPath::builtin("SPRITES/RanShowButtonSmall280"), CButton::tooltip(), [this]()
	{
		if(const auto * ps = seat())
			send(ps->aiAdventure, ps->aiBattle, -1);
	});
	asMap->moveBy(Point(0, 18 - asMap->pos.h / 2)); // centred on the chess pieces' row
	auto asMapWords = std::make_shared<CEngravedLabel>(0, 0, FONT_BIG, ETextAlignment::CENTER, LIBRARY->generaltexth->translate("vcmi.lobby.aiSettings.asMap"));
	asMapWords->setLetteringCapitals(14);
	asMap->setOverlay(asMapWords);
	buttons.push_back(asMap);

	constexpr std::array difButns = {"GSPBUT3.DEF", "GSPBUT4.DEF", "GSPBUT5.DEF", "GSPBUT6.DEF", "GSPBUT7.DEF"};
	for(int i = 0; i < 5; i++)
	{
		const int x = 314 + i * 44;
		tierMarks.push_back(std::make_shared<TransparentFilledRectangle>(Rect(x - 3, TIER_TOP - 1, 38, 38), GOLD_FILL, GOLD_LINE, 1));
		tierButtons.push_back(std::make_shared<CButton>(Point(x, TIER_TOP + 2), AnimationPath::builtin(difButns[i]), LIBRARY->generaltexth->zelp[24 + i], [this, i]()
		{
			if(const auto * ps = seat())
				send(ps->aiAdventure, ps->aiBattle, i);
		}));
	}

	description = std::make_shared<CMultiLineLabel>(Rect(20, TIER_TOP + 48, 560, 44), FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "");

	// S073: the thoughts tick box, under the difficulty's text
	thoughtsBox = std::make_shared<TransparentFilledRectangle>(Rect(20, 372, 18, 18), ColorRGBA(0, 0, 0, 80), GOLD_LINE, 1);
	thoughtsTick = std::make_shared<TransparentFilledRectangle>(Rect(24, 376, 10, 10), GOLD_LINE, GOLD_LINE, 1);
	thoughtsLabel = std::make_shared<CLabel>(44, 381, FONT_SMALL, ETextAlignment::CENTERLEFT, Colors::WHITE, LIBRARY->generaltexth->translate("vcmi.lobby.aiSettings.thoughts"), 300);
	thoughtsArea = std::make_shared<LRClickableArea>(Rect(20, 370, 330, 22), [this]()
	{
		if(const auto * ps = seat())
			GAME->server().setPlayerAI(color, ps->aiAdventure, ps->aiBattle, ps->aiTier, !ps->aiThoughts);
	}, nullptr);

	// S312: the same gold bar and lettering, centred on the OK button's row (408 to 440)
	// S319: as tall as the check mark (32), the lettering at 14-pixel capitals like "Game Difficulty"
	auto reset = std::make_shared<CButton>(Point(20, 488), AnimationPath::builtin("SPRITES/RanShowButtonSmall250"), CButton::tooltip(), [this]() { GAME->server().setPlayerAI(color, "", "", -1, false); });
	reset->moveBy(Point(0, 16 - reset->pos.h / 2));
	auto resetWords = std::make_shared<CEngravedLabel>(0, 0, FONT_BIG, ETextAlignment::CENTER, LIBRARY->generaltexth->translate("vcmi.lobby.aiSettings.reset"));
	resetWords->setLetteringCapitals(14);
	reset->setOverlay(resetWords);
	buttons.push_back(reset);

	buttons.push_back(std::make_shared<CButton>(Point(pos.w - 20 - 64, 488), AnimationPath::builtin("MuBchck"), CButton::tooltip(), [this]() { sendBonuses(); close(); }, EShortcut::GLOBAL_RETURN));

	// S424: the Seat Color list under the Battle AI list, two columns of four; the swatch is the colour itself (not the seat's picked one)
	colourTitle = std::make_shared<CLabel>(340, 150, FONT_MEDIUM, ETextAlignment::TOPLEFT, Colors::YELLOW, LIBRARY->generaltexth->translate("vcmi.dmb.lobby.seatColour"));
	for(int i = 0; i < PlayerColor::PLAYER_LIMIT_I; ++i)
	{
		const int x = 340 + (i / 4) * 120;
		const int y = 174 + (i % 4) * 24;
		ColourCell cell;
		cell.mark = std::make_shared<TransparentFilledRectangle>(Rect(x - 2, y - 2, 116, 22), GOLD_FILL, GOLD_LINE, 1);
		cell.swatch = std::make_shared<TransparentFilledRectangle>(Rect(x + 2, y + 2, 14, 14), graphics->playerColors.own[i], ColorRGBA(0, 0, 0, 255), 1);
		cell.label = std::make_shared<CLabel>(x + 22, y + 9, FONT_SMALL, ETextAlignment::CENTERLEFT, Colors::WHITE, LIBRARY->generaltexth->capColors[i], 80);
		cell.heldDot = std::make_shared<TransparentFilledRectangle>(Rect(x + 104, y + 7, 5, 5), ColorRGBA(160, 160, 160, 255), ColorRGBA(160, 160, 160, 255), 1);
		const PlayerColor picked(i);
		cell.area = std::make_shared<LRClickableArea>(Rect(x - 2, y - 2, 116, 22), [this, picked]()
		{
			if(GAME->server().isHost())
				GAME->server().setSeatColor(color, picked);
		}, [](){ CRClickPopup::createAndPush(LIBRARY->generaltexth->translate("vcmi.dmb.lobby.seatColour.help")); });
		colourCells.push_back(cell);
	}
	// S380: the seat bonuses row (every seat): movement, spell points and army as typeable percents (0 to 200), and a starting artifact
	if(const auto * ps = seat())
	{
		bonusParts.push_back(std::make_shared<CLabel>(20, 408, FONT_MEDIUM, ETextAlignment::TOPLEFT, Colors::YELLOW, LIBRARY->generaltexth->translate("vcmi.dmb.lobby.seatBonuses")));
		// the ranges are the S330 table's, approved by K with it: movement 75 to 150 (minus 25 to plus 50 percent), spell points 50 to 200,
		// army size 50 to 300; the top while typing, the bottom on OK
		const auto percentField = [this](int x, const std::string & textId, int value, const std::string & helpId, int top) -> std::shared_ptr<CTextInput>
		{
			bonusParts.push_back(std::make_shared<CLabel>(x, 444, FONT_SMALL, ETextAlignment::CENTERLEFT, Colors::WHITE, LIBRARY->generaltexth->translate(textId)));
			const Rect box(x + 96, 436, 40, 16);
			bonusParts.push_back(std::make_shared<TransparentFilledRectangle>(box.resize(3), ColorRGBA(0, 0, 0, 128), ColorRGBA(64, 64, 64, 64)));
			auto input = std::make_shared<CTextInput>(box, FONT_SMALL, ETextAlignment::CENTERLEFT, true);
			input->setText(std::to_string(value));
			std::weak_ptr<CTextInput> self = input;
			input->setCallback([self, top](const std::string & s)
			{
				std::string digits = s;
				digits.erase(std::remove_if(digits.begin(), digits.end(), [](char c) { return !isdigit(c); }), digits.end());
				digits = digits.substr(0, 3);
				const std::string shown = digits.empty() ? "" : std::to_string(std::min(top, std::stoi(digits)));
				auto field = self.lock();
				if(field && shown != s)
					field->setText(shown);
			});
			input->setPopupCallback([helpId](){ CRClickPopup::createAndPush(LIBRARY->generaltexth->translate(helpId)); });
			bonusParts.push_back(std::make_shared<CLabel>(box.topRight().x, box.center().y, FONT_SMALL, ETextAlignment::CENTERRIGHT, Colors::WHITE, "%"));
			return input;
		};
		movementInput = percentField(20, "vcmi.dmb.lobby.seatBonuses.movement", ps->handicap.percentMovement, "vcmi.dmb.lobby.seatBonuses.movementHelp", 150);
		manaInput = percentField(210, "vcmi.dmb.lobby.seatBonuses.mana", ps->handicap.percentMana, "vcmi.dmb.lobby.seatBonuses.manaHelp", 200);
		armyInput = percentField(400, "vcmi.dmb.lobby.seatBonuses.army", ps->handicap.percentArmy, "vcmi.dmb.lobby.seatBonuses.armyHelp", 300);
		chosenArtifact = ps->handicap.startArtifact;
		bonusParts.push_back(std::make_shared<CLabel>(20, 468, FONT_SMALL, ETextAlignment::CENTERLEFT, Colors::WHITE, LIBRARY->generaltexth->translate("vcmi.dmb.lobby.seatBonuses.artifact")));
		bonusParts.push_back(std::make_shared<TransparentFilledRectangle>(Rect(114, 459, 300, 18), ColorRGBA(0, 0, 0, 128), GOLD_LINE, 1));
		artifactLabel = std::make_shared<CLabel>(120, 468, FONT_SMALL, ETextAlignment::CENTERLEFT, Colors::WHITE, "", 290);
		artifactArea = std::make_shared<LRClickableArea>(Rect(114, 459, 300, 18), [this]() { chooseArtifact(); },
			[](){ CRClickPopup::createAndPush(LIBRARY->generaltexth->translate("vcmi.dmb.lobby.seatBonuses.artifactHelp")); });

		// S380 (rows 6 to 29): "More Bonuses" opens the rest of the seat's bonuses, framed like the artifact box
		pendingBonuses = std::make_shared<Handicap>(ps->handicap);
		const Rect moreBox(430, 459, 150, 18);
		bonusParts.push_back(std::make_shared<TransparentFilledRectangle>(moreBox, ColorRGBA(0, 0, 0, 128), GOLD_LINE, 1));
		moreLabel = std::make_shared<CLabel>(moreBox.center().x, moreBox.center().y, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, seatText("more"));
		moreArea = std::make_shared<LRClickableArea>(moreBox, [this]()
		{
			const auto * seatNow = seat();
			ENGINE->windows().createAndPushWindow<SeatBonusesWindow>(color, pendingBonuses, seatNow ? seatNow->castle : FactionID::RANDOM, [this]() { sendBonuses(); });
		}, [](){ CRClickPopup::createAndPush(seatText("moreHelp")); });
		artifactLabel->setText(chosenArtifact == ArtifactID::NONE ? LIBRARY->generaltexth->translate("vcmi.dmb.lobby.seatBonuses.none") : chosenArtifact.toEntity(LIBRARY)->getNameTranslated());
	}

	// a human seat's window shows only its colour
	for(const auto & row : adventureChoices)
		computerOnly.insert(computerOnly.end(), {row.mark, row.label, row.area});
	for(const auto & row : battleChoices)
		computerOnly.insert(computerOnly.end(), {row.mark, row.label, row.area});
	computerOnly.insert(computerOnly.end(), tierMarks.begin(), tierMarks.end());
	computerOnly.insert(computerOnly.end(), tierButtons.begin(), tierButtons.end());
	computerOnly.insert(computerOnly.end(), {description, thoughtsBox, thoughtsTick, thoughtsLabel, thoughtsArea, asMap, reset});
	for(size_t i = 1; i < 5 && i < labels.size(); ++i) // the Adventure AI, Battle AI and tier headings (labels[0] is the title)
		computerOnly.push_back(labels[i]);

	refresh();
	updateShadow();
	center();
}

void AISettingsWindow::chooseArtifact()
{
	// S380: every artifact a hero can carry from the start (treasure, minor, major, relic), by name; the first entry is none
	std::vector<ArtifactID> ids{ArtifactID::NONE};
	std::vector<std::string> names{LIBRARY->generaltexth->translate("vcmi.dmb.lobby.seatBonuses.none")};
	for(const auto & art : LIBRARY->arth->objects)
	{
		if(!art)
			continue;
		const auto kind = art->aClass;
		if(kind == EArtifactClass::ART_TREASURE || kind == EArtifactClass::ART_MINOR || kind == EArtifactClass::ART_MAJOR || kind == EArtifactClass::ART_RELIC)
		{
			ids.push_back(art->getId());
			names.push_back(art->getNameTranslated());
		}
	}
	size_t current = 0;
	for(size_t i = 0; i < ids.size(); ++i)
		if(ids[i] == chosenArtifact)
			current = i;
	ENGINE->windows().createAndPushWindow<CObjectListWindow>(names, nullptr, LIBRARY->generaltexth->translate("vcmi.dmb.lobby.seatBonuses.artifact"),
		"", [this, ids, names](int index)
		{
			if(index < 0 || index >= static_cast<int>(ids.size()))
				return;
			chosenArtifact = ids[index];
			artifactLabel->setText(names[index]);
			redraw();
		}, current, std::vector<std::shared_ptr<IImage>>{}, true);
}

void AISettingsWindow::sendBonuses() const
{
	const auto * ps = seat();
	if(!ps || !GAME->server().isHost() || !movementInput)
		return;
	const auto number = [](const std::shared_ptr<CTextInput> & field, int bottom, int top)
	{
		const std::string text = field->getText();
		return text.empty() ? 100 : std::clamp(std::stoi(text), bottom, top);
	};
	// the lobby's own fields (resources, income, growth, Sees map) as the lobby has them; the bonuses from these two windows
	Handicap handicap = ps->handicap;
	handicap.percentMovement = number(movementInput, 75, 150);
	handicap.percentMana = number(manaInput, 50, 200);
	handicap.percentArmy = number(armyInput, 50, 300);
	handicap.startArtifact = chosenArtifact;
	if(pendingBonuses)
	{
		handicap.seatValues = pendingBonuses->seatValues;
		handicap.startSpell = pendingBonuses->startSpell;
		handicap.startSkill = pendingBonuses->startSkill;
		handicap.startBuildings = pendingBonuses->startBuildings;
		handicap.stackCreature = pendingBonuses->stackCreature;
	}
	const Handicap & now = ps->handicap;
	if(handicap.percentMovement != now.percentMovement || handicap.percentMana != now.percentMana || handicap.percentArmy != now.percentArmy
		|| handicap.startArtifact != now.startArtifact || handicap.seatValues != now.seatValues || handicap.startSpell != now.startSpell
		|| handicap.startSkill != now.startSkill || handicap.startBuildings != now.startBuildings || handicap.stackCreature != now.stackCreature)
	{
		logGlobal->info("S380: seat bonuses of %s sent, %d number rows", color.toString(), static_cast<int>(handicap.seatValues.size()));
		GAME->server().setPlayerHandicap(color, handicap);
	}
}

const PlayerSettings * AISettingsWindow::seat() const
{
	const auto & seats = SEL->getStartInfo()->playerInfos;
	const auto found = seats.find(color);
	return found == seats.end() ? nullptr : &found->second;
}

void AISettingsWindow::buildChoices(bool battleList, std::vector<Choice> & into, int x)
{
	int y = LIST_TOP;
	for(const auto & choice : CServerHandler::seatAIChoices(battleList, true))
	{
		if(into.size() >= static_cast<size_t>(LIST_ROWS))
			break;

		Choice row;
		row.name = choice.name;
		row.mark = std::make_shared<TransparentFilledRectangle>(Rect(x, y, ROW_WIDTH, ROW_HEIGHT - 2), GOLD_FILL, GOLD_LINE, 1);
		if(!choice.problem.empty())
		{
			// S293 D1: an AI that cannot load is shown grey; a click says why in the text under the tiers, and picks nothing
			const std::string text = displayName(choice.name) + " " + LIBRARY->generaltexth->translate("vcmi.lobby.aiSettings.cannotLoad");
			row.label = std::make_shared<CLabel>(x + 8, y + (ROW_HEIGHT - 2) / 2, FONT_SMALL, ETextAlignment::CENTERLEFT, ColorRGBA(140, 140, 140), text, ROW_WIDTH - 14);
			std::string why = LIBRARY->generaltexth->translate("vcmi.lobby.aiSettings.cannotLoadWhy");
			boost::algorithm::replace_first(why, "%s", displayName(choice.name));
			boost::algorithm::replace_first(why, "%s", choice.problem);
			row.area = std::make_shared<LRClickableArea>(Rect(x, y, ROW_WIDTH, ROW_HEIGHT - 2), [this, why]()
			{
				description->setText(why);
				redraw();
			}, nullptr);
			into.push_back(row);
			y += ROW_HEIGHT;
			continue;
		}
		// an AI plugin the mod catalog does not vouch for is marked, as the launcher marks it: the host is asked before the start
		const std::string text = displayName(choice.name) + (choice.untrusted ? " *" : "");
		row.label = std::make_shared<CLabel>(x + 8, y + (ROW_HEIGHT - 2) / 2, FONT_SMALL, ETextAlignment::CENTERLEFT, Colors::WHITE, text, ROW_WIDTH - 14);
		const std::string picked = choice.name;
		row.area = std::make_shared<LRClickableArea>(Rect(x, y, ROW_WIDTH, ROW_HEIGHT - 2), [this, battleList, picked]()
		{
			const auto * ps = seat();
			if(!ps)
				return;
			if(battleList)
				send(ps->aiAdventure, picked, ps->aiTier);
			else
				send(picked, ps->aiBattle, ps->aiTier);
		}, nullptr);
		into.push_back(row);
		y += ROW_HEIGHT;
	}
}

void AISettingsWindow::send(const std::string & adventureAI, const std::string & battleAI, int tier) const
{
	sendKeepingThoughts(adventureAI, battleAI, tier);
}

void AISettingsWindow::sendKeepingThoughts(const std::string & adventureAI, const std::string & battleAI, int tier) const
{
	const auto * ps = seat();
	GAME->server().setPlayerAI(color, adventureAI, battleAI, tier, ps && ps->aiThoughts && adventureAICanThink(adventureAI.empty() && ps ? resolvedAdventure(*ps) : adventureAI));
}

bool AISettingsWindow::adventureAICanThink(const std::string & name) const
{
	for(const auto & choice : CServerHandler::seatAIChoices(false))
		if(choice.name == name)
			return choice.thoughts;
	return false;
}

void AISettingsWindow::notFocusedClick()
{
	logGlobal->info("S380: a click outside Seat Settings closes it");
	close();
}

void AISettingsWindow::refresh()
{
	const auto * ps = seat();
	if(!ps)
	{
		close();
		return;
	}

	// S424: the window follows the seat's colour when it changes
	backgroundTexture->setPlayerColor(settings["lobby"]["seatColourWindows"].Bool() ? DmbSeatColours::shown(color) : menuColour());
	if(!anim.empty())
		anim.front()->setFrame(DmbSeatColours::shown(color).getNum());
	// S424: the colour list, for every seat
	for(int i = 0; i < static_cast<int>(colourCells.size()); ++i)
	{
		const PlayerColor colour(i);
		bool held = false;
		for(const auto & other : SEL->getStartInfo()->playerInfos)
			if(other.first != color && other.second.shown() == colour)
				held = true;
		colourCells[i].mark->setEnabled(ps->shown() == colour);
		colourCells[i].heldDot->setEnabled(held);
		colourCells[i].label->setColor(held ? ColorRGBA(170, 170, 170) : Colors::WHITE);
	}
	const bool computer = ps->isControlledByAI();
	for(const auto & part : computerOnly)
		if(part)
			part->setEnabled(computer);
	if(!computer)
	{
		redraw();
		return;
	}

	const std::string adventure = resolvedAdventure(*ps);
	const std::string battle = resolvedBattle(*ps);
	for(auto & row : adventureChoices)
		row.mark->setEnabled(row.name == adventure);
	for(auto & row : battleChoices)
		row.mark->setEnabled(row.name == battle);

	const bool canThink = adventureAICanThink(adventure);
	thoughtsBox->setEnabled(canThink);
	thoughtsLabel->setEnabled(canThink);
	thoughtsArea->setEnabled(canThink);
	thoughtsTick->setEnabled(canThink && ps->aiThoughts);

	const int tierSetting = ps->aiTier >= 0 && ps->aiTier <= 4 ? ps->aiTier : -1;
	for(size_t i = 0; i < tierMarks.size(); ++i)
		tierMarks[i]->setEnabled(static_cast<int>(i) == tierSetting + 1);

	std::string text = knownAI(adventure) ? LIBRARY->generaltexth->translate("vcmi.lobby.aiInfo." + adventure) : displayName(adventure);
	text += "\n";
	text += tierSetting < 0 ? LIBRARY->generaltexth->translate("vcmi.lobby.aiTierInfo.map") : LIBRARY->generaltexth->translate("vcmi.lobby.aiTierInfo." + std::to_string(tierSetting));
	description->setText(text);
	redraw();
}

std::string AISettingsWindow::displayName(const std::string & name)
{
	if(name == "Nullkiller")
		return "Nullkiller 1";
	if(name == "Nullkiller2")
		return "Nullkiller 2";
	if(name == "BattleAI")
		return "Battle AI";
	if(name == "StupidAI")
		return "Stupid AI";
	return name;
}

bool AISettingsWindow::alliedToHuman(const PlayerSettings & seat)
{
	const TeamID team = SEL->getPlayerInfo(seat.color).team;
	if(team == TeamID::NO_TEAM)
		return false;
	for(const auto & other : SEL->getStartInfo()->playerInfos)
		if(other.first != seat.color && other.second.isControlledByHuman() && SEL->getPlayerInfo(other.first).team == team)
			return true;
	return false;
}

std::string AISettingsWindow::resolvedAdventure(const PlayerSettings & seat)
{
	return seat.aiAdventure.empty() ? CServerHandler::seatAIDefault(false, alliedToHuman(seat)) : seat.aiAdventure;
}

std::string AISettingsWindow::resolvedBattle(const PlayerSettings & seat)
{
	return seat.aiBattle.empty() ? CServerHandler::seatAIDefault(true, alliedToHuman(seat)) : seat.aiBattle;
}

int AISettingsWindow::effectiveTier(const PlayerSettings & seat)
{
	return seat.aiTier >= 0 && seat.aiTier <= 4 ? seat.aiTier : std::clamp<int>(SEL->getStartInfo()->difficulty, 0, 4);
}
