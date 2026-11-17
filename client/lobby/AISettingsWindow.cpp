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
#include "../../lib/GameLibrary.h"
#include "../../lib/StartInfo.h"
#include "../../lib/mapping/CMapHeader.h"
#include "../../lib/texts/CGeneralTextHandler.h"

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
}

AISettingsWindow::AISettingsWindow(PlayerColor seatColor)
	: CWindowObject(BORDERED)
	, color(seatColor)
{
	OBJECT_CONSTRUCTION;

	addUsedEvents(LCLICK);

	pos = Rect(0, 0, 500, 420);
	backgroundTexture = std::make_shared<FilledTexturePlayerColored>(pos);
	backgroundTexture->setPlayerColor(color);

	anim.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("ITGFLAGS"), color.getNum(), 0, 12, 8));
	labels.push_back(std::make_shared<CLabel>(pos.w / 2 + 10, 18, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, LIBRARY->generaltexth->translate("vcmi.lobby.aiSettings.title")));
	labels.push_back(std::make_shared<CLabel>(20, 48, FONT_MEDIUM, ETextAlignment::TOPLEFT, Colors::YELLOW, LIBRARY->generaltexth->translate("vcmi.lobby.aiSettings.adventure")));
	labels.push_back(std::make_shared<CLabel>(260, 48, FONT_MEDIUM, ETextAlignment::TOPLEFT, Colors::YELLOW, LIBRARY->generaltexth->translate("vcmi.lobby.aiSettings.battle")));
	labels.push_back(std::make_shared<CLabel>(20, TIER_TOP - 26, FONT_MEDIUM, ETextAlignment::TOPLEFT, Colors::YELLOW, LIBRARY->generaltexth->translate("vcmi.lobby.aiSettings.tier")));

	buildChoices(false, adventureChoices, 20);
	buildChoices(true, battleChoices, 260);

	// the tier: "as the map", then the five chess pieces of the lobby's own difficulty row
	// S301 (K, November 17th: "Let's change the buttons for "as the map" and "Reset to launcher defaults" with the other button schemas we already
	// have established....no ui diversions unles i okay them") and S306 (K: "I was thinking they would be gold with black text, you know, like most
	// the other buttons...we don't have any brown background buttons in the rest of the pre-game ui"): the lobby's gold bar (RANSHOW, made at
	// this width, as the list window's Delete) with its word carved in black, and the chess pieces' gold mark when chosen
	tierMarks.push_back(std::make_shared<TransparentFilledRectangle>(Rect(17, TIER_TOP - 1, 102, 38), GOLD_FILL, GOLD_LINE, 1));
	auto asMap = std::make_shared<CButton>(Point(20, TIER_TOP), AnimationPath::builtin("SPRITES/RanShowButton96"), CButton::tooltip(), [this]()
	{
		if(const auto * ps = seat())
			send(ps->aiAdventure, ps->aiBattle, -1);
	});
	asMap->moveBy(Point(0, 18 - asMap->pos.h / 2)); // centred on the chess pieces' row
	asMap->setOverlay(std::make_shared<CEngravedLabel>(0, 0, FONT_MEDIUM, ETextAlignment::CENTER, LIBRARY->generaltexth->translate("vcmi.lobby.aiSettings.asMap")));
	buttons.push_back(asMap);

	constexpr std::array difButns = {"GSPBUT3.DEF", "GSPBUT4.DEF", "GSPBUT5.DEF", "GSPBUT6.DEF", "GSPBUT7.DEF"};
	for(int i = 0; i < 5; i++)
	{
		const int x = 130 + i * 44;
		tierMarks.push_back(std::make_shared<TransparentFilledRectangle>(Rect(x - 3, TIER_TOP - 1, 38, 38), GOLD_FILL, GOLD_LINE, 1));
		tierButtons.push_back(std::make_shared<CButton>(Point(x, TIER_TOP + 2), AnimationPath::builtin(difButns[i]), LIBRARY->generaltexth->zelp[24 + i], [this, i]()
		{
			if(const auto * ps = seat())
				send(ps->aiAdventure, ps->aiBattle, i);
		}));
	}

	description = std::make_shared<CMultiLineLabel>(Rect(20, TIER_TOP + 48, 460, 44), FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "");

	// S073: the thoughts tick box, under the lists on the right
	thoughtsBox = std::make_shared<TransparentFilledRectangle>(Rect(260, 372, 18, 18), ColorRGBA(0, 0, 0, 80), GOLD_LINE, 1);
	thoughtsTick = std::make_shared<TransparentFilledRectangle>(Rect(264, 376, 10, 10), GOLD_LINE, GOLD_LINE, 1);
	thoughtsLabel = std::make_shared<CLabel>(284, 381, FONT_SMALL, ETextAlignment::CENTERLEFT, Colors::WHITE, LIBRARY->generaltexth->translate("vcmi.lobby.aiSettings.thoughts"), 170);
	thoughtsArea = std::make_shared<LRClickableArea>(Rect(260, 370, 210, 22), [this]()
	{
		if(const auto * ps = seat())
			GAME->server().setPlayerAI(color, ps->aiAdventure, ps->aiBattle, ps->aiTier, !ps->aiThoughts);
	}, nullptr);

	// S301, S306: the same gold bar at 210 pixels for the longer words, centred on the OK button's row (368 to 400)
	auto reset = std::make_shared<CButton>(Point(20, 368), AnimationPath::builtin("SPRITES/RanShowButton210"), CButton::tooltip(), [this]() { GAME->server().setPlayerAI(color, "", "", -1, false); });
	reset->moveBy(Point(0, 16 - reset->pos.h / 2));
	reset->setOverlay(std::make_shared<CEngravedLabel>(0, 0, FONT_MEDIUM, ETextAlignment::CENTER, LIBRARY->generaltexth->translate("vcmi.lobby.aiSettings.reset")));
	buttons.push_back(reset);

	buttons.push_back(std::make_shared<CButton>(Point(pos.w - 20 - 64, 368), AnimationPath::builtin("MuBchck"), CButton::tooltip(), [this]() { close(); }, EShortcut::GLOBAL_RETURN));

	refresh();
	updateShadow();
	center();
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
	close();
}

void AISettingsWindow::refresh()
{
	const auto * ps = seat();
	if(!ps || !ps->isControlledByAI())
	{
		close();
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
