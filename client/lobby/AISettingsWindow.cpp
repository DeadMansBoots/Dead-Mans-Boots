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
#include "../render/IFont.h"
#include "../render/IRenderHandler.h"
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
/// S472 (K, November 22nd: "lets just put all the bonuses on one page (an external page)"): the page has every seat bonus, the four that
/// were in Seat Settings (movement, spell points, army size, artifact) included, and fits the 800x600 screen (600x540)
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
	std::shared_ptr<CLabel> artifactLabel;
	std::shared_ptr<CButton> okButton;
	SpellID spell;
	SecondarySkill skill;
	std::vector<BuildingID> buildings;
	CreatureID creature;
	ArtifactID artifact;
	/// S472: the movement, spell point and army percents (Handicap's own fields), by their text key
	std::map<std::string, std::shared_ptr<CTextInput>> percents;

	static constexpr int LEFT = 20;
	static constexpr int RIGHT = 312;
	static constexpr int TOP = 52;
	static constexpr int STEP = 24; // S472: 26 before; the four Seat Settings rows came onto this page

	/// S380's percent rows (the S330 table's ranges, approved by K with it): typeable, the top while typing, the bottom on OK
	struct PercentRange
	{
		int bottom;
		int top;
	};
	static PercentRange percentRange(const std::string & key)
	{
		if(key == "movement")
			return {75, 150};
		if(key == "mana")
			return {50, 200};
		return {50, 300};
	}

	void percentField(const std::string & key, int value, int y)
	{
		parts.push_back(std::make_shared<CLabel>(LEFT, y + 9, FONT_SMALL, ETextAlignment::CENTERLEFT, Colors::WHITE, seatText(key)));
		const Rect box(240, y + 1, 40, 16);
		parts.push_back(std::make_shared<TransparentFilledRectangle>(box.resize(3), ColorRGBA(0, 0, 0, 128), ColorRGBA(64, 64, 64, 64)));
		auto input = std::make_shared<CTextInput>(box, FONT_SMALL, ETextAlignment::CENTERLEFT, true);
		input->setText(std::to_string(value));
		std::weak_ptr<CTextInput> self = input;
		const int top = percentRange(key).top;
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
		input->setPopupCallback([key](){ CRClickPopup::createAndPush(seatText(key + "Help")); });
		parts.push_back(std::make_shared<CLabel>(box.topRight().x + 2, box.center().y, FONT_SMALL, ETextAlignment::CENTERLEFT, Colors::WHITE, "%"));
		percents[key] = input;
	}

	int percentValue(const std::string & key) const
	{
		const std::string text = percents.at(key)->getText();
		const auto range = percentRange(key);
		return text.empty() ? 100 : std::clamp(std::stoi(text), range.bottom, range.top);
	}

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
		artifactLabel->setText(artifact == ArtifactID::NONE ? seatText("none") : artifact.toEntity(LIBRARY)->getNameTranslated());
		redraw();
	}

	void chooseArtifact()
	{
		// S380: every artifact a hero can carry from the start (treasure, minor, major, relic), by name; the first entry is none
		std::vector<ArtifactID> ids{ArtifactID::NONE};
		std::vector<std::string> names{seatText("none")};
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
		openList<ArtifactID>("artifact", ids, names, artifact, [this](ArtifactID id) { artifact = id; });
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
		for(const auto & field : percents)
			field.second->setText("100");
		spell = SpellID::NONE;
		skill = SecondarySkill::NONE;
		creature = CreatureID::NONE;
		artifact = ArtifactID::NONE;
		buildings.clear();
		showPicks();
	}

	void accept()
	{
		logGlobal->info("S472: Seat Bonuses OK");
		pending->percentMovement = percentValue("movement");
		pending->percentMana = percentValue("mana");
		pending->percentArmy = percentValue("army");
		pending->startArtifact = artifact;
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
		, artifact(pending->startArtifact)
	{
		OBJECT_CONSTRUCTION;
		addUsedEvents(LCLICK);
		pos = Rect(0, 0, 600, 540);
		background = std::make_shared<FilledTexturePlayerColored>(pos);
		background->setPlayerColor(settings["lobby"]["seatColourWindows"].Bool() ? DmbSeatColours::shown(color) : menuColour());
		parts.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("ITGFLAGS"), DmbSeatColours::shown(color).getNum(), 0, 12, 8));
		parts.push_back(std::make_shared<CLabel>(pos.w / 2 + 10, 18, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, LIBRARY->generaltexth->translate("vcmi.dmb.lobby.seatBonuses")));

		// left: the hero and battle rows (6 to 17 without 14), then the movement, spell point and army percents (S472: from Seat Settings)
		const std::vector<std::string> left = {"manaRegen", "experience", "attack", "defense", "power", "knowledge", "luck", "morale", "scouting",
			"speed", "health", "damage", "necromancy", "creatureExperience", "surrender"};
		for(size_t i = 0; i < left.size(); ++i)
			numberField(left[i], LEFT, 240, TOP + static_cast<int>(i) * STEP, 40);
		const int percentTop = TOP + static_cast<int>(left.size()) * STEP;
		percentField("movement", pending->percentMovement, percentTop);
		percentField("mana", pending->percentMana, percentTop + STEP);
		percentField("army", pending->percentArmy, percentTop + 2 * STEP);

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
		y += STEP;
		artifactLabel = pickerRow("artifact", y, [this]() { chooseArtifact(); }); // S472: from Seat Settings

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

/// S463: the Seat Color list, opened under its box and drawn as the lobby's turn timer dropdown draws its list
/// (config/widgets/turnOptionsDropdownLibrary.json: the blue DiBoxBck texture, rows of 20 on half-black, a yellow frame on the row under the
/// mouse): every colour with its name in its own colour, the seat's own marked gold, and beside a colour another seat has, "Seat N" in grey
/// (N counts the lobby's seat rows from the top). The host's click picks (a held colour swaps the two seats); any other click closes the list
class SeatColourList final : public CWindowObject
{
	static constexpr int ROW = 20;
	PlayerColor color;
	std::shared_ptr<FilledTexturePlayerColored> background;
	std::shared_ptr<TransparentFilledRectangle> frame;
	std::vector<std::shared_ptr<TransparentFilledRectangle>> rowBacks;
	std::vector<std::shared_ptr<TransparentFilledRectangle>> marks;
	std::vector<std::shared_ptr<TransparentFilledRectangle>> swatches;
	std::vector<std::shared_ptr<CLabel>> names;
	std::vector<std::shared_ptr<CLabel>> heldBy;
	std::vector<std::shared_ptr<TransparentFilledRectangle>> hovers;
	int hovered = -1;

	int rowAt(const Point & cursorPosition) const
	{
		if(!pos.isInside(cursorPosition))
			return -1;
		const int row = (cursorPosition.y - pos.y - 1) / ROW;
		return row >= 0 && row < static_cast<int>(names.size()) ? row : -1;
	}

public:
	SeatColourList(PlayerColor seatColor, const Point & under, int width)
		: CWindowObject(SHADOW_DISABLED)
		, color(seatColor)
	{
		OBJECT_CONSTRUCTION;
		addUsedEvents(LCLICK | MOVE);
		pos = Rect(0, 0, width, PlayerColor::PLAYER_LIMIT_I * ROW + 2);
		background = std::make_shared<FilledTexturePlayerColored>(pos);
		background->setPlayerColor(PlayerColor(1)); // the stock list's "color": "blue"
		frame = std::make_shared<TransparentFilledRectangle>(Rect(0, 0, pos.w, pos.h), ColorRGBA(0, 0, 0, 0), ColorRGBA(64, 80, 128, 128), 1);
		const auto font = ENGINE->renderHandler().loadFont(FONT_SMALL);
		for(int i = 0; i < PlayerColor::PLAYER_LIMIT_I; ++i)
		{
			const int y = 1 + i * ROW;
			const std::string name = LIBRARY->generaltexth->capColors[i];
			rowBacks.push_back(std::make_shared<TransparentFilledRectangle>(Rect(1, y, width - 2, ROW - 1), ColorRGBA(0, 0, 0, 128), ColorRGBA(0, 0, 0, 128), 1));
			marks.push_back(std::make_shared<TransparentFilledRectangle>(Rect(2, y + 1, width - 4, ROW - 3), GOLD_FILL, GOLD_LINE, 1));
			swatches.push_back(std::make_shared<TransparentFilledRectangle>(Rect(6, y + 3, 13, 13), graphics->playerColors.own[i], ColorRGBA(0, 0, 0, 255), 1));
			names.push_back(std::make_shared<CLabel>(26, y + ROW / 2, FONT_SMALL, ETextAlignment::CENTERLEFT, graphics->playerColors.own[i], name));
			heldBy.push_back(std::make_shared<CLabel>(26 + static_cast<int>(font->getStringWidth(name)) + 8, y + ROW / 2, FONT_SMALL, ETextAlignment::CENTERLEFT,
				ColorRGBA(150, 150, 150), ""));
			hovers.push_back(std::make_shared<TransparentFilledRectangle>(Rect(2, y + 1, width - 4, ROW - 3), ColorRGBA(0, 0, 0, 0), ColorRGBA(255, 255, 0, 255), 1));
			hovers.back()->disable();
		}
		refresh();
		moveTo(under);
		fitToScreen(4);
	}

	/// the marks and the "Seat N" texts as the lobby state has them now
	void refresh()
	{
		const auto & seats = SEL->getStartInfo()->playerInfos;
		const auto own = seats.find(color);
		for(int i = 0; i < static_cast<int>(names.size()); ++i)
		{
			const PlayerColor colour(i);
			std::string holder;
			int number = 0;
			for(const auto & other : seats)
			{
				++number;
				if(other.first != color && other.second.shown() == colour)
				{
					holder = LIBRARY->generaltexth->translate("vcmi.dmb.lobby.seatColour.heldBy");
					boost::algorithm::replace_first(holder, "%d", std::to_string(number));
				}
			}
			marks[i]->setEnabled(own != seats.end() && own->second.shown() == colour);
			heldBy[i]->setText(holder);
		}
		redraw();
	}

	void mouseMoved(const Point & cursorPosition, const Point & lastUpdateDistance) override
	{
		const int row = rowAt(cursorPosition);
		if(row == hovered)
			return;
		hovered = row;
		for(int i = 0; i < static_cast<int>(hovers.size()); ++i)
			hovers[i]->setEnabled(i == row);
		redraw();
	}

	void clickReleased(const Point & cursorPosition) override
	{
		const int row = rowAt(cursorPosition);
		const PlayerColor seatColour = color;
		close();
		if(row >= 0 && GAME->server().isHost())
		{
			logGlobal->info("S463: Seat Color of %s: %s picked", seatColour.toString(), PlayerColor(row).toString());
			GAME->server().setSeatColor(seatColour, PlayerColor(row));
		}
	}

	void notFocusedClick() override
	{
		close();
	}
};

AISettingsWindow::AISettingsWindow(PlayerColor seatColor)
	: CWindowObject(BORDERED)
	, color(seatColor)
{
	OBJECT_CONSTRUCTION;

	addUsedEvents(LCLICK);

	// S312: wider and taller than before, so the two gold bars carry their words in the lobby buttons' own lettering
	pos = Rect(0, 0, 600, 500); // S380: 80 taller for the seat bonuses row; S472: 40 of it back, the row is one button now
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
	auto reset = std::make_shared<CButton>(Point(20, 448), AnimationPath::builtin("SPRITES/RanShowButtonSmall250"), CButton::tooltip(), [this]() { GAME->server().setPlayerAI(color, "", "", -1, false); });
	reset->moveBy(Point(0, 16 - reset->pos.h / 2));
	auto resetWords = std::make_shared<CEngravedLabel>(0, 0, FONT_BIG, ETextAlignment::CENTER, LIBRARY->generaltexth->translate("vcmi.lobby.aiSettings.reset"));
	resetWords->setLetteringCapitals(14);
	reset->setOverlay(resetWords);
	buttons.push_back(reset);

	buttons.push_back(std::make_shared<CButton>(Point(pos.w - 20 - 64, 448), AnimationPath::builtin("MuBchck"), CButton::tooltip(), [this]() { sendBonuses(); close(); }, EShortcut::GLOBAL_RETURN));

	// S424: Seat Color under the Battle AI list. S463: one box, the lobby's dropdown bar (lobby/dropdown, 200x18, its arrow at the right)
	// with the seat's colour: the swatch and the name in that colour; a click opens the list under it
	colourTitle = std::make_shared<CLabel>(340, 150, FONT_MEDIUM, ETextAlignment::TOPLEFT, Colors::YELLOW, LIBRARY->generaltexth->translate("vcmi.dmb.lobby.seatColour"));
	colourBar = std::make_shared<CAnimImage>(AnimationPath::builtin("lobby/dropdown"), 0, 0, 340, 174);
	colourName =std::make_shared<CLabel>(364, 183, FONT_SMALL, ETextAlignment::CENTERLEFT, Colors::WHITE, "", 150);
	colourArea = std::make_shared<LRClickableArea>(Rect(340, 174, 200, 18), [this]()
	{
		const Point under(colourBar->pos.x, colourBar->pos.y + colourBar->pos.h);
		auto list = std::make_shared<SeatColourList>(color, under, colourBar->pos.w);
		colourList = list;
		ENGINE->windows().pushWindow(list);
	}, [](){ CRClickPopup::createAndPush(LIBRARY->generaltexth->translate("vcmi.dmb.lobby.seatColour.help")); });
	// S472 (K, November 22nd: "lets just put all the bonuses on one page (an external page)"): Seat Settings keeps one button, "Bonuses",
	// which opens the page with every seat bonus (the S380 percents and artifact included); the page edits this copy of the seat's handicap
	// and its OK sends it
	if(const auto * ps = seat())
	{
		bonusParts.push_back(std::make_shared<CLabel>(20, 408, FONT_MEDIUM, ETextAlignment::TOPLEFT, Colors::YELLOW, LIBRARY->generaltexth->translate("vcmi.dmb.lobby.seatBonuses")));
		pendingBonuses = std::make_shared<Handicap>(ps->handicap);
		const Rect moreBox(180, 407, 160, 22);
		bonusParts.push_back(std::make_shared<TransparentFilledRectangle>(moreBox, ColorRGBA(0, 0, 0, 128), GOLD_LINE, 1));
		moreLabel = std::make_shared<CLabel>(moreBox.center().x, moreBox.center().y, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, seatText("more"));
		moreArea = std::make_shared<LRClickableArea>(moreBox, [this]()
		{
			const auto * seatNow = seat();
			ENGINE->windows().createAndPushWindow<SeatBonusesWindow>(color, pendingBonuses, seatNow ? seatNow->castle : FactionID::RANDOM, [this]() { sendBonuses(); });
		}, [](){ CRClickPopup::createAndPush(seatText("moreHelp")); });
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

void AISettingsWindow::sendBonuses() const
{
	const auto * ps = seat();
	if(!ps || !GAME->server().isHost() || !pendingBonuses)
		return;
	// the lobby's own fields (resources, income, growth, Sees map) as the lobby has them; the bonuses from the Seat Bonuses page
	Handicap handicap = ps->handicap;
	{
		handicap.percentMovement = pendingBonuses->percentMovement;
		handicap.percentMana = pendingBonuses->percentMana;
		handicap.percentArmy = pendingBonuses->percentArmy;
		handicap.startArtifact = pendingBonuses->startArtifact;
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
		// S463: only the top window can close (WindowBase::close throws otherwise), so the colour list over this window closes first
		if(auto list = colourList.lock(); list && ENGINE->windows().isTopWindow(list.get()))
			list->close();
		if(ENGINE->windows().isTopWindow(this))
			close();
		return;
	}

	// S424: the window follows the seat's colour when it changes
	backgroundTexture->setPlayerColor(settings["lobby"]["seatColourWindows"].Bool() ? DmbSeatColours::shown(color) : menuColour());
	if(!anim.empty())
		anim.front()->setFrame(DmbSeatColours::shown(color).getNum());
	// S424: the seat's colour, for every seat. S463: in the box, and in the list when it is open
	const int shownIndex = ps->shown().getNum();
	if(shownIndex >= 0 && shownIndex < PlayerColor::PLAYER_LIMIT_I)
	{
		OBJECT_CONSTRUCTION;
		colourSwatch = std::make_shared<TransparentFilledRectangle>(Rect(345, 177, 12, 12), graphics->playerColors.own[shownIndex], ColorRGBA(0, 0, 0, 255), 1);
		colourName->setColor(graphics->playerColors.own[shownIndex]);
		colourName->setText(LIBRARY->generaltexth->capColors[shownIndex]);
	}
	if(auto list = colourList.lock())
		list->refresh();
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
