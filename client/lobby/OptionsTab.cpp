/*
 * OptionsTab.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "OptionsTab.h"
#include "AISettingsWindow.h"

#include "CSelectionBase.h"

#include "../CServerHandler.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../gui/Shortcut.h"
#include "../gui/WindowHandler.h"
#include "../render/Graphics.h"
#include "../render/IFont.h"
#include "../render/IRenderHandler.h"
#include "../render/IImage.h"
#include "../media/ISoundPlayer.h"
#include "../widgets/CComponent.h"
#include "../widgets/ComboBox.h"
#include "../widgets/CTextInput.h"
#include "../widgets/Buttons.h"
#include "../widgets/Images.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/ObjectLists.h"
#include "../widgets/Slider.h"
#include "../widgets/TextControls.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../windows/GUIClasses.h"
#include "../windows/InfoWindows.h"
#include "../windows/CHeroOverview.h"
#include "../eventsSDL/InputHandler.h"

#include "../../lib/entities/faction/CFaction.h"
#include "../../lib/entities/faction/CTown.h"
#include "../../lib/entities/faction/CTownHandler.h"
#include "../../lib/entities/hero/CHeroHandler.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/entities/ResourceTypeHandler.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/json/JsonUtils.h"
#include "../../lib/constants/StringConstants.h"
#include "../../lib/mapping/CMapInfo.h"
#include "../../lib/mapping/CMapHeader.h"
#include "../../lib/GameLibrary.h"
#include "../gui/MenuColour.h"
#include "../../lib/DmbSeatColours.h"
#include "../render/Canvas.h"

namespace
{
/// S374 (K, November 19th: "the basegame "no handicap" text doesn't fit in their boxes...we should try and help them if we can"): the handicap
/// box is H3's own row art (ADOPRPNL and the other colours), 20 rows inside, and FONT_TINY's two lines 12 apart need 21 (the "p" of
/// "Handicap" sat on the box's bottom edge). FONT_TINY is the smallest font, the town column leaves no room to widen the box, and "No Handicap"
/// on one line is wider than it, so the lines are drawn closer, `pitch` apart. S398 (K, November 19th: "one pixel up, but the "no" line needs to
/// scoot down a line so they come together"): the first line one pixel lower than stock and the second one pixel higher. Same words, font and colour
class PitchedLabel : public CMultiLineLabel
{
	int pitch;
public:
	PitchedLabel(Rect position, EFonts font, const ColorRGBA & color, const std::string & text, int linePitch)
		: CMultiLineLabel(position, font, ETextAlignment::CENTER, color, text), pitch(linePitch)
	{
	}
	void showAll(Canvas & to) override
	{
		CIntObject::showAll(to);
		const auto & fontPtr = ENGINE->renderHandler().loadFont(font);
		CanvasClipRectGuard guard(to, pos);
		const std::vector<std::string> lines = getLines();
		// one line stays centred in the box as stock draws it; two or more start one pixel below the top, where stock's started once they filled the box
		Point lineStart = pos.topLeft() + Point(0, lines.size() == 1 ? (pos.h - fontPtr->getLineHeight()) / 2 : 1);
		for(const std::string & line : lines)
		{
			if(!line.empty())
				blitLine(to, Rect(lineStart, Point(pos.w, fontPtr->getLineHeight())), line);
			lineStart.y += pitch;
		}
	}
};
}

static JsonPath optionsTabConfigLocation()
{
	if(settings["general"]["enableUiEnhancements"].Bool())
		return JsonPath::builtin("config/widgets/playerOptionsTab.json");
	else
		return JsonPath::builtin("config/widgets/advancedOptionsTab.json");
}

OptionsTab::OptionsTab()
	: OptionsTabBase(optionsTabConfigLocation())
	, humanPlayers(0)
{
	acceptOptionPages("advancedOptions", "core.genrltxt.515");
}

void OptionsTab::closePickers(const char * why)
{
	for(auto picker : ENGINE->windows().findWindows<SelectionWindow>())
	{
		logGlobal->info("S466: the picker closes: %s", why);
		picker->closeWhenOnTop();
	}
}

void OptionsTab::recreate()
{
	ENGINE->stallDiag.lastLobbyUpdate = std::chrono::steady_clock::now(); // S466
	entries.clear();
	humanPlayers = 0;

	for (auto tooltipWindow : ENGINE->windows().findWindows<CPlayerOptionTooltipBox>())
		tooltipWindow->close();

	for (auto heroOverview : ENGINE->windows().findWindows<CHeroOverview>())
		heroOverview->close();

	// DMB (S002): an open AI Settings window shows the seat as the lobby state now holds it
	for(auto aiWindow : ENGINE->windows().findWindows<AISettingsWindow>())
		aiWindow->refresh();

	for (auto selectionWindow : ENGINE->windows().findWindows<SelectionWindow>())
		selectionWindow->reopen();

	// DMB: on the tab's own page when mods have added pages to it (acceptOptionPages)
	OBJECT_CONSTRUCTION_TARGETED(stockPageOrSelf());
	for(auto & pInfo : SEL->getStartInfo()->playerInfos)
	{
		if(pInfo.second.isControlledByHuman())
			humanPlayers++;

		entries.insert(std::make_pair(pInfo.first, std::make_shared<PlayerOptionsEntry>(pInfo.second, * this)));
	}

	OptionsTabBase::recreate();
}

size_t OptionsTab::CPlayerSettingsHelper::getImageIndex(bool big)
{
	enum EBonusSelection //frames of bonuses file
	{
		WOOD_ORE = 0,   CRYSTAL = 1,    GEM  = 2,
		MERCURY  = 3,   SULFUR  = 5,    GOLD = 8,
		ARTIFACT = 9,   RANDOM  = 10,
		WOOD = 0,       ORE     = 0,    MITHRIL = 10, // resources unavailable in bonuses file

		TOWN_RANDOM = 38,  TOWN_NONE = 39, // Special frames in ITPA
		HERO_RANDOM = 156, HERO_NONE = 157 // Special frames in PortraitsSmall
	};
	auto factionIndex = playerSettings.getCastleValidated();

	switch(selectionType)
	{
	case TOWN:
	{
		if (playerSettings.castle == FactionID::NONE)
			return TOWN_NONE;

		if (playerSettings.castle == FactionID::RANDOM)
			return TOWN_RANDOM;

		return (*LIBRARY->townh)[factionIndex]->town->clientInfo.icons[true][false] + (big ? 0 : 2);
	}

	case HERO:
	{
		if (playerSettings.hero == HeroTypeID::NONE)
			return HERO_NONE;

		if (playerSettings.hero == HeroTypeID::RANDOM)
			return HERO_RANDOM;

		if(playerSettings.heroPortrait != HeroTypeID::NONE)
			return playerSettings.heroPortrait;

		auto index = playerSettings.getHeroValidated();
		return (*LIBRARY->heroh)[index]->imageIndex;
	}

	case BONUS:
	{
		switch(playerSettings.bonus)
		{
		case PlayerStartingBonus::RANDOM:
			return RANDOM;
		case PlayerStartingBonus::NONE:
			return RANDOM; // DMB (S065): the bonuses file has no picture of its own for it; the name says what it is
		case PlayerStartingBonus::ARTIFACT:
			return ARTIFACT;
		case PlayerStartingBonus::GOLD:
			return GOLD;
		case PlayerStartingBonus::RESOURCE:
		{
			switch((*LIBRARY->townh)[factionIndex]->town->primaryRes.toEnum())
			{
			case EGameResID::WOOD_AND_ORE:
				return WOOD_ORE;
			case EGameResID::WOOD:
				return WOOD;
			case EGameResID::MERCURY:
				return MERCURY;
			case EGameResID::ORE:
				return ORE;
			case EGameResID::SULFUR:
				return SULFUR;
			case EGameResID::CRYSTAL:
				return CRYSTAL;
			case EGameResID::GEMS:
				return GEM;
			case EGameResID::GOLD:
				return GOLD;
			}
		}
		}
	}
	}
	return 0;
}

AnimationPath OptionsTab::CPlayerSettingsHelper::getImageName(bool big)
{
	switch(selectionType)
	{
	case OptionsTab::TOWN:
		return AnimationPath::builtin(big ? "ITPt": "ITPA");
	case OptionsTab::HERO:
		return AnimationPath::builtin(big ? "PortraitsLarge": "PortraitsSmall");
	case OptionsTab::BONUS:
		return AnimationPath::builtin("SCNRSTAR");
	}
	return {};
}

std::string OptionsTab::CPlayerSettingsHelper::getName()
{
	switch(selectionType)
	{
		case TOWN:
		{
			if (playerSettings.castle == FactionID::NONE)
				return LIBRARY->generaltexth->allTexts[523];

			if (playerSettings.castle == FactionID::RANDOM)
				return LIBRARY->generaltexth->allTexts[522];

			auto factionIndex = playerSettings.getCastleValidated();
			return (*LIBRARY->townh)[factionIndex]->getNameTranslated();
		}
		case HERO:
		{
			if (playerSettings.hero == HeroTypeID::NONE)
				return LIBRARY->generaltexth->allTexts[523];

			if (playerSettings.hero == HeroTypeID::RANDOM)
					return LIBRARY->generaltexth->allTexts[522];

			if(!playerSettings.heroNameTextId.empty())
				return LIBRARY->generaltexth->translate(playerSettings.heroNameTextId);
			auto index = playerSettings.getHeroValidated();
			return (*LIBRARY->heroh)[index]->getNameTranslated();
		}
		case BONUS:
		{
			if (playerSettings.bonus == PlayerStartingBonus::RANDOM)
					return LIBRARY->generaltexth->allTexts[522];

			return LIBRARY->generaltexth->arraytxt[214 + static_cast<int>(playerSettings.bonus)];
		}
	}
	return "";
}


std::string OptionsTab::CPlayerSettingsHelper::getTitle()
{
	switch(selectionType)
	{
	case OptionsTab::TOWN:
		return playerSettings.castle.isValid() ? LIBRARY->generaltexth->allTexts[80] : LIBRARY->generaltexth->allTexts[103];
	case OptionsTab::HERO:
		return playerSettings.hero.isValid() ? LIBRARY->generaltexth->allTexts[77] : LIBRARY->generaltexth->allTexts[101];
	case OptionsTab::BONUS:
	{
		switch(playerSettings.bonus)
		{
		case PlayerStartingBonus::RANDOM:
			return LIBRARY->generaltexth->allTexts[86]; //{Random Bonus}
		case PlayerStartingBonus::NONE:
			return LIBRARY->generaltexth->translate("vcmi.dmb.bonus.none"); // DMB (S065)
		case PlayerStartingBonus::ARTIFACT:
			return LIBRARY->generaltexth->allTexts[83]; //{Artifact Bonus}
		case PlayerStartingBonus::GOLD:
			return LIBRARY->generaltexth->allTexts[84]; //{Gold Bonus}
		case PlayerStartingBonus::RESOURCE:
			return LIBRARY->generaltexth->allTexts[85]; //{Resource Bonus}
		}
	}
	}
	return "";
}
std::string OptionsTab::CPlayerSettingsHelper::getSubtitle()
{
	auto factionIndex = playerSettings.getCastleValidated();
	auto heroIndex = playerSettings.getHeroValidated();

	switch(selectionType)
	{
	case TOWN:
		return getName();
	case HERO:
	{
		if(playerSettings.hero.isValid())
			return getName() + " - " + (*LIBRARY->heroh)[heroIndex]->heroClass->getNameTranslated();
		return getName();
	}

	case BONUS:
	{
		switch(playerSettings.bonus)
		{
		case PlayerStartingBonus::GOLD:
			return LIBRARY->generaltexth->allTexts[87]; //500-1000
		case PlayerStartingBonus::RESOURCE:
		{
			switch((*LIBRARY->townh)[factionIndex]->town->primaryRes.toEnum())
			{
			case EGameResID::MERCURY:
				return LIBRARY->generaltexth->allTexts[694];
			case EGameResID::SULFUR:
				return LIBRARY->generaltexth->allTexts[695];
			case EGameResID::CRYSTAL:
				return LIBRARY->generaltexth->allTexts[692];
			case EGameResID::GEMS:
				return LIBRARY->generaltexth->allTexts[693];
			case EGameResID::WOOD_AND_ORE:
				return LIBRARY->generaltexth->allTexts[89]; //At the start of the game, 5-10 wood and 5-10 ore are added to your Kingdom's resource pool
			}
		}
		}
	}
	}
	return "";
}

std::string OptionsTab::CPlayerSettingsHelper::getDescription()
{
	auto factionIndex = playerSettings.getCastleValidated();

	switch(selectionType)
	{
	case TOWN:
		return LIBRARY->generaltexth->allTexts[104];
	case HERO:
		return LIBRARY->generaltexth->allTexts[102];
	case BONUS:
	{
		switch(playerSettings.bonus)
		{
		case PlayerStartingBonus::RANDOM:
			return LIBRARY->generaltexth->allTexts[94]; //Gold, wood and ore, or an artifact is randomly chosen as your starting bonus
		case PlayerStartingBonus::NONE:
			return LIBRARY->generaltexth->translate("vcmi.dmb.bonus.none.help"); // DMB (S065)
		case PlayerStartingBonus::ARTIFACT:
			return LIBRARY->generaltexth->allTexts[90]; //An artifact is randomly chosen and equipped to your starting hero
		case PlayerStartingBonus::GOLD:
			return LIBRARY->generaltexth->allTexts[92]; //At the start of the game, 500-1000 gold is added to your Kingdom's resource pool
		case PlayerStartingBonus::RESOURCE:
		{
			switch((*LIBRARY->townh)[factionIndex]->town->primaryRes.toEnum())
			{
			case EGameResID::MERCURY:
				return LIBRARY->generaltexth->allTexts[690];
			case EGameResID::SULFUR:
				return LIBRARY->generaltexth->allTexts[691];
			case EGameResID::CRYSTAL:
				return LIBRARY->generaltexth->allTexts[688];
			case EGameResID::GEMS:
				return LIBRARY->generaltexth->allTexts[689];
			case EGameResID::WOOD_AND_ORE:
				return LIBRARY->generaltexth->allTexts[93]; //At the start of the game, 5-10 wood and 5-10 ore are added to your Kingdom's resource pool
			}
		}
		}
	}
	}
	return "";
}

OptionsTab::CPlayerOptionTooltipBox::CPlayerOptionTooltipBox(CPlayerSettingsHelper & helper)
	: CWindowObject(BORDERED | RCLICK_POPUP), CPlayerSettingsHelper(helper)
{
	OBJECT_CONSTRUCTION;

	switch(selectionType)
	{
		case TOWN:
			genTownWindow();
			break;
		case HERO:
			genHeroWindow();
			break;
		case BONUS:
			genBonusWindow();
			break;
	}

	center();
}

void OptionsTab::CPlayerOptionTooltipBox::genHeader()
{
	// S302 (K, November 17th: "I like having the player color being represented in the AI options for that seat. Let's make that uniform, and an
	// early test implementation of our forthcoming custom-ui configurability"): the seat's right-click box in its player's colour, with the setting
	// "Seat windows in the player's colour" (lobby.seatColourWindows, on by default); off, the stock brown
	if(settings["lobby"]["seatColourWindows"].Bool())
	{
		auto seatColoured = std::make_shared<FilledTexturePlayerColored>(Rect(0, 0, pos.w, pos.h));
		seatColoured->setPlayerColor(DmbSeatColours::shown(playerSettings.color)); // DMB (S424)
		backgroundTexture = seatColoured;
	}
	else
		backgroundTexture = std::make_shared<CFilledTexture>(ImagePath::builtin("DIBOXBCK"), pos);
	updateShadow();

	labelTitle = std::make_shared<CLabel>(pos.w / 2 + 8, 21, FONT_MEDIUM, ETextAlignment::CENTER, Colors::YELLOW, getTitle());
	labelSubTitle = std::make_shared<CLabel>(pos.w / 2, 88, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, getSubtitle());
	image = std::make_shared<CAnimImage>(getImageName(), getImageIndex(), 0, pos.w / 2 - 24, 45);
}

void OptionsTab::CPlayerOptionTooltipBox::genTownWindow()
{
	auto factionIndex = playerSettings.getCastleValidated();

	if (playerSettings.castle == FactionID::RANDOM)
		return genBonusWindow();

	pos = Rect(0, 0, 228, 290);
	genHeader();
	labelAssociatedCreatures = std::make_shared<CLabel>(pos.w / 2 + 8, 122, FONT_MEDIUM, ETextAlignment::CENTER, Colors::YELLOW, LIBRARY->generaltexth->allTexts[79]);
	std::vector<std::shared_ptr<CComponent>> components;
	const CTown * town = (*LIBRARY->townh)[factionIndex]->town.get();

	for(auto & elem : town->creatures)
	{
		if(!elem.empty())
			components.push_back(std::make_shared<CComponent>(ComponentType::CREATURE, elem.front(), std::nullopt, CComponent::tiny));
	}
	boxAssociatedCreatures = std::make_shared<CComponentBox>(components, Rect(10, 140, pos.w - 20, 140), 20, 10, 22, 4);
}

void OptionsTab::CPlayerOptionTooltipBox::genHeroWindow()
{
	auto heroIndex = playerSettings.getHeroValidated();

	if (playerSettings.hero == HeroTypeID::RANDOM)
		return genBonusWindow();

	pos = Rect(0, 0, 292, 226);
	genHeader();
	labelHeroSpeciality = std::make_shared<CLabel>(pos.w / 2 + 4, 117, FONT_MEDIUM, ETextAlignment::CENTER, Colors::YELLOW, LIBRARY->generaltexth->allTexts[78]);

	imageSpeciality = std::make_shared<CAnimImage>(AnimationPath::builtin("UN44"), (*LIBRARY->heroh)[heroIndex]->imageIndex, 0, pos.w / 2 - 22, 134);
	labelSpecialityName = std::make_shared<CLabel>(pos.w / 2, 188, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, (*LIBRARY->heroh)[heroIndex]->getSpecialtyNameTranslated());
}

void OptionsTab::CPlayerOptionTooltipBox::genBonusWindow()
{
	pos = Rect(0, 0, 228, 162);
	genHeader();

	textBonusDescription = std::make_shared<CTextBox>(getDescription(), Rect(10, 100, pos.w - 20, 70), 0, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE);
}

OptionsTab::SelectionWindow::SelectionWindow(const PlayerColor & color, SelType _type, int sliderPos)
	: CWindowObject(BORDERED), color(color)
{
	addUsedEvents(LCLICK | SHOW_POPUP);

	type = _type;

	loadFromSeat();
	recreate(sliderPos);
}

OptionsTab::SelectionWindow::~SelectionWindow()
{
	ENGINE->stallDiag.pickerWidgets = -1; // S466: no picker open
}

void OptionsTab::SelectionWindow::loadFromSeat()
{
	unusableHeroes.clear();
	allowedBonus.clear();
	const auto & seat = SEL->getStartInfo()->playerInfos.at(color); // callers check the seat is there (reopen, SelectedBox)
	initialFaction = seat.castle;
	initialHero = seat.hero;
	initialBonus = seat.bonus;
	selectedFaction = initialFaction;
	selectedHero = initialHero;
	selectedBonus = initialBonus;
	allowedFactions = SEL->getPlayerInfo(color).allowedFactions;
	allowedHeroes = SEL->getMapInfo()->mapHeader->allowedHeroes;

	for(auto & player : SEL->getStartInfo()->playerInfos)
	{
		if(player.first != color && (int)player.second.hero > HeroTypeID::RANDOM)
			unusableHeroes.insert(player.second.hero);
	}

	for (const auto & disposedHero : SEL->getMapInfo()->mapHeader->disposedHeroes)
	{
		if (!disposedHero.players.count(color))
			allowedHeroes.erase(disposedHero.heroId);
	}

	allowedBonus.push_back(PlayerStartingBonus::RANDOM);

	if(initialHero != HeroTypeID::NONE|| SEL->getPlayerInfo(color).heroesNames.size() > 0)
		allowedBonus.push_back(PlayerStartingBonus::ARTIFACT);

	allowedBonus.push_back(PlayerStartingBonus::GOLD);

	if(initialFaction.isValid())
		allowedBonus.push_back(PlayerStartingBonus::RESOURCE);
}

std::tuple<int, int> OptionsTab::SelectionWindow::calcLines(FactionID faction)
{
	int additionalItems = 1; // random

	if(!faction.isValid())
		return std::make_tuple(
			std::ceil(((double)allowedFactions.size() + additionalItems) / MAX_ELEM_PER_LINES),
			(allowedFactions.size() + additionalItems) % MAX_ELEM_PER_LINES
		);

	int count = 0;
	for(auto & elemh : allowedHeroes)
	{
		const CHero * type = elemh.toHeroType();
		if(type->heroClass->faction != faction)
			continue;

		count++;
	}

	return std::make_tuple(
		std::ceil(((double)count + additionalItems) / MAX_ELEM_PER_LINES),
		(count + additionalItems) % MAX_ELEM_PER_LINES
	);
}

void OptionsTab::SelectionWindow::apply()
{
	if(ENGINE->windows().isTopWindow(this))
	{
		ENGINE->input().hapticFeedback();
		ENGINE->sound().playSound(soundBase::button);

		close();

		logGlobal->info("S466: picker click: chosen, the picker closes");
		setSelection();
	}
	else
		logGlobal->info("S466: picker click ignored: %s is the top window", ENGINE->windows().topWindowName());
}

void OptionsTab::SelectionWindow::setSelection()
{
	if(selectedFaction != initialFaction)
		GAME->server().setPlayerOption(LobbyChangePlayerOption::TOWN_ID, selectedFaction, color);

	if(selectedHero != initialHero)
		GAME->server().setPlayerOption(LobbyChangePlayerOption::HERO_ID, selectedHero, color);

	if(selectedBonus != initialBonus)
		GAME->server().setPlayerOption(LobbyChangePlayerOption::BONUS_ID, static_cast<int>(selectedBonus), color);
}

void OptionsTab::SelectionWindow::reopen()
{
	// DMB (S426, K 11-21: "there's a lot of us getting frozen/unable to exit hero selection screens just now"): every lobby update rebuilds an
	// open picker from its seat; an update can take that seat away (fewer players, another map or template while the picker is open), and
	// reading the seat list past its end crashed or hung the client. The picker of a seat that is gone closes.
	// S448 (K 11-21: "when the issue happens, clicking even a hero does nothing"): stock closed the picker and pushed a new one on every
	// update. close() throws unless the picker is the top window (WindowBase::close), so an update that came while another window was over
	// the picker stopped halfway, and a click could be handled by a picker that had just been replaced. The picker now stays the same
	// window and is refreshed in place from its seat; one whose seat is gone (or no longer this player's) closes, at once when it is on
	// top, else at its next click
	const auto seat = SEL->getStartInfo()->playerInfos.find(color);
	const bool gone = seat == SEL->getStartInfo()->playerInfos.end() || (type == SelType::HERO && seat->second.castle == FactionID::RANDOM)
		|| !(GAME->server().isMyColor(color) || GAME->server().isHost());
	logGlobal->info("S448: the lobby changed with the %s picker of %s open: %s", type == SelType::TOWN ? "town" : (type == SelType::HERO ? "hero" : "bonus"),
		color.toString(), gone ? "its seat is gone, it closes" : "refreshed in place");
	if(gone)
	{
		closeWhenOnTop();
		return;
	}
	const int sliderPos = slider ? slider->getValue() : 0;
	loadFromSeat();
	recreate(sliderPos);
	redraw();
}

void OptionsTab::SelectionWindow::closeWhenOnTop()
{
	seatGone = true;
	if(ENGINE->windows().isTopWindow(this))
		close();
}

void OptionsTab::SelectionWindow::activate()
{
	CWindowObject::activate();
	// S448 (OmniAI Dev's gate, November 22nd: after the guest pressed OK on "this player slot no longer exists", the picker of the gone seat
	// stayed on the screen, live, until a click): a picker whose seat went while a window was over it closes when it is on top again. The
	// close waits for the window handler to finish the pop that made it the top window
	if(seatGone)
		ENGINE->dispatchMainThread([]()
		{
			auto top = ENGINE->windows().topWindow<OptionsTab::SelectionWindow>();
			if(top && top->seatGone)
			{
				logGlobal->info("S448: the picker of a gone seat (or one whose tab is gone, S466) is on top again: it closes");
				top->close();
			}
		});
}

bool OptionsTab::SelectionWindow::receiveEvent(const Point & position, int eventType) const
{
	if(eventType == AEventsReceiver::SHOW_POPUP)
		return true;
	return CWindowObject::receiveEvent(position, eventType);
}

void OptionsTab::SelectionWindow::recreate(int sliderPos)
{
	OBJECT_CONSTRUCTION;

	// S466: a build replaces the picker's portraits, names and frames; stock kept the old ones under the new (every scroll, and here every
	// lobby update since the in-place refresh), about 200 more widgets each time for a full hero list
	components.clear();

	int amountLines = 1;
	if(type == SelType::BONUS)
		elementsPerLine = allowedBonus.size();
	else
	{
		std::tie(amountLines, elementsPerLine) = calcLines((type > SelType::TOWN) ? selectedFaction : FactionID::RANDOM);
		if(amountLines > 1 || elementsPerLine == 0)
			elementsPerLine = MAX_ELEM_PER_LINES;
	}

	int x = (elementsPerLine) * (ICON_BIG_WIDTH-1);
	int y = (std::min(amountLines, MAX_LINES)) * (ICON_BIG_HEIGHT-1);

	int sliderWidth = ((amountLines > MAX_LINES) ? 16 : 0);

	pos = Rect(pos.x, pos.y, x + sliderWidth, y);
	backgroundTexture = std::make_shared<FilledTexturePlayerColored>(Rect(0, 0, pos.w - sliderWidth, pos.h));
	// S302: the seat's town, hero and bonus picker in its player's colour (lobby.seatColourWindows, on by default); off, the stock blue
	backgroundTexture->setPlayerColor(settings["lobby"]["seatColourWindows"].Bool() ? DmbSeatColours::shown(color) : menuColour()); // DMB (S424): the seat's picked colour
	updateShadow();

	if(type == SelType::TOWN)
		genContentFactions();
	if(type == SelType::HERO)
		genContentHeroes();
	if(type == SelType::BONUS)
		genContentBonus();
	genContentGrid(std::min(amountLines, MAX_LINES));

	if(!slider && amountLines > MAX_LINES)
	{
		slider = std::make_shared<CSlider>(Point(x, 0), y, std::bind(&OptionsTab::SelectionWindow::sliderMove, this, _1), MAX_LINES, amountLines, 0, Orientation::VERTICAL, CSlider::BLUE);
		slider->setPanningStep(ICON_BIG_HEIGHT);
		slider->setScrollBounds(Rect(-pos.w + slider->pos.w, 0, x + slider->pos.w, y));
		slider->scrollTo(sliderPos);
	}

	center();
	// S466: how many widgets the picker draws now, after each build (opening, a scroll, a lobby update)
	ENGINE->stallDiag.pickerWidgets = static_cast<int>(components.size());
	logGlobal->info("S466: the %s picker of %s built: %d widgets", type == SelType::TOWN ? "town" : (type == SelType::HERO ? "hero" : "bonus"),
		color.toString(), static_cast<int>(components.size()));
}

void OptionsTab::SelectionWindow::drawOutlinedText(int x, int y, ColorRGBA color, std::string text)
{
	components.push_back(std::make_shared<CLabel>(x-1, y, FONT_TINY, ETextAlignment::CENTER, Colors::BLACK, text, 56));
	components.push_back(std::make_shared<CLabel>(x+1, y, FONT_TINY, ETextAlignment::CENTER, Colors::BLACK, text, 56));
	components.push_back(std::make_shared<CLabel>(x, y-1, FONT_TINY, ETextAlignment::CENTER, Colors::BLACK, text, 56));
	components.push_back(std::make_shared<CLabel>(x, y+1, FONT_TINY, ETextAlignment::CENTER, Colors::BLACK, text, 56));
	components.push_back(std::make_shared<CLabel>(x, y, FONT_TINY, ETextAlignment::CENTER, color, text, 56));
}

void OptionsTab::SelectionWindow::genContentGrid(int lines)
{
	for(int y = 0; y < lines; y++)
	{
		for(int x = 0; x < elementsPerLine; x++)
		{
			components.push_back(std::make_shared<CPicture>(ImagePath::builtin("lobby/townBorderBig"), x * (ICON_BIG_WIDTH-1), y * (ICON_BIG_HEIGHT-1)));
		}
	}
}

void OptionsTab::SelectionWindow::genContentFactions()
{
	int i = 1;

	// random
	PlayerSettings set = PlayerSettings();
	set.castle = FactionID::RANDOM;
	CPlayerSettingsHelper helper = CPlayerSettingsHelper(set, SelType::TOWN);
	components.push_back(std::make_shared<CAnimImage>(helper.getImageName(), helper.getImageIndex(), 0, 6, (ICON_SMALL_HEIGHT/2)));
	drawOutlinedText(TEXT_POS_X, TEXT_POS_Y, (selectedFaction == FactionID::RANDOM) ? Colors::YELLOW : Colors::WHITE, helper.getName());
	if(selectedFaction == FactionID::RANDOM)
		components.push_back(std::make_shared<CPicture>(ImagePath::builtin("lobby/townBorderSmallActivated"), 6, (ICON_SMALL_HEIGHT/2)));

	factions.clear();
	for(auto & elem : allowedFactions)
	{
		int x = i % elementsPerLine;
		int y = (i / elementsPerLine) - (slider ? slider->getValue() : 0);

		PlayerSettings set = PlayerSettings();
		set.castle = elem;

		CPlayerSettingsHelper helper = CPlayerSettingsHelper(set, SelType::TOWN);

		factions.push_back(elem);
		i++;

		if(y < 0 || y > MAX_LINES - 1)
			continue;
			
		components.push_back(std::make_shared<CAnimImage>(helper.getImageName(true), helper.getImageIndex(true), 0, x * (ICON_BIG_WIDTH-1), y * (ICON_BIG_HEIGHT-1)));
		components.push_back(std::make_shared<CPicture>(ImagePath::builtin(selectedFaction == elem ? "lobby/townBorderBigActivated" : "lobby/townBorderBig"), x * (ICON_BIG_WIDTH-1), y * (ICON_BIG_HEIGHT-1)));
		drawOutlinedText(x * (ICON_BIG_WIDTH-1) + TEXT_POS_X, y * (ICON_BIG_HEIGHT-1) + TEXT_POS_Y, (selectedFaction == elem) ? Colors::YELLOW : Colors::WHITE, helper.getName());
	}
}

void OptionsTab::SelectionWindow::genContentHeroes()
{
	int i = 1;

	// random
	PlayerSettings set = PlayerSettings();
	set.hero = HeroTypeID::RANDOM;
	CPlayerSettingsHelper helper = CPlayerSettingsHelper(set, SelType::HERO);
	components.push_back(std::make_shared<CAnimImage>(helper.getImageName(), helper.getImageIndex(), 0, 6, (ICON_SMALL_HEIGHT/2)));
	drawOutlinedText(TEXT_POS_X, TEXT_POS_Y, (selectedHero == HeroTypeID::RANDOM) ? Colors::YELLOW : Colors::WHITE, helper.getName());
	if(selectedHero == HeroTypeID::RANDOM)
		components.push_back(std::make_shared<CPicture>(ImagePath::builtin("lobby/townBorderSmallActivated"), 6, (ICON_SMALL_HEIGHT/2)));

	heroes.clear();
	for(auto & elem : allowedHeroes)
	{
		const CHero * type = elem.toHeroType();

		if(type->heroClass->faction != selectedFaction)
			continue;

		int x = i % elementsPerLine;
		int y = (i / elementsPerLine) - (slider ? slider->getValue() : 0);

		PlayerSettings set = PlayerSettings();
		set.hero = elem;

		CPlayerSettingsHelper helper = CPlayerSettingsHelper(set, SelType::HERO);

		heroes.push_back(elem);
		i++;

		if(y < 0 || y > MAX_LINES - 1)
			continue;

		components.push_back(std::make_shared<CAnimImage>(helper.getImageName(true), helper.getImageIndex(true), 0, x * (ICON_BIG_WIDTH-1), y * (ICON_BIG_HEIGHT-1)));
		drawOutlinedText(x * (ICON_BIG_WIDTH-1) + TEXT_POS_X, y * (ICON_BIG_HEIGHT-1) + TEXT_POS_Y, (selectedHero == elem) ? Colors::YELLOW : Colors::WHITE, helper.getName());
		ImagePath image = ImagePath::builtin("lobby/townBorderBig");
		if(selectedHero == elem)
			image = ImagePath::builtin("lobby/townBorderBigActivated");
		if(unusableHeroes.count(elem))
			image = ImagePath::builtin("lobby/townBorderBigGrayedOut");
		components.push_back(std::make_shared<CPicture>(image, x * (ICON_BIG_WIDTH-1), y * (ICON_BIG_HEIGHT-1)));
	}
}

void OptionsTab::SelectionWindow::genContentBonus()
{
	PlayerSettings set = SEL->getStartInfo()->playerInfos.find(color)->second;

	int i = 0;
	for(auto elem : allowedBonus)
	{
		int x = i;
		int y = 0;

		set.bonus = elem;
		CPlayerSettingsHelper helper = CPlayerSettingsHelper(set, SelType::BONUS);
		components.push_back(std::make_shared<CAnimImage>(helper.getImageName(), helper.getImageIndex(), 0, x * (ICON_BIG_WIDTH-1) + 6, y * (ICON_BIG_HEIGHT-1) + (ICON_SMALL_HEIGHT/2)));
		drawOutlinedText(x * (ICON_BIG_WIDTH-1) + TEXT_POS_X, y * (ICON_BIG_HEIGHT-1) + TEXT_POS_Y, Colors::WHITE , helper.getName());
		if(selectedBonus == elem)
		{
			components.push_back(std::make_shared<CPicture>(ImagePath::builtin("lobby/townBorderSmallActivated"), x * (ICON_BIG_WIDTH-1) + 6, y * (ICON_BIG_HEIGHT-1) + (ICON_SMALL_HEIGHT/2)));
			drawOutlinedText(x * (ICON_BIG_WIDTH-1) + TEXT_POS_X, y * (ICON_BIG_HEIGHT-1) + TEXT_POS_Y, Colors::YELLOW , helper.getName());
		}

		i++;
	}
}

int OptionsTab::SelectionWindow::getElement(const Point & cursorPosition)
{
	int x = (cursorPosition.x - pos.x) / (ICON_BIG_WIDTH-1);
	int y = (cursorPosition.y - pos.y) / (ICON_BIG_HEIGHT-1) + (slider ? slider->getValue() : 0);

	return x + y * elementsPerLine;
}

void OptionsTab::SelectionWindow::setElement(int elem, bool doApply)
{
	PlayerSettings set = PlayerSettings();
	if(type == SelType::TOWN)
	{
		if(elem > 0)
		{
			elem--;
			if(elem >= factions.size())
				return;
			set.castle = factions[elem];
		}
		else
		{
			set.castle = FactionID::RANDOM;
		}
		if(set.castle != FactionID::NONE)
		{
			if(!doApply)
			{
				CPlayerSettingsHelper helper = CPlayerSettingsHelper(set, SelType::TOWN);
				ENGINE->windows().createAndPushWindow<CPlayerOptionTooltipBox>(helper);
			}
			else
				selectedFaction = set.castle;
		}
	}
	if(type == SelType::HERO)
	{
		if(elem > 0)
		{
			elem--;
			if(elem >= heroes.size())
			{
				if(doApply)
					logGlobal->info("S466: picker click on an empty cell (element %d of %d heroes): nothing happens", elem + 1, static_cast<int>(heroes.size()));
				return;
			}
			set.hero = heroes[elem];
		}
		else
		{
			set.hero = HeroTypeID::RANDOM;
		}

		// S466: only a hero is checked against the other seats' picks; stock checked heroes[elem] for Random too, which with elem 0 is the
		// faction's first hero, so Random did nothing while another seat held that hero (and read past an empty list)
		if(doApply && set.hero != HeroTypeID::RANDOM && unusableHeroes.count(set.hero))
		{
			logGlobal->info("S466: picker click on %s ignored: another seat's hero", set.hero.toHeroType()->getJsonKey());
			return;
		}

		if(set.hero != HeroTypeID::NONE)
		{
			if(!doApply)
			{
				CPlayerSettingsHelper helper = CPlayerSettingsHelper(set, SelType::HERO);
				if(settings["general"]["enableUiEnhancements"].Bool() && helper.playerSettings.hero.isValid() && helper.playerSettings.heroNameTextId.empty())
					ENGINE->windows().createAndPushWindow<CHeroOverview>(helper.playerSettings.hero);
				else
					ENGINE->windows().createAndPushWindow<CPlayerOptionTooltipBox>(helper);
			}
			else
				selectedHero = set.hero;
		}
	}
	if(type == SelType::BONUS)
	{
		if(elem >= 4)
			return;
		set.bonus = static_cast<PlayerStartingBonus>(allowedBonus[elem]);

		if(!doApply)
		{
			CPlayerSettingsHelper helper = CPlayerSettingsHelper(set, SelType::BONUS);
			ENGINE->windows().createAndPushWindow<CPlayerOptionTooltipBox>(helper);
		}
		else
			selectedBonus = set.bonus;
	}

	if(doApply)
		apply();
}

void OptionsTab::SelectionWindow::sliderMove(int slidPos)
{
	if(!slider)
		return; // ignore spurious call when slider is being created
	recreate();
	redraw();
}

void OptionsTab::SelectionWindow::notFocusedClick()
{
	logGlobal->info("S448: a click outside the picker closes it, the choice unchanged");
	close();
}

void OptionsTab::SelectionWindow::clickReleased(const Point & cursorPosition)
{
	if(seatGone)
	{
		closeWhenOnTop();
		return;
	}
	if(slider && slider->pos.isInside(cursorPosition))
		return;

	int elem = getElement(cursorPosition);

	setElement(elem, true);
}

void OptionsTab::SelectionWindow::showPopupWindow(const Point & cursorPosition)
{
	// S448 (K: "should be able to right-click, click outside of the box, click random, or click a hero to close it"): a right click outside
	// the picker closes it with the choice unchanged; inside, it shows the hero's or town's details as stock
	if(seatGone || !pos.isInside(cursorPosition))
	{
		logGlobal->info("S448: a right click outside the picker closes it, the choice unchanged");
		closeWhenOnTop();
		return;
	}
	if(slider && slider->pos.isInside(cursorPosition))
		return;

	int elem = getElement(cursorPosition);

	setElement(elem, false);
}

OptionsTab::HandicapWindow::HandicapWindow()
	: CWindowObject(BORDERED)
{
	OBJECT_CONSTRUCTION;

	addUsedEvents(LCLICK);

	enum Columns : int32_t
	{
		PERCENT = 900,
		INCOME = 1000,
		GROWTH = 2000,
		SEES_MAP = 3000,
	};
	// TODO: configurable resources
	// DMB (S376 C, K 11-19: "change it from "handicap" to starting resources, since it seems this covers the starting resources of all players (and
	// adjusts per difficulty for players and AI seats)"; S398: "the option for the players to add more by percent (probably should be a typeable
	// field)"): the window shows what each seat starts with (its difficulty row, S375, plus the stock bonus) and a typeable percent of that row;
	// what is stored is still stock's bonus, the typed amount less the row, so no new field travels
	auto columns = std::vector<int>{EGameResID::GOLD, EGameResID::WOOD, EGameResID::MERCURY, EGameResID::ORE, EGameResID::SULFUR, EGameResID::CRYSTAL, EGameResID::GEMS, Columns::PERCENT, Columns::INCOME, Columns::GROWTH, Columns::SEES_MAP};
	auto columnX = [](int j){ return 30 + j * 66 + (j > 0 ? 10 : 0); }; // Gold field is larger; 66 apart so eleven columns fit 800 pixels

	pos = Rect(0, 0, columnX(columns.size() - 1) + 60, 100 + SEL->getStartInfo()->playerInfos.size() * 30);

	backgroundTexture = std::make_shared<FilledTexturePlayerColored>(pos);
	backgroundTexture->setPlayerColor(menuColour());

	labels.push_back(std::make_shared<CLabel>(pos.w / 2 + 8, 15, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, LIBRARY->generaltexth->translate("vcmi.dmb.lobby.startingResources")));
	const int gameDifficulty = std::clamp<int>(SEL->getStartInfo()->difficulty, 0, 4);
	labels.push_back(std::make_shared<CLabel>(pos.w - 14, 17, FONT_SMALL, ETextAlignment::CENTERRIGHT, Colors::WHITE,
		LIBRARY->generaltexth->translate("vcmi.dmb.lobby.startingResources.at") + " " + LIBRARY->generaltexth->arraytxt[142 + gameDifficulty]));

	int i = 0;
	for(auto & pInfo : SEL->getStartInfo()->playerInfos)
	{
		PlayerColor player = pInfo.first;
		const PlayerSettings &ps = SEL->getStartInfo()->getIthPlayersSettings(player);
		const TResources row = PlayerSettings::difficultyResources(!ps.isControlledByAI(), ps.startingTier(!ps.isControlledByAI(), gameDifficulty));
		startingRows[player] = row;
		anim.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("ITGFLAGS"), DmbSeatColours::shown(player).getNum(), 0, 7, 57 + i * 30));
		for(int j = 0; j < columns.size(); j++)
		{
			bool isPercent = int(columns[j]) == Columns::PERCENT;
			bool isIncome = int(columns[j]) == Columns::INCOME;
			bool isGrowth = int(columns[j]) == Columns::GROWTH;
			EGameResID resource = columns[j];

			int xPos = columnX(j);

			if(int(columns[j]) == Columns::SEES_MAP)
			{
				if(i == 0)
					labels.push_back(std::make_shared<CLabel>(xPos, 38, FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, LIBRARY->generaltexth->translate("vcmi.dmb.lobby.startingResources.seesMap")));
				// S332: a computer seat played by Nullkiller has the map cheat; the tick shows what it comes to and the host can set it
				const std::string ai = ps.isControlledByAI() ? AISettingsWindow::resolvedAdventure(ps) : std::string();
				const std::string settingsFile = ai == "Nullkiller" ? "config/ai/nkai/nkai-settings" : (ai == "Nullkiller2" ? "config/ai/nk2ai/nk2ai-settings" : "");
				const Rect box(xPos + 10, 59 + i * 30, 18, 18);
				if(settingsFile.empty())
				{
					labels.push_back(std::make_shared<CLabel>(box.center().x, box.center().y, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, "-"));
					continue;
				}
				const JsonNode nk = JsonUtils::assembleFromFiles(settingsFile);
				const JsonNode & tierNode = nk[GameConstants::DIFFICULTY_NAMES[AISettingsWindow::effectiveTier(ps)]];
				seesMapDefault[player] = SEL->getStartInfo()->extraOptionsInfo.cheatsAllowed && !AISettingsWindow::alliedToHuman(ps)
					&& (tierNode["openMap"].Bool() || tierNode["allowObjectGraph"].Bool());
				seesMap[player] = ps.handicap.seesMap;
				const ColorRGBA gold(212, 175, 55, 255);
				seesMapParts.push_back(std::make_shared<TransparentFilledRectangle>(box, ColorRGBA(0, 0, 0, 128), gold, 1));
				seesMapTicks[player] = std::make_shared<TransparentFilledRectangle>(Rect(box.x + 4, box.y + 4, 10, 10), gold, gold, 1);
				seesMapParts.push_back(std::make_shared<LRClickableArea>(box.resize(4), [this, player](){
					// a click sets the opposite of what the tick shows; back to what the AI decides when that is the same again
					const bool shown = seesMap[player] >= 0 ? seesMap[player] == 1 : seesMapDefault[player];
					seesMap[player] = (!shown == seesMapDefault[player]) ? -1 : (shown ? 0 : 1);
					showSeesMap(player);
				}, [](){ CRClickPopup::createAndPush(LIBRARY->generaltexth->translate("vcmi.dmb.lobby.startingResources.seesMapHelp")); }));
				showSeesMap(player);
				continue;
			}

			if(i == 0)
			{
				if(isPercent)
					labels.push_back(std::make_shared<CLabel>(xPos, 38, FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, LIBRARY->generaltexth->translate("vcmi.dmb.lobby.startingResources.percent")));
				else if(isIncome)
					labels.push_back(std::make_shared<CLabel>(xPos, 38, FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, LIBRARY->generaltexth->translate("core.jktext.32")));
				else if(isGrowth)
					labels.push_back(std::make_shared<CLabel>(xPos, 38, FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, LIBRARY->generaltexth->translate("core.genrltxt.194")));
				else
					anim.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("SMALRES"), GameResID(resource).getNum(), 0, 15 + xPos + (j == 0 ? 10 : 0), 35));
			}

			auto area = Rect(xPos, 60 + i * 30, j == 0 ? 60 : 50, 16);
			textinputbackgrounds.push_back(std::make_shared<TransparentFilledRectangle>(area.resize(3), ColorRGBA(0,0,0,128), ColorRGBA(64,64,64,64)));
			auto input = std::make_shared<CTextInput>(area, FONT_SMALL, ETextAlignment::CENTERLEFT, true);
			if(isPercent)
			{
				percentInputs[player] = input;
				input->setText(std::to_string(row[EGameResID::GOLD] > 0 ? std::max<int>(0, (row[EGameResID::GOLD] + ps.handicap.startBonus[EGameResID::GOLD]) * 100 / row[EGameResID::GOLD]) : 100));
				// the percent sets the seven amounts to that share of the seat's row; the amounts are what is kept
				input->setCallback([this, player](const std::string & s){
					std::string tmp = s;
					tmp.erase(std::remove_if(tmp.begin(), tmp.end(), [](char c) { return !isdigit(c); }), tmp.end());
					tmp = tmp.substr(0, 3);
					const int percent = tmp.empty() ? 0 : std::stoi(tmp);
					percentInputs[player]->setText(std::to_string(percent));
					for(auto & field : textinputs[player])
						if(int(field.first) < Columns::PERCENT)
							field.second->setText(std::to_string(static_cast<int64_t>(startingRows[player][field.first]) * percent / 100));
				});
				input->setPopupCallback([](){ CRClickPopup::createAndPush(LIBRARY->generaltexth->translate("vcmi.dmb.lobby.startingResources.percentHelp")); });
				labels.push_back(std::make_shared<CLabel>(area.topRight().x, area.center().y, FONT_SMALL, ETextAlignment::CENTERRIGHT, Colors::WHITE, "%"));
				continue;
			}
			textinputs[player][resource] = input;
			const bool isAmount = !isIncome && !isGrowth;
			input->setText(std::to_string(isIncome ? ps.handicap.percentIncome : (isGrowth ? ps.handicap.percentGrowth : std::max<int>(0, row[resource] + ps.handicap.startBonus[resource]))));
			input->setCallback([this, player, resource, isIncome, isGrowth, isAmount](const std::string & s){
				// text input processing: remove non digits; cut length; fill empty field with 0 (an amount is what the seat starts with, never below 0)
				std::string tmp = s;
				tmp.erase(std::remove_if(tmp.begin(), tmp.end(), [](char c) { return !isdigit(c); }), tmp.end());
				int maxLength = isIncome || isGrowth ? 3 : (resource == EGameResID::GOLD ? 6 : 5);
				tmp = tmp.substr(0, maxLength);
				const int value = tmp.empty() ? 0 : std::stoi(tmp);
				textinputs[player][resource]->setText(std::to_string(value));
				// a typed gold amount moves the seat's percent with it
				if(isAmount && resource == EGameResID::GOLD && startingRows[player][EGameResID::GOLD] > 0)
					percentInputs[player]->setText(std::to_string(static_cast<int64_t>(value) * 100 / startingRows[player][EGameResID::GOLD]));
			});
			input->setPopupCallback([isIncome, isGrowth](){
				// Help for the textinputs
				if(isIncome)
					CRClickPopup::createAndPush(LIBRARY->generaltexth->translate("vcmi.lobby.handicap.income"));
				else if(isGrowth)
					CRClickPopup::createAndPush(LIBRARY->generaltexth->translate("vcmi.lobby.handicap.growth"));
				else
					CRClickPopup::createAndPush(LIBRARY->generaltexth->translate("vcmi.dmb.lobby.startingResources.resourceHelp"));
			});
			if(isIncome || isGrowth)
				labels.push_back(std::make_shared<CLabel>(area.topRight().x, area.center().y, FONT_SMALL, ETextAlignment::CENTERRIGHT, Colors::WHITE, "%"));
		}
		i++;
	}

	buttons.push_back(std::make_shared<CButton>(Point(pos.w / 2 - 32, 60 + SEL->getStartInfo()->playerInfos.size() * 30), AnimationPath::builtin("MuBchck"), CButton::tooltip(), [this](){
		for (const auto& player : textinputs)
		{
			TResources resources = TResources();
			int income = 100;
			int growth = 100;
			for (const auto& resource : player.second)
			{
				bool isIncome = int(resource.first) == Columns::INCOME;
				bool isGrowth = int(resource.first) == Columns::GROWTH;
				if(isIncome)
					income = std::stoi(resource.second->getText());
				else if(isGrowth)
					growth = std::stoi(resource.second->getText());
				else // S376 C: the typed amount less the seat's row is stock's bonus
					resources[resource.first] = std::stoi(resource.second->getText()) - startingRows[player.first][resource.first];
			}
			const auto choice = seesMap.find(player.first);
			// S380: the seat's other fields (its bonuses, set in Seat Settings) are kept as they are
			Handicap handicap = SEL->getStartInfo()->getIthPlayersSettings(player.first).handicap;
			handicap.startBonus = resources;
			handicap.percentIncome = income;
			handicap.percentGrowth = growth;
			handicap.seesMap = choice != seesMap.end() ? choice->second : int8_t(-1);
			GAME->server().setPlayerHandicap(player.first, handicap);
		}
	    		
		close();
	}, EShortcut::GLOBAL_RETURN));

	updateShadow();
	center();
}

void OptionsTab::HandicapWindow::showSeesMap(PlayerColor player)
{
	const bool shown = seesMap[player] >= 0 ? seesMap[player] == 1 : seesMapDefault[player];
	seesMapTicks[player]->setEnabled(shown);
	redraw();
}

void OptionsTab::HandicapWindow::notFocusedClick()
{
	close();
}

OptionsTab::SelectedBox::SelectedBox(Point position, PlayerSettings & playerSettings, SelType type)
	: Scrollable(LCLICK | SHOW_POPUP, position, Orientation::HORIZONTAL)
	, CPlayerSettingsHelper(playerSettings, type)
{
	OBJECT_CONSTRUCTION;

	image = std::make_shared<CAnimImage>(getImageName(), getImageIndex());
	subtitle = std::make_shared<CLabel>(24, 39, FONT_TINY, ETextAlignment::CENTER, Colors::WHITE, getName(), 71);

	pos = image->pos;

	setPanningStep(pos.w);
}

void OptionsTab::SelectedBox::update()
{
	image->setFrame(getImageIndex());
	subtitle->setText(getName());
}

void OptionsTab::SelectedBox::showPopupWindow(const Point & cursorPosition)
{
	// cases when we do not need to display a message
	if(playerSettings.castle == FactionID::NONE && CPlayerSettingsHelper::selectionType == TOWN)
		return;
	if(playerSettings.hero == HeroTypeID::NONE && !SEL->getPlayerInfo(playerSettings.color).hasCustomMainHero() && CPlayerSettingsHelper::selectionType == HERO)
		return;

	if(settings["general"]["enableUiEnhancements"].Bool() && CPlayerSettingsHelper::selectionType == HERO && playerSettings.hero.isValid() && playerSettings.heroNameTextId.empty())
		ENGINE->windows().createAndPushWindow<CHeroOverview>(playerSettings.hero);
	else
		ENGINE->windows().createAndPushWindow<CPlayerOptionTooltipBox>(*this);
}

void OptionsTab::SelectedBox::clickReleased(const Point & cursorPosition)
{
	if (!SEL)
		return;

	if(SEL->screenType != ESelectionScreen::newGame)
		return;
	
	PlayerInfo pi = SEL->getPlayerInfo(playerSettings.color);
	const bool foreignPlayer = GAME->server().isGuest() && !GAME->server().isMyColor(playerSettings.color);

	if(selectionType == SelType::TOWN && ((pi.allowedFactions.size() < 2 && !pi.isFactionRandom) || foreignPlayer))
		return;

	if(selectionType == SelType::HERO && ((pi.defaultHero() == HeroTypeID::NONE || !playerSettings.castle.isValid() || foreignPlayer)))
		return;

	if(selectionType == SelType::BONUS && foreignPlayer)
		return;

	ENGINE->input().hapticFeedback();
	ENGINE->windows().createAndPushWindow<SelectionWindow>(playerSettings.color, selectionType);
}

void OptionsTab::SelectedBox::scrollBy(int distance)
{
	// FIXME: currently options tab is completely recreacted from scratch whenever we receive any information from server
	// because of that, panning event gets interrupted (due to destruction of element)
	// so, currently, gesture will always move selection only by 1, and then wait for recreation from server info
	distance = std::clamp(distance, -1, 1);

	switch(CPlayerSettingsHelper::selectionType)
	{
		case TOWN:
			GAME->server().setPlayerOption(LobbyChangePlayerOption::TOWN, distance, playerSettings.color);
			break;
		case HERO:
			GAME->server().setPlayerOption(LobbyChangePlayerOption::HERO, distance, playerSettings.color);
			break;
		case BONUS:
			GAME->server().setPlayerOption(LobbyChangePlayerOption::BONUS, distance, playerSettings.color);
			break;
	}

	setScrollingEnabled(false);
}

OptionsTab::PlayerOptionsEntry::PlayerOptionsEntry(const PlayerSettings & S, const OptionsTab & parent)
	: CIntObject(LCLICK | KEYBOARD | TEXTINPUT)
	, pi(std::make_unique<PlayerInfo>(SEL->getPlayerInfo(S.color)))
	, s(std::make_unique<PlayerSettings>(S))
	, parentTab(parent)
	, name(S.name)
{
	OBJECT_CONSTRUCTION;

	int serial = 0;
	for(PlayerColor g = PlayerColor(0); g < s->color; ++g)
	{
		auto itred = SEL->getPlayerInfo(g);
		if(itred.canComputerPlay || itred.canHumanPlay)
			serial++;
	}

	pos.x += 54;
	pos.y += 128 + serial * 50;

	assert(GAME->server().mi && GAME->server().mi->mapHeader);
	const PlayerInfo & p = SEL->getPlayerInfo(s->color);
	assert(p.canComputerPlay || p.canHumanPlay); //someone must be able to control this player
	if(p.canHumanPlay && p.canComputerPlay)
		whoCanPlay = HUMAN_OR_CPU;
	else if(p.canComputerPlay)
		whoCanPlay = CPU;
	else
		whoCanPlay = HUMAN;

	static const std::array<std::string, PlayerColor::PLAYER_LIMIT_I> flags =
	{{
		"AOFLGBR.DEF", "AOFLGBB.DEF", "AOFLGBY.DEF", "AOFLGBG.DEF",
		"AOFLGBO.DEF", "AOFLGBP.DEF", "AOFLGBT.DEF", "AOFLGBS.DEF"
	}};
	static const std::array<std::string, PlayerColor::PLAYER_LIMIT_I> bgs =
	{{
		"ADOPRPNL.bmp", "ADOPBPNL.bmp", "ADOPYPNL.bmp", "ADOPGPNL.bmp",
		"ADOPOPNL.bmp", "ADOPPPNL.bmp", "ADOPTPNL.bmp", "ADOPSPNL.bmp"
	}};

	background = std::make_shared<CPicture>(ImagePath::builtin(bgs[DmbSeatColours::shown(s->color).getNum()]), 0, 0);
	if(s->isControlledByAI() || GAME->server().isGuest())
	{
		// DMB (S002, K: "a configure AI button where it says computer ... the button text should reflect the currently selected AI"): a computer seat names its AI, with its tier
		// (the seat's own, or the map's) as a chess piece in the margin beside the row; the host clicks the name for the seat's AI Settings window
		const bool computerSeat = s->isControlledByAI();
		labelPlayerName = std::make_shared<CLabel>(55, 10, EFonts::FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, computerSeat ? AISettingsWindow::displayName(AISettingsWindow::resolvedAdventure(*s)) : name, 95);
		if(computerSeat)
		{
			// the margin beside the row holds the flag of a seat a human may take; a seat only the computer plays has none, and shows the tier there
			if(!pi->canHumanPlay)
			{
				static const std::array<std::string, 5> tierIcons = {{"GSPBUT3.DEF", "GSPBUT4.DEF", "GSPBUT5.DEF", "GSPBUT6.DEF", "GSPBUT7.DEF"}};
				aiTierIcon = std::make_shared<CAnimImage>(AnimationPath::builtin(tierIcons[AISettingsWindow::effectiveTier(*s)]), 0, 0, -43, 0);
				// S331 (K, November 19th: "the difficulty per seat buttons for ai don't line up with the seat rows"): centred on the row from the
				// picture's own height; a fixed 8 pixels down hung the piece's frame past the row's bottom into the next seat
				aiTierIcon->moveBy(Point(0, (background->pos.h - aiTierIcon->pos.h) / 2));
				// S470 (K, November 22nd, picture k_lobby_difficulty_buttons_center_1122.png: "we should center the chess-piece/difficulty buttons"):
				// on the flag column, the piece's centre on the centre of the flag a human seat shows there (the flag is the wider picture, so a
				// piece drawn from the flag's left edge stood about 5 pixels left of the column)
				if(const auto flagImage = ENGINE->renderHandler().loadImage(AnimationPath::builtin(flags[0]), 0, 0, EImageBlitMode::COLORKEY))
					aiTierIcon->moveBy(Point((flagImage->width() - aiTierIcon->pos.w) / 2, 0));
			}
			const PlayerColor seatColor = s->color;
			auto openSeat = [seatColor]()
			{
				if(GAME->server().isHost())
					ENGINE->windows().createAndPushWindow<AISettingsWindow>(seatColor);
			};
			auto seatHelp = [](){
				CRClickPopup::createAndPush(LIBRARY->generaltexth->translate("vcmi.lobby.aiSettings.seatHelp"));
			};
			aiSeatArea = std::make_shared<LRClickableArea>(Rect(6, 3, 95, 15), openSeat, seatHelp);
			// S332: the red area is the name, the "CPU" cell below it and the piece in the margin
			aiTypeArea = std::make_shared<LRClickableArea>(Rect(6, 21, 45, 26), openSeat, seatHelp);
			if(aiTierIcon)
			{
				const Rect piece(aiTierIcon->pos.topLeft() - pos.topLeft(), aiTierIcon->pos.dimensions());
				aiPieceArea = std::make_shared<LRClickableArea>(piece, openSeat, seatHelp);
				const bool custom = s->handicap.isCustom();
				if(custom)
				{
					customTile = std::make_shared<TransparentFilledRectangle>(piece.resize(-2), ColorRGBA(24, 40, 112, 255), ColorRGBA(150, 160, 210, 255));
					customGear = std::make_shared<CPicture>(ImagePath::builtin("settingsWindow/gear"), 0, 0);
					customGear->moveTo(aiTierIcon->pos.center() - Point(customGear->pos.w / 2, customGear->pos.h / 2));
				}
			}
		}
	}
	else
		labelPlayerNameEdit = std::make_shared<CTextInputWithConfirm>(Rect(6, 3, 95, 15), EFonts::FONT_SMALL, ETextAlignment::CENTER, name, false, [this](){ updateName(); });

	labelWhoCanPlay = std::make_shared<CMultiLineLabel>(Rect(6, 21, 45, 26), EFonts::FONT_TINY, ETextAlignment::CENTER, Colors::WHITE, LIBRARY->generaltexth->arraytxt[206 + whoCanPlay]);
	// S424 (with S332's red area): a human seat's "Human or CPU" cell opens its Seat Settings (the Seat Color list), for the host
	if(!s->isControlledByAI() && !aiTypeArea)
	{
		const PlayerColor seatColor = s->color;
		aiTypeArea = std::make_shared<LRClickableArea>(Rect(6, 21, 45, 26), [seatColor]()
		{
			if(GAME->server().isHost())
				ENGINE->windows().createAndPushWindow<AISettingsWindow>(seatColor);
		}, [](){ CRClickPopup::createAndPush(LIBRARY->generaltexth->translate("vcmi.dmb.lobby.seatColour.help")); });
	}

	auto hasHandicap = [this](){ return s->handicap.startBonus.empty() && s->handicap.percentIncome == 100 && s->handicap.percentGrowth == 100; };
	// S415 (K, November 21st: "Say "Starting" over "Resources" now."): the button names what its window sets, in both states; the window
	// behind it is still stock's Handicap window, and its right-click text still lists a changed seat's handicap
	std::string labelHandicapText = LIBRARY->generaltexth->translate("vcmi.dmb.lobby.startingResources");
	// S374, S398: the two lines 2 pixels closer than the font's line height (the first one down, the second one up), so both fit inside the box
	labelHandicap = std::make_shared<PitchedLabel>(Rect(55, 23, 46, 24), EFonts::FONT_TINY, Colors::WHITE, labelHandicapText,
		ENGINE->renderHandler().loadFont(EFonts::FONT_TINY)->getLineHeight() - 2);
	handicap = std::make_shared<LRClickableArea>(Rect(53, 23, 50, 24), [](){
		if(!GAME->server().isHost())
			return;
		
		ENGINE->windows().createAndPushWindow<HandicapWindow>();
	}, [this, hasHandicap](){
		if(hasHandicap())
			CRClickPopup::createAndPush(MetaString::createFromTextID("core.help.124.help").toString());
		else
		{
			auto str = MetaString::createFromTextID("vcmi.lobby.handicap");
			str.appendRawString(":\n");
			for(auto & res : LIBRARY->resourceTypeHandler->getAllObjects())
				if(s->handicap.startBonus[res] != 0)
				{
					str.appendRawString("\n");
					str.appendName(res);
					str.appendRawString(": ");
					str.appendRawString(std::to_string(s->handicap.startBonus[res]));
				}
			if(s->handicap.percentIncome != 100)
			{
				str.appendRawString("\n");
				str.appendTextID("core.jktext.32");
				str.appendRawString(": ");
				str.appendRawString(std::to_string(s->handicap.percentIncome) + "%");
			}
			if(s->handicap.percentGrowth != 100)
			{
				str.appendRawString("\n");
				str.appendTextID("core.genrltxt.194");
				str.appendRawString(": ");
				str.appendRawString(std::to_string(s->handicap.percentGrowth) + "%");
			}
			CRClickPopup::createAndPush(str.toString());
		}
	});

	if(SEL->screenType == ESelectionScreen::newGame)
	{
		buttonTownLeft   = std::make_shared<CButton>(Point(107, 5), AnimationPath::builtin("ADOPLFA.DEF"), LIBRARY->generaltexth->zelp[132], std::bind(&IServerAPI::setPlayerOption, &GAME->server(), LobbyChangePlayerOption::TOWN, -1, s->color));
		buttonTownRight  = std::make_shared<CButton>(Point(168, 5), AnimationPath::builtin("ADOPRTA.DEF"), LIBRARY->generaltexth->zelp[133], std::bind(&IServerAPI::setPlayerOption, &GAME->server(), LobbyChangePlayerOption::TOWN, +1, s->color));
		buttonHeroLeft   = std::make_shared<CButton>(Point(183, 5), AnimationPath::builtin("ADOPLFA.DEF"), LIBRARY->generaltexth->zelp[148], std::bind(&IServerAPI::setPlayerOption, &GAME->server(), LobbyChangePlayerOption::HERO, -1, s->color));
		buttonHeroRight  = std::make_shared<CButton>(Point(244, 5), AnimationPath::builtin("ADOPRTA.DEF"), LIBRARY->generaltexth->zelp[149], std::bind(&IServerAPI::setPlayerOption, &GAME->server(), LobbyChangePlayerOption::HERO, +1, s->color));
		buttonBonusLeft  = std::make_shared<CButton>(Point(259, 5), AnimationPath::builtin("ADOPLFA.DEF"), LIBRARY->generaltexth->zelp[164], std::bind(&IServerAPI::setPlayerOption, &GAME->server(), LobbyChangePlayerOption::BONUS, -1, s->color));
		buttonBonusRight = std::make_shared<CButton>(Point(320, 5), AnimationPath::builtin("ADOPRTA.DEF"), LIBRARY->generaltexth->zelp[165], std::bind(&IServerAPI::setPlayerOption, &GAME->server(), LobbyChangePlayerOption::BONUS, +1, s->color));
	}

	hideUnavailableButtons();

	if(SEL->screenType != ESelectionScreen::scenarioInfo && SEL->getPlayerInfo(s->color).canHumanPlay)
	{
		flag = std::make_shared<CButton>(
			Point(-43, 2),
			AnimationPath::builtin(flags[DmbSeatColours::shown(s->color).getNum()]),
			LIBRARY->generaltexth->zelp[180],
			std::bind(&OptionsTab::onSetPlayerClicked, &parentTab, *s)
		);
		flag->setHoverable(true);
		flag->block(GAME->server().isGuest());
	}
	else
		flag = nullptr;

	town = std::make_shared<SelectedBox>(Point(119, 2), *s, TOWN);
	hero = std::make_shared<SelectedBox>(Point(195, 2), *s, HERO);
	bonus = std::make_shared<SelectedBox>(Point(271, 2), *s, BONUS);
}

void OptionsTab::PlayerOptionsEntry::updateName() {
	if(labelPlayerNameEdit->getText() != name)
	{
		GAME->server().setPlayerName(s->color, labelPlayerNameEdit->getText());
		if(GAME->server().isMyColor(s->color))
		{
			Settings set = settings.write["general"]["playerName"];
			set->String() = labelPlayerNameEdit->getText();
		}
	}
	name = labelPlayerNameEdit->getText();
}

void OptionsTab::onSetPlayerClicked(const PlayerSettings & ps) const
{
	// DMB (K, November 7th: "i should be able to click my (the host's) slot color to remove myself (and then be unslotted)" and "other unseated players should also be able to be
	// brought in as observers"): stock lets a click through only for an AI's seat or when more than one human is seated, so a lone human could not leave a seat, the host's own or a
	// guest's (the two-client proof, 11:13: the host could not unseat the one guest). The flag of a seat is the host's only (a guest's is blocked), and a game with no human seat is allowed,
	// so the host may click any seat: the server gives the host's own seat to the AI, and a guest's to the next player or the AI
	GAME->server().setPlayer(ps.color);
}

void OptionsTab::PlayerOptionsEntry::hideUnavailableButtons()
{
	if(!buttonTownLeft)
		return;

	const bool foreignPlayer = GAME->server().isGuest() && !GAME->server().isMyColor(s->color);

	if((pi->allowedFactions.size() < 2 && !pi->isFactionRandom) || foreignPlayer)
	{
		buttonTownLeft->disable();
		buttonTownRight->disable();
	}
	else
	{
		buttonTownLeft->enable();
		buttonTownRight->enable();
	}

	if((pi->defaultHero() != HeroTypeID::RANDOM || !s->castle.isValid()) //fixed hero
		|| foreignPlayer) //or not our player
	{
		buttonHeroLeft->disable();
		buttonHeroRight->disable();
	}
	else
	{
		buttonHeroLeft->enable();
		buttonHeroRight->enable();
	}

	if(foreignPlayer)
	{
		buttonBonusLeft->disable();
		buttonBonusRight->disable();
	}
	else
	{
		buttonBonusLeft->enable();
		buttonBonusRight->enable();
	}
}
