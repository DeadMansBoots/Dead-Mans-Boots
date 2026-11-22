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

	// S312: wider and taller than before, so the two gold bars carry their words in the lobby buttons' own lettering
	pos = Rect(0, 0, 600, 460);
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
	auto reset = std::make_shared<CButton>(Point(20, 408), AnimationPath::builtin("SPRITES/RanShowButtonSmall250"), CButton::tooltip(), [this]() { GAME->server().setPlayerAI(color, "", "", -1, false); });
	reset->moveBy(Point(0, 16 - reset->pos.h / 2));
	auto resetWords = std::make_shared<CEngravedLabel>(0, 0, FONT_BIG, ETextAlignment::CENTER, LIBRARY->generaltexth->translate("vcmi.lobby.aiSettings.reset"));
	resetWords->setLetteringCapitals(14);
	reset->setOverlay(resetWords);
	buttons.push_back(reset);

	buttons.push_back(std::make_shared<CButton>(Point(pos.w - 20 - 64, 408), AnimationPath::builtin("MuBchck"), CButton::tooltip(), [this]() { close(); }, EShortcut::GLOBAL_RETURN));

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
