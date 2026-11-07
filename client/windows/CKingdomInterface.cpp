/*
 * CKingdomInterface.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CKingdomInterface.h"

#include "CCastleInterface.h"
#include "CPlayerState.h"
#include "InfoWindows.h"

#include "../CPlayerInterface.h"
#include "../PlayerLocalState.h"
#include "../adventureMap/CResDataBar.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../gui/Shortcut.h"
#include "../gui/WindowHandler.h"
#include "../widgets/CComponent.h"
#include "../widgets/CGarrisonInt.h"
#include "../widgets/TextControls.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/Buttons.h"
#include "../widgets/ObjectLists.h"
#include "../windows/CHeroWindow.h"
#include "../windows/CMarketWindow.h"

#include "../../lib/CConfigHandler.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/StartInfo.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/hero/CHeroHandler.h"
#include "../../lib/entities/ResourceTypeHandler.h"
#include "../../lib/texts/TextOperations.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/MiscObjects.h"
#include "../../lib/mapObjectConstructors/CommonConstructors.h"
#include "../../lib/mapObjectConstructors/CObjectClassesHandler.h"
#include "texts/CGeneralTextHandler.h"
#include "../../lib/GameSettings.h"

static const std::string OVERVIEW_BACKGROUND = "OvCast.pcx";
static const size_t OVERVIEW_SIZE = 4;

// DMB (S070, K 11-07: "review player resources, town states, troops and garrisons, etc."): an observer (a watcher with no player state of
// its own) sees any player's kingdom in this window, read-only. A player sees their own, exactly as before.
namespace
{
PlayerColor kingdomShown = PlayerColor::CANNOT_DETERMINE;

bool kingdomObserver()
{
	return GAME->interface()->cb->getPlayerState(GAME->interface()->playerID, false) == nullptr;
}

PlayerColor kingdomPlayer()
{
	return kingdomObserver() ? kingdomShown : GAME->interface()->playerID;
}

const PlayerState * kingdomState()
{
	return GAME->interface()->cb->getPlayerState(kingdomPlayer(), false);
}

std::vector<PlayerColor> kingdomPlayersInGame()
{
	std::vector<PlayerColor> out;
	for(const auto & seat : GAME->interface()->cb->getStartInfo()->playerInfos)
	{
		const auto * state = GAME->interface()->cb->getPlayerState(seat.first, false);
		if(state && state->status == EPlayerStatus::INGAME)
			out.push_back(seat.first);
	}
	return out;
}

std::vector<const CGHeroInstance *> kingdomHeroes()
{
	if(!kingdomObserver())
		return GAME->interface()->localState->getWanderingHeroes();
	std::vector<const CGHeroInstance *> out;
	if(const auto * state = kingdomState())
		for(const auto * hero : state->getHeroes())
			if(hero && !hero->isGarrisoned()) // a hero in a town's garrison is shown with its town
				out.push_back(hero);
	return out;
}

std::vector<const CGTownInstance *> kingdomTowns()
{
	if(!kingdomObserver())
		return GAME->interface()->localState->getOwnedTowns();
	if(const auto * state = kingdomState())
		return state->getTowns();
	return {};
}

std::vector<const CGObjectInstance *> kingdomObjects()
{
	if(!kingdomObserver())
		return GAME->interface()->cb->getMyObjects();
	if(const auto * state = kingdomState())
		return state->getOwnedObjects();
	return {};
}

std::string kingdomShort(uint64_t value)
{
	return value >= 100000 ? std::to_string(value / 1000) + "k" : std::to_string(value);
}
}

InfoBox::InfoBox(Point position, InfoPos Pos, InfoSize Size, std::shared_ptr<IInfoBoxData> Data):
	size(Size),
	infoPos(Pos),
	data(Data),
	value(nullptr),
	name(nullptr)
{
	assert(data);
	addUsedEvents(LCLICK | SHOW_POPUP);
	EFonts font = (size < SIZE_MEDIUM)? FONT_SMALL: FONT_MEDIUM;

	OBJECT_CONSTRUCTION;
	pos+=position;

	image = std::make_shared<CAnimImage>(data->getImageName(size), data->getImageIndex());
	pos = image->pos;

	switch(infoPos)
	{
	case POS_CORNER:
		value = std::make_shared<CLabel>(pos.w, pos.h, font, ETextAlignment::BOTTOMRIGHT, Colors::WHITE, data->getValueText());
		break;
	case POS_INSIDE:
		value = std::make_shared<CLabel>(pos.w/2, pos.h-6, font, ETextAlignment::CENTER, Colors::WHITE, data->getValueText());
		break;
	case POS_UP_DOWN:
		name = std::make_shared<CLabel>(pos.w/2, -12, font, ETextAlignment::CENTER, Colors::WHITE, data->getNameText());
		[[fallthrough]];
	case POS_DOWN:
		value = std::make_shared<CLabel>(pos.w/2, pos.h+8, font, ETextAlignment::CENTER, Colors::WHITE, data->getValueText());
		break;
	case POS_RIGHT:
		name = std::make_shared<CLabel>(pos.w+6, 6, font, ETextAlignment::TOPLEFT, Colors::WHITE, data->getNameText());
		value = std::make_shared<CLabel>(pos.w+6, pos.h-16, font, ETextAlignment::TOPLEFT, Colors::WHITE, data->getValueText());
		break;
	}

	if(name)
		pos = pos.include(name->pos);
	if(value)
		pos = pos.include(value->pos);

	hover = std::make_shared<CHoverableArea>();
	hover->hoverText = data->getHoverText();
	hover->pos = pos;
}

InfoBox::~InfoBox() = default;

void InfoBox::showPopupWindow(const Point & cursorPosition)
{
	std::shared_ptr<CComponent> comp;
	std::string text;
	data->prepareMessage(text, comp);

	if (comp)
		CRClickPopup::createAndPush(text, CInfoWindow::TCompsInfo(1, comp));
	else if (!text.empty())
		CRClickPopup::createAndPush(text);
}

void InfoBox::clickPressed(const Point & cursorPosition)
{
	std::shared_ptr<CComponent> comp;
	std::string text;
	data->prepareMessage(text, comp);

	if(comp)
		GAME->interface()->showInfoDialog(text, CInfoWindow::TCompsInfo(1, comp));
	else if (!text.empty())
		GAME->interface()->showInfoDialog(text);
}

IInfoBoxData::IInfoBoxData(InfoType Type)
	: type(Type)
{
}

InfoBoxAbstractHeroData::InfoBoxAbstractHeroData(InfoType Type)
	: IInfoBoxData(Type)
{
}

std::string InfoBoxAbstractHeroData::getValueText()
{
	switch (type)
	{
	case HERO_MANA:
	case HERO_PRIMARY_SKILL:
		return std::to_string(getValue());
	case HERO_EXPERIENCE:
		return TextOperations::formatMetric(getValue(), 6);
	case HERO_SPECIAL:
		return LIBRARY->generaltexth->jktexts[5];
	case HERO_SECONDARY_SKILL:
		{
			si64 value = getValue();
			if (value)
				return LIBRARY->generaltexth->levels[value];
			else
				return "";
		}
	default:
		logGlobal->error("Invalid InfoBox info type");
	}
	return "";
}

std::string InfoBoxAbstractHeroData::getNameText()
{
	switch (type)
	{
	case HERO_PRIMARY_SKILL:
		return LIBRARY->generaltexth->primarySkillNames[getSubID()];
	case HERO_MANA:
		return LIBRARY->generaltexth->allTexts[387];
	case HERO_EXPERIENCE:
		return LIBRARY->generaltexth->jktexts[6];
	case HERO_SPECIAL:
		return LIBRARY->heroh->objects[getSubID()]->getSpecialtyNameTranslated();
	case HERO_SECONDARY_SKILL:
		if (getValue())
			return LIBRARY->skillh->getByIndex(getSubID())->getNameTranslated();
		else
			return "";
	default:
		logGlobal->error("Invalid InfoBox info type");
	}
	return "";
}

AnimationPath InfoBoxAbstractHeroData::getImageName(InfoBox::InfoSize size)
{
	//TODO: sizes
	switch(size)
	{
	case InfoBox::SIZE_SMALL:
		{
			switch(type)
			{
			case HERO_PRIMARY_SKILL:
			case HERO_MANA:
			case HERO_EXPERIENCE:
				return AnimationPath::builtin("PSKIL32");
			case HERO_SPECIAL:
				return AnimationPath::builtin("UN32");
			case HERO_SECONDARY_SKILL:
				return AnimationPath::builtin("SECSK32");
			default:
				assert(0);
			}
		}
	case InfoBox::SIZE_BIG:
		{
			switch(type)
			{
			case HERO_PRIMARY_SKILL:
			case HERO_MANA:
			case HERO_EXPERIENCE:
				return AnimationPath::builtin("PSKIL42");
			case HERO_SPECIAL:
				return AnimationPath::builtin("UN44");
			case HERO_SECONDARY_SKILL:
				return AnimationPath::builtin("SECSKILL");
			default:
				assert(0);
			}
		}
	default:
		assert(0);
	}
	return {};
}

std::string InfoBoxAbstractHeroData::getHoverText()
{
	//TODO: any texts here?
	return "";
}

size_t InfoBoxAbstractHeroData::getImageIndex()
{
	switch (type)
	{
	case HERO_SPECIAL:
		return LIBRARY->heroh->objects[getSubID()]->imageIndex;
	case HERO_PRIMARY_SKILL:
		return getSubID();
	case HERO_MANA:
		return 5;
	case HERO_EXPERIENCE:
		return 4;
	case HERO_SECONDARY_SKILL:
		{
			si64 value = getValue();
			if (value)
				return getSubID()*3 + value + 2;
			else
				return 0;//FIXME: Should be transparent instead of empty
		}
	default:
		assert(0);
		return 0;
	}
}

void InfoBoxAbstractHeroData::prepareMessage(std::string & text, std::shared_ptr<CComponent> & comp)
{
	comp.reset();
	switch (type)
	{
	case HERO_SPECIAL:
		text = LIBRARY->heroh->objects[getSubID()]->getSpecialtyDescriptionTranslated();
		break;
	case HERO_PRIMARY_SKILL:
		text = LIBRARY->generaltexth->arraytxt[2+getSubID()];
		comp = std::make_shared<CComponent>(ComponentType::PRIM_SKILL, PrimarySkill(getSubID()), getValue());
		break;
	case HERO_MANA:
		text = LIBRARY->generaltexth->allTexts[149];
		break;
	case HERO_EXPERIENCE:
		text = LIBRARY->generaltexth->allTexts[241];
		break;
	case HERO_SECONDARY_SKILL:
		{
			si64 value = getValue();
			int  subID = getSubID();
			if(value)
			{
				text = LIBRARY->skillh->getByIndex(subID)->getDescriptionTranslated((int)value);
				comp = std::make_shared<CComponent>(ComponentType::SEC_SKILL, SecondarySkill(subID), (int)value);
			}
			break;
		}
	default:
		break;
	}
}

InfoBoxHeroData::InfoBoxHeroData(InfoType Type, const CGHeroInstance * Hero, int Index):
	InfoBoxAbstractHeroData(Type),
	hero(Hero),
	index(Index)
{
}

int InfoBoxHeroData::getSubID()
{
	switch(type)
	{
	case HERO_PRIMARY_SKILL:
		return index;
	case HERO_SECONDARY_SKILL:
		if(hero->secSkills.size() > index)
			return hero->secSkills[index].first.getNum();
		else
			return 0;
	case HERO_SPECIAL:
		return hero->getHeroTypeID().getNum();
	case HERO_MANA:
	case HERO_EXPERIENCE:
		return 0;
	default:
		assert(0);
		return 0;
	}
}

si64 InfoBoxHeroData::getValue()
{
	if(!hero)
		return 0;

	switch(type)
	{
	case HERO_PRIMARY_SKILL:
		return hero->getPrimSkillLevel(static_cast<PrimarySkill>(index));
	case HERO_MANA:
		return hero->mana;
	case HERO_EXPERIENCE:
		return hero->exp;
	case HERO_SECONDARY_SKILL:
		if(hero->secSkills.size() > index)
			return hero->secSkills[index].second;
		else
			return 0;
	case HERO_SPECIAL:
		return 0;
	default:
		assert(0);
		return 0;
	}
}

std::string InfoBoxHeroData::getHoverText()
{
	switch (type)
	{
	case HERO_PRIMARY_SKILL:
		return boost::str(boost::format(LIBRARY->generaltexth->heroscrn[1]) % LIBRARY->generaltexth->primarySkillNames[index]);
	case HERO_MANA:
		return LIBRARY->generaltexth->heroscrn[22];
	case HERO_EXPERIENCE:
		return LIBRARY->generaltexth->heroscrn[9];
	case HERO_SPECIAL:
		return LIBRARY->generaltexth->heroscrn[27];
	case HERO_SECONDARY_SKILL:
		if (hero->secSkills.size() > index)
		{
			std::string level = LIBRARY->generaltexth->levels[hero->secSkills[index].second-1];
			std::string skill = hero->secSkills[index].first.toEntity(LIBRARY)->getNameTranslated();
			return boost::str(boost::format(LIBRARY->generaltexth->heroscrn[21]) % level % skill);
		}
		else
		{
			return "";
		}
	default:
		return InfoBoxAbstractHeroData::getHoverText();
	}
}

std::string InfoBoxHeroData::getValueText()
{
	if (hero)
	{
		switch (type)
		{
		case HERO_MANA:
			return std::to_string(hero->mana) + '/' +
				std::to_string(hero->manaLimit());
		case HERO_EXPERIENCE:
			return TextOperations::formatMetric(hero->exp, 6);
		}
	}
	return InfoBoxAbstractHeroData::getValueText();
}

void InfoBoxHeroData::prepareMessage(std::string & text, std::shared_ptr<CComponent> & comp)
{
	comp.reset();
	switch(type)
	{
	case HERO_MANA:
		text = LIBRARY->generaltexth->allTexts[205];
		boost::replace_first(text, "%s", hero->getNameTranslated());
		boost::replace_first(text, "%d", std::to_string(hero->mana));
		boost::replace_first(text, "%d", std::to_string(hero->manaLimit()));
		break;
	case HERO_EXPERIENCE:
		text = LIBRARY->generaltexth->allTexts[2];
		boost::replace_first(text, "%d", std::to_string(hero->level));
		boost::replace_first(text, "%d", std::to_string(LIBRARY->heroh->reqExp(hero->level+1)));
		boost::replace_first(text, "%d", std::to_string(hero->exp));
		break;
	default:
		InfoBoxAbstractHeroData::prepareMessage(text, comp);
		break;
	}
}

InfoBoxCustomHeroData::InfoBoxCustomHeroData(InfoType Type, int SubID, si64 Value):
	InfoBoxAbstractHeroData(Type),
	subID(SubID),
	value(Value)
{
}

int InfoBoxCustomHeroData::getSubID()
{
	return subID;
}

si64 InfoBoxCustomHeroData::getValue()
{
	return value;
}

InfoBoxCustom::InfoBoxCustom(std::string ValueText, std::string NameText, const AnimationPath & ImageName, size_t ImageIndex, std::string HoverText):
	IInfoBoxData(CUSTOM),
	valueText(ValueText),
	nameText(NameText),
	imageName(ImageName),
	hoverText(HoverText),
	imageIndex(ImageIndex)
{
}

std::string InfoBoxCustom::getHoverText()
{
	return hoverText;
}

size_t InfoBoxCustom::getImageIndex()
{
	return imageIndex;
}

AnimationPath InfoBoxCustom::getImageName(InfoBox::InfoSize size)
{
	return imageName;
}

std::string InfoBoxCustom::getNameText()
{
	return nameText;
}

std::string InfoBoxCustom::getValueText()
{
	return valueText;
}

void InfoBoxCustom::prepareMessage(std::string & text, std::shared_ptr<CComponent> & comp)
{
}

CKingdomInterface::CKingdomInterface(PlayerColor shown)
	: CWindowObject(PLAYER_COLORED | BORDERED, ImagePath::builtin(OVERVIEW_BACKGROUND))
{
	OBJECT_CONSTRUCTION;
	ui32 footerPos = OVERVIEW_SIZE * 116;

	// DMB (S070): an observer's window opens on the player asked for, else the one shown last, else the first player still in the game
	if(kingdomObserver())
	{
		const auto players = kingdomPlayersInGame();
		if(vstd::contains(players, shown))
			kingdomShown = shown;
		else if(!vstd::contains(players, kingdomShown))
			kingdomShown = players.empty() ? PlayerColor::CANNOT_DETERMINE : players.front();
		if(kingdomShown.isValidPlayer() && background)
			background->setPlayerColor(kingdomShown);
		addUsedEvents(TIME);
	}

	tabArea = std::make_shared<CTabbedInt>(std::bind(&CKingdomInterface::createMainTab, this, _1), Point(4,4));

	std::vector<const CGObjectInstance * > ownedObjects = kingdomObjects();
	generateObjectsList(ownedObjects);
	generateMinesList(ownedObjects, 0);
	generateButtons();

	statusbar = CGStatusBar::create(std::make_shared<CPicture>(ImagePath::builtin("KSTATBAR"), 10,pos.h - 45));
	resdatabar = std::make_shared<CResDataBar>(ImagePath::builtin("KRESBAR"), 7, 111+footerPos, 29, 3, 76, 81);

	if(kingdomObserver())
	{
		if(kingdomShown.isValidPlayer())
			resdatabar->setShownPlayer(kingdomShown);
		generatePlayerFlags();
		shownSignature = kingdomSignature();
	}

	activateTab(settings["general"]["lastKindomInterface"].Integer());
}

void CKingdomInterface::generatePlayerFlags()
{
	OBJECT_CONSTRUCTION;
	// the flags sit in the list's title strip, left end; the first column's label moves right for an observer (CKingdHeroList, CKingdTownList)
	int x = 24;
	for(const PlayerColor & player : kingdomPlayersInGame())
	{
		auto flag = std::make_shared<CAnimImage>(AnimationPath::builtin("ITGFLAGS"), player.getNum(), 0, x, 6);
		auto area = std::make_shared<LRClickableArea>(Rect(x, 6, flag->pos.w, flag->pos.h), [this, player]()
		{
			// deferred: the click's own widget must not be rebuilt inside its handler
			ENGINE->dispatchMainThread([this, player]()
			{
				if(ENGINE->windows().topWindow<CKingdomInterface>().get() == this)
				{
					showPlayer(player);
					activateTab(0);
				}
			});
		});
		x += flag->pos.w + 2;
		playerFlags.push_back(flag);
		playerFlagAreas.push_back(area);
	}
	allPlayersLabel = std::make_shared<CLabel>(x + 4, 7, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::YELLOW, LIBRARY->generaltexth->translate("vcmi.kingdomOverview.allPlayers"));
	allPlayersArea = std::make_shared<LRClickableArea>(Rect(x + 2, 6, allPlayersLabel->pos.w + 6, 16), [this]()
	{
		ENGINE->dispatchMainThread([this]()
		{
			if(ENGINE->windows().topWindow<CKingdomInterface>().get() == this)
				activateTab(2);
		});
	});
	const std::string name = kingdomShown.isValidPlayer() ? LIBRARY->generaltexth->capColors[kingdomShown.getNum()] : std::string();
	shownPlayerLabel = std::make_shared<CLabel>(x + allPlayersLabel->pos.w + 16, 7, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, name);
}

void CKingdomInterface::showPlayer(PlayerColor player)
{
	OBJECT_CONSTRUCTION;
	kingdomShown = player;
	if(background)
		background->setPlayerColor(player);
	objects.clear();
	const auto ownedObjects = kingdomObjects();
	generateObjectsList(ownedObjects);
	generateMinesList(ownedObjects, 0);
	if(resdatabar)
		resdatabar->setShownPlayer(player);
	if(shownPlayerLabel)
		shownPlayerLabel->setText(LIBRARY->generaltexth->capColors[player.getNum()]);
	shownSignature = kingdomSignature();
	tabArea->reset();
	redraw();
}

uint64_t CKingdomInterface::kingdomSignature() const
{
	// what the shown player's footer and lists show: resources, objects, heroes, towns and their armies; a change means the view is stale
	const auto * state = kingdomState();
	if(!state)
		return 0;
	uint64_t sig = 1469598103934665603ULL;
	auto mix = [&sig](uint64_t v)
	{
		sig ^= v;
		sig *= 1099511628211ULL;
	};
	for(const auto & resource : LIBRARY->resourceTypeHandler->getAllObjects())
		mix(static_cast<uint64_t>(state->resources[resource]));
	mix(state->getOwnedObjects().size());
	for(const auto * hero : state->getHeroes())
		mix(hero->getArmyStrength() + 31ULL * hero->id.getNum() + 7ULL * hero->exp);
	for(const auto * town : state->getTowns())
		mix(town->getUpperArmy()->getArmyStrength() + 31ULL * town->id.getNum() + town->built);
	return sig;
}

void CKingdomInterface::tick(uint32_t msPassed)
{
	// an observer's view follows the AIs: checked twice a second, rebuilt only when the shown kingdom changed (a paused game never changes)
	sinceRefresh += msPassed;
	if(sinceRefresh < 500 || !kingdomObserver() || !kingdomShown.isValidPlayer())
		return;
	sinceRefresh = 0;
	const uint64_t now = kingdomSignature();
	if(now != shownSignature)
		showPlayer(kingdomShown);
}

void CKingdomInterface::generateObjectsList(const std::vector<const CGObjectInstance * > &ownedObjects)
{
	ui32 footerPos = OVERVIEW_SIZE * 116;
	size_t dwellSize = (footerPos - 64)/57;

	//Map used to determine image number for several objects
	std::map<std::pair<int,int>,int> idToImage;
	idToImage[std::make_pair( 20, 1)] = 81;//Golem factory
	idToImage[std::make_pair( 42, 0)] = 82;//Lighthouse
	idToImage[std::make_pair( 33, 0)] = 83;//Garrison
	idToImage[std::make_pair(219, 0)] = 83;//Garrison
	idToImage[std::make_pair( 33, 1)] = 84;//Anti-magic Garrison
	idToImage[std::make_pair(219, 1)] = 84;//Anti-magic Garrison
	idToImage[std::make_pair( 53, 7)] = 85;//Abandoned mine
	idToImage[std::make_pair( 20, 0)] = 86;//Conflux
	idToImage[std::make_pair( 87, 0)] = 87;//Harbor

	std::map<int, OwnedObjectInfo> visibleObjects;
	for(const CGObjectInstance * object : ownedObjects)
	{
		//Dwellings
		if(auto * dwelling = dynamic_cast<const CGDwelling *>(object))
		{
			auto kingdomOverviewImage = dwelling->getKingdomOverviewImage();

			if(!kingdomOverviewImage.empty())
			{
				OwnedObjectInfo & info = visibleObjects[object->subID];
				if(info.count++ == 0)
				{
					info.hoverText = object->getObjectName();
					info.imagePath = kingdomOverviewImage;
					info.imageID = 0;
				}
			}
			else if(object->ID == Obj::CREATURE_GENERATOR1)
			{
				OwnedObjectInfo & info = visibleObjects[object->subID];
				if(info.count++ == 0)
				{
					info.hoverText = object->getObjectName();
					info.imageID = object->subID;
				}
			}
		}

		//Special objects from idToImage map that should be displayed in objects list
		auto iter = idToImage.find(std::make_pair(object->ID, object->subID));
		if(iter != idToImage.end())
		{
			OwnedObjectInfo & info = visibleObjects[iter->second];
			if(info.count++ == 0)
			{
				info.hoverText = object->getObjectName();
				info.imageID = iter->second;
			}
		}
	}
	objects.reserve(visibleObjects.size());

	for(auto & element : visibleObjects)
	{
		objects.push_back(element.second);
	}
	dwellingsList = std::make_shared<CListBox>(std::bind(&CKingdomInterface::createOwnedObject, this, _1),
		Point(740,44), Point(0,57), dwellSize, visibleObjects.size());
}

std::shared_ptr<CIntObject> CKingdomInterface::createOwnedObject(size_t index)
{
	if(index < objects.size())
	{
		OwnedObjectInfo & obj = objects[index];
		std::string value = std::to_string(obj.count);
		auto data = std::make_shared<InfoBoxCustom>(value, "", obj.imagePath.empty() ? AnimationPath::builtin("FLAGPORT") : obj.imagePath, obj.imageID, obj.hoverText);
		return std::make_shared<InfoBox>(Point(), InfoBox::POS_CORNER, InfoBox::SIZE_SMALL, data);
	}
	return std::shared_ptr<CIntObject>();
}

std::shared_ptr<CIntObject> CKingdomInterface::createMainTab(size_t index)
{
	size_t size = OVERVIEW_SIZE;
	switch(index)
	{
	case 0:
		return std::make_shared<CKingdHeroList>(size, [this](const CWindowWithArtifacts::CArtifactsOfHeroPtr & newHeroSet)
			{
				// DMB (S070): an observer looks at artifacts (the popup) and moves none
				if(!kingdomObserver())
				{
					newHeroSet->clickPressedCallback = [this, newHeroSet](const CArtPlace & artPlace, const Point & cursorPosition)
					{
						clickPressedOnArtPlace(newHeroSet->getHero(), artPlace.slot, false, false, false, cursorPosition);
					};
					newHeroSet->gestureCallback = [this, newHeroSet](const CArtPlace & artPlace, const Point & cursorPosition)
					{
						showQuickBackpackWindow(newHeroSet->getHero(), artPlace.slot, cursorPosition);
					};
				}
				newHeroSet->showPopupCallback = [this, newHeroSet](CArtPlace & artPlace, const Point & cursorPosition)
				{
					showArtifactPopup(*newHeroSet, artPlace, cursorPosition);
				};
				addSet(newHeroSet);
			});
	case 1:
		return std::make_shared<CKingdTownList>(size);
	case 2:
		// DMB (S070): the observer's all-players tab; a click on a row shows that player's heroes
		return std::make_shared<CKingdPlayersList>([this](PlayerColor player)
			{
				ENGINE->dispatchMainThread([this, player]()
				{
					if(ENGINE->windows().topWindow<CKingdomInterface>().get() == this)
					{
						showPlayer(player);
						activateTab(0);
					}
				});
			});
	default:
		return std::shared_ptr<CIntObject>();
	}
}

std::shared_ptr<MineInstanceConstructor> CKingdomInterface::getMineHandler(const GameResID & res)
{
	std::shared_ptr<MineInstanceConstructor> mineHandler;
	for(auto & subObjID : LIBRARY->objtypeh->knownSubObjects(Obj::MINE))
	{
		auto handler = std::dynamic_pointer_cast<MineInstanceConstructor>(LIBRARY->objtypeh->getHandlerFor(Obj::MINE, subObjID));
		if(handler->getResourceType() == res)
			mineHandler = handler;
	}

	if(!mineHandler)
	{
		logGlobal->error("No mine for resource %s found!", res.toResource()->getJsonKey());
		return nullptr;
	}

	return mineHandler;
}

void CKingdomInterface::generateMinesList(const std::vector<const CGObjectInstance *> & ownedObjects, int line)
{
	OBJECT_CONSTRUCTION;

	ui32 footerPos = OVERVIEW_SIZE * 116;
	ResourceSet minesCount = ResourceSet();
	int totalIncome=0;

	for(auto & ptr : minesBox)
    	ptr.reset();

	for(const CGObjectInstance * object : ownedObjects)
	{
		//Mines
		if(object->ID == Obj::MINE || object->ID == Obj::ABANDONED_MINE)
		{
			const CGMine * mine = dynamic_cast<const CGMine *>(object);
			minesCount[mine->producedResource]++;
		}
	}

	for(auto & mapObject : ownedObjects)
		totalIncome += mapObject->asOwnable()->dailyIncome()[EGameResID::GOLD];

	//if player has some modded boosts we want to show that as well
	// DMB (S070): the shown player's settings, towns and state (an observer has none of its own)
	const auto * playerSettings = GAME->interface()->cb->getPlayerSettings(kingdomPlayer());
	const auto towns = kingdomTowns();
	if(const auto * shownState = kingdomState(); shownState && playerSettings)
	{
		totalIncome += shownState->valOfBonuses(BonusType::RESOURCES_CONSTANT_BOOST, BonusSubtypeID(GameResID(EGameResID::GOLD))) * playerSettings->handicap.percentIncome / 100;
		totalIncome += shownState->valOfBonuses(BonusType::RESOURCES_TOWN_MULTIPLYING_BOOST, BonusSubtypeID(GameResID(EGameResID::GOLD))) * towns.size() * playerSettings->handicap.percentIncome / 100;
	}

	for(int i=0; i<GameConstants::RESOURCE_QUANTITY; i++)
	{
		int resID = line * GameConstants::RESOURCE_QUANTITY + i;
		if(resID >= LIBRARY->resourceTypeHandler->getAllObjects().size())
			break;

		std::string value = std::to_string(minesCount[resID]);
		std::shared_ptr<InfoBoxCustom> data;
		if(line == 0)
			data = std::make_shared<InfoBoxCustom>(value, "", AnimationPath::builtin("OVMINES"), i, LIBRARY->generaltexth->translate("core.minename", i));
		else
		{
			auto mine = getMineHandler(GameResID(resID));
			if(!mine || mine->getKingdomOverviewImage().empty())
				continue;
			data = std::make_shared<InfoBoxCustom>(value, "", mine->getKingdomOverviewImage(), 0, mine->getNameTranslated());
		}
		minesBox[i] = std::make_shared<InfoBox>(Point(20+i*80, 31+footerPos), InfoBox::POS_INSIDE, InfoBox::SIZE_SMALL, data);
		minesBox[i]->removeUsedEvents(LCLICK|SHOW_POPUP); //fixes #890 - mines boxes ignore clicks
	}

	if(LIBRARY->resourceTypeHandler->getAllObjects().size() > GameConstants::RESOURCE_QUANTITY)
	{
		int lines = vstd::divideAndCeil(LIBRARY->resourceTypeHandler->getAllObjects().size(), GameConstants::RESOURCE_QUANTITY);
		minesSlider = std::make_shared<CSlider>(Point(723, 495), 57, [this, ownedObjects](int to){
			generateMinesList(ownedObjects, to);
			statusbar->clear();
			setRedrawParent(true);
			redraw();
		}, 1, lines, line, Orientation::VERTICAL, CSlider::BROWN);
		minesSlider->setPanningStep(57);
		minesSlider->setScrollBounds(Rect(-735, 0, 735, 57));
	}

	incomeArea = std::make_shared<CHoverableArea>();
	incomeArea->pos = Rect(pos.x+580, pos.y+31+footerPos, 136, 68);
	incomeArea->hoverText = LIBRARY->generaltexth->allTexts[255];
	incomeAmount = std::make_shared<CLabel>(628, footerPos + 70, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, std::to_string(totalIncome));
}

void CKingdomInterface::generateButtons()
{
	ui32 footerPos = OVERVIEW_SIZE * 116;

	//Main control buttons
	btnHeroes = std::make_shared<CButton>(Point(748, 28+footerPos), AnimationPath::builtin("OVBUTN1.DEF"), CButton::tooltip(LIBRARY->generaltexth->overview[11], LIBRARY->generaltexth->overview[6]),
		std::bind(&CKingdomInterface::activateTab, this, 0), EShortcut::KINGDOM_HEROES_TAB);
	btnHeroes->block(true);

	btnTowns = std::make_shared<CButton>(Point(748, 64+footerPos), AnimationPath::builtin("OVBUTN6.DEF"), CButton::tooltip(LIBRARY->generaltexth->overview[12], LIBRARY->generaltexth->overview[7]),
		std::bind(&CKingdomInterface::activateTab, this, 1), EShortcut::KINGDOM_TOWNS_TAB);

	btnExit = std::make_shared<CButton>(Point(748,99+footerPos), AnimationPath::builtin("OVBUTN1.DEF"), CButton::tooltip(LIBRARY->generaltexth->allTexts[600]),
		std::bind(&CKingdomInterface::close, this), EShortcut::GLOBAL_RETURN);
	btnExit->setImageOrder(3, 4, 5, 6);

	//Object list control buttons
	dwellTop = std::make_shared<CButton>(Point(733, 4), AnimationPath::builtin("OVBUTN4.DEF"), CButton::tooltip(), [&](){ dwellingsList->moveToPos(0); redraw(); });

	dwellBottom = std::make_shared<CButton>(Point(733, footerPos+2), AnimationPath::builtin("OVBUTN4.DEF"), CButton::tooltip(), [&](){ dwellingsList->moveToPos(-1); redraw(); });
	dwellBottom->setImageOrder(2, 3, 4, 5);

	dwellUp = std::make_shared<CButton>(Point(733, 24), AnimationPath::builtin("OVBUTN4.DEF"), CButton::tooltip(), [&](){ dwellingsList->moveToPrev(); redraw(); });
	dwellUp->setImageOrder(4, 5, 6, 7);

	dwellDown = std::make_shared<CButton>(Point(733, footerPos-18), AnimationPath::builtin("OVBUTN4.DEF"), CButton::tooltip(), [&](){ dwellingsList->moveToNext(); redraw(); });
	dwellDown->setImageOrder(6, 7, 8, 9);
}

void CKingdomInterface::activateTab(size_t which)
{
	Settings s = settings.write["general"]["lastKindomInterface"];
	s->Integer() = which;

	if(which == 2 && !kingdomObserver())
		which = 0; // the all-players tab is an observer's only
	btnHeroes->block(which == 0);
	btnTowns->block(which == 1);
	tabArea->setActive(which);
}

void CKingdomInterface::buildChanged()
{
	tabArea->reset();
}

void CKingdomInterface::townChanged(const CGTownInstance *town)
{
	if(auto townList = std::dynamic_pointer_cast<CKingdTownList>(tabArea->getItem()))
		townList->townChanged(town);
}

void CKingdomInterface::heroRemoved()
{
	tabArea->reset();
}

void CKingdomInterface::updateGarrisons()
{
	if(auto garrison = std::dynamic_pointer_cast<IGarrisonHolder>(tabArea->getItem()))
		garrison->updateGarrisons();
}

bool CKingdomInterface::holdsGarrison(const CArmedInstance * army)
{
	return army->getOwner() == kingdomPlayer();
}

CKingdHeroList::CKingdHeroList(size_t maxSize, const CreateHeroItemFunctor & onCreateHeroItemCallback)
{
	OBJECT_CONSTRUCTION;
	title = std::make_shared<CPicture>(ImagePath::builtin("OVTITLE"),16,0);
	title->setPlayerColor(kingdomPlayer());
	// DMB (S070): an observer's flags fill the title strip's left end, so the first label moves right
	heroLabel = std::make_shared<CLabel>(kingdomObserver() ? 300 : 150, 10, FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE, LIBRARY->generaltexth->overview[0]);
	skillsLabel = std::make_shared<CLabel>(500, 10, FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE, LIBRARY->generaltexth->overview[1]);

	ui32 townCount = kingdomHeroes().size();
	ui32 size = OVERVIEW_SIZE*116 + 19;
	heroes = std::make_shared<CListBox>([onCreateHeroItemCallback](size_t idx) -> std::shared_ptr<CIntObject>
		{
			auto heroesList = kingdomHeroes();
			if(idx < heroesList.size())
			{
				auto hero = std::make_shared<CHeroItem>(heroesList[idx]);
				onCreateHeroItemCallback(hero->heroArts);
				return hero;
			}
			else
			{
				return std::make_shared<CAnimImage>(AnimationPath::builtin("OVSLOT"), (idx - 2) % GameConstants::KINGDOM_WINDOW_HEROES_SLOTS);
			}
		}, Point(19,21), Point(0,116), maxSize, townCount, 0, 1, Rect(-19, -21, size, size));
	heroes->getSlider()->setScrollBounds(Rect(0, 0, 725, 483));
}

void CKingdHeroList::updateGarrisons()
{
	for(std::shared_ptr<CIntObject> object : heroes->getItems())
	{
		if(IGarrisonHolder * garrison = dynamic_cast<IGarrisonHolder*>(object.get()))
			garrison->updateGarrisons();
	}
}

bool CKingdHeroList::holdsGarrison(const CArmedInstance * army)
{
	for(std::shared_ptr<CIntObject> object : heroes->getItems())
		if(IGarrisonHolder * garrison = dynamic_cast<IGarrisonHolder*>(object.get()))
			if (garrison->holdsGarrison(army))
				return true;
	return false;
}

CKingdTownList::CKingdTownList(size_t maxSize)
{
	OBJECT_CONSTRUCTION;
	title = std::make_shared<CPicture>(ImagePath::builtin("OVTITLE"), 16, 0);
	title->setPlayerColor(kingdomPlayer());
	townLabel = std::make_shared<CLabel>(kingdomObserver() ? 300 : 146, 10,FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE, LIBRARY->generaltexth->overview[3]);
	garrHeroLabel = std::make_shared<CLabel>(375, 10, FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE, LIBRARY->generaltexth->overview[4]);
	visitHeroLabel = std::make_shared<CLabel>(608, 10, FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE, LIBRARY->generaltexth->overview[5]);

	ui32 townCount = kingdomTowns().size();
	ui32 size = OVERVIEW_SIZE*116 + 19;
	towns = std::make_shared<CListBox>(std::bind(&CKingdTownList::createTownItem, this, _1),
		Point(19,21), Point(0,116), maxSize, townCount, 0, 1, Rect(-19, -21, size, size));
	towns->getSlider()->setScrollBounds(Rect(0, 0, 725, 483));
}

void CKingdTownList::townChanged(const CGTownInstance * town)
{
	for(std::shared_ptr<CIntObject> object : towns->getItems())
	{
		CTownItem * townItem = dynamic_cast<CTownItem *>(object.get());
		if(townItem && townItem->town == town)
			townItem->update();
	}
}

void CKingdTownList::updateGarrisons()
{
	for(std::shared_ptr<CIntObject> object : towns->getItems())
	{
		if(IGarrisonHolder * garrison = dynamic_cast<IGarrisonHolder*>(object.get()))
			garrison->updateGarrisons();
	}
}

bool CKingdTownList::holdsGarrison(const CArmedInstance * army)
{
	for(std::shared_ptr<CIntObject> object : towns->getItems())
		if(IGarrisonHolder * garrison = dynamic_cast<IGarrisonHolder*>(object.get()))
			if (garrison->holdsGarrison(army))
				return true;
	return false;
}

std::shared_ptr<CIntObject> CKingdTownList::createTownItem(size_t index)
{
	ui32 picCount = 4; // OVSLOT contains 4 images

	auto townsList = kingdomTowns();

	if(index < townsList.size())
		return std::make_shared<CTownItem>(townsList[index]);
	else
		return std::make_shared<CAnimImage>(AnimationPath::builtin("OVSLOT"), (index-2) % picCount );
}

CKingdPlayersList::CKingdPlayersList(const std::function<void(PlayerColor)> & onPick)
{
	OBJECT_CONSTRUCTION;
	title = std::make_shared<CPicture>(ImagePath::builtin("OVTITLE"), 16, 0);
	title->setPlayerColor(kingdomPlayer());
	pos = Rect(pos.x, pos.y, 725, 483);
	// header: the seven resources as icons, then gold a day, towns (forts), heroes, the army in heroes and in towns, the strongest hero
	for(int i = 0; i < 7; i++)
		widgets.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("SMALRES"), i, 0, 130 + i * 56, 24));
	const std::vector<std::pair<int, std::string>> heads = {
		{ 528, LIBRARY->generaltexth->translate("vcmi.kingdomOverview.perDay") },
		{ 586, LIBRARY->generaltexth->overview[3] },
		{ 630, LIBRARY->generaltexth->overview[0] },
		{ 676, LIBRARY->generaltexth->translate("vcmi.kingdomOverview.army") } };
	for(const auto & head : heads)
		widgets.push_back(std::make_shared<CLabel>(head.first, 30, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, head.second));

	int y = 50;
	for(const PlayerColor & player : kingdomPlayersInGame())
	{
		const auto * state = GAME->interface()->cb->getPlayerState(player, false);
		if(!state)
			continue;
		widgets.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("ITGFLAGS"), player.getNum(), 0, 22, y + 4));
		widgets.push_back(std::make_shared<CLabel>(50, y + 6, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, LIBRARY->generaltexth->capColors[player.getNum()]));
		for(int i = 0; i < 7; i++)
			widgets.push_back(std::make_shared<CLabel>(130 + i * 56 + 16, y + 6, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, kingdomShort(state->resources[GameResID(i)])));

		int income = 0;
		for(const auto * object : state->getOwnedObjects())
			income += object->asOwnable()->dailyIncome()[EGameResID::GOLD];
		int forts = 0;
		uint64_t inTowns = 0;
		for(const auto * town : state->getTowns())
		{
			if(town->hasFort())
				forts++;
			inTowns += town->getUpperArmy()->getArmyStrength();
		}
		uint64_t inHeroes = 0, best = 0;
		int bestLevel = 0;
		for(const auto * hero : state->getHeroes())
		{
			inHeroes += hero->getArmyStrength();
			if(hero->getArmyStrength() >= best)
			{
				best = hero->getArmyStrength();
				bestLevel = hero->level;
			}
		}
		widgets.push_back(std::make_shared<CLabel>(528, y + 6, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, "+" + std::to_string(income)));
		widgets.push_back(std::make_shared<CLabel>(586, y + 6, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, std::to_string(state->getTowns().size()) + " (" + std::to_string(forts) + ")"));
		widgets.push_back(std::make_shared<CLabel>(630, y + 6, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, std::to_string(state->getHeroes().size())));
		widgets.push_back(std::make_shared<CLabel>(676, y + 6, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, kingdomShort(inHeroes) + " / " + kingdomShort(inTowns)));
		widgets.push_back(std::make_shared<CLabel>(50, y + 24, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::YELLOW,
			LIBRARY->generaltexth->translate("vcmi.kingdomOverview.strongest") + " " + kingdomShort(best) + " (L" + std::to_string(bestLevel) + ")"));
		widgets.push_back(std::make_shared<LRClickableArea>(Rect(16, y, 700, 50), [onPick, player]() { onPick(player); }));
		y += 54;
	}
}

CTownItem::CTownItem(const CGTownInstance * Town)
	: town(Town)
{
	OBJECT_CONSTRUCTION;
	background = std::make_shared<CAnimImage>(AnimationPath::builtin("OVSLOT"), 6);
	name = std::make_shared<CLabel>(74, 8, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, town->getNameTranslated());

	income = std::make_shared<CLabel>( 190, 60, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, std::to_string(town->dailyIncome()[EGameResID::GOLD]));
	hall = std::make_shared<CTownInfo>( 69, 31, town, true);
	fort = std::make_shared<CTownInfo>(111, 31, town, false);

	garr = std::make_shared<CGarrisonInt>(Point(313, 3), 4, Point(232,0), town->getUpperArmy(), town->getVisitingHero(), true, true, CGarrisonInt::ESlotsLayout::TWO_ROWS);
	heroes = std::make_shared<HeroSlots>(town, Point(244,6), Point(475,6), garr, false);

	size_t iconIndex = town->getTown()->clientInfo.icons[town->hasFort()][town->built >= GAME->interface()->cb->getSettings().getInteger(EGameSettings::TOWNS_BUILDINGS_PER_TURN_CAP)];

	picture = std::make_shared<CAnimImage>(AnimationPath::builtin("ITPT"), iconIndex, 0, 5, 6);
	openTown = std::make_shared<LRClickableAreaOpenTown>(Rect(5, 6, 58, 64), town);

	for(size_t i=0; i<town->creatures.size() && i<GameConstants::CREATURES_PER_TOWN; i++)
	{
		growth.push_back(std::make_shared<CCreaInfo>(Point(401+37*(int)i, 78), town, (int)i, true, true));
		available.push_back(std::make_shared<CCreaInfo>(Point(48+37*(int)i, 78), town, (int)i, true, false));
	}

	fastTownHall = std::make_shared<CButton>(Point(69, 31), AnimationPath::builtin("castleInterfaceQuickAccess"), CButton::tooltip(), [this]() { std::make_shared<CCastleBuildings>(town)->enterTownHall(); });
	fastTownHall->setOverlay(std::make_shared<CAnimImage>(AnimationPath::builtin("ITMTL"), town->hallLevel()));

	int imageIndex = town->fortLevel() == CGTownInstance::EFortLevel::NONE ? 3 : town->fortLevel() - 1;
	fastArmyPurchase = std::make_shared<CButton>(Point(111, 31), AnimationPath::builtin("castleInterfaceQuickAccess"), CButton::tooltip(), [this]() { std::make_shared<CCastleBuildings>(town)->enterToTheQuickRecruitmentWindow(); });
	fastArmyPurchase->setOverlay(std::make_shared<CAnimImage>(AnimationPath::builtin("itmcl"), imageIndex));

	fastMarket = std::make_shared<LRClickableArea>(Rect(153, 6, 65, 64), []()
	{
		std::vector<const CGTownInstance*> towns = GAME->interface()->cb->getTownsInfo(true);
		for(auto & town : towns)
		{
			if(town->hasBuilt(BuildingID::MARKETPLACE))
			{
				ENGINE->windows().createAndPushWindow<CMarketWindow>(town, nullptr, nullptr, EMarketMode::RESOURCE_RESOURCE);
				return;
			}
		}
		GAME->interface()->showInfoDialog(LIBRARY->generaltexth->translate("vcmi.adventureMap.noTownWithMarket"));
	});
	fastTown = std::make_shared<LRClickableArea>(Rect(67, 6, 165, 20), [&]()
	{
		ENGINE->windows().createAndPushWindow<CCastleInterface>(town);
	});

	// DMB (S070): an observer builds, recruits and trades nothing in another player's town; the town window itself opens read-only (S6)
	if(kingdomObserver())
	{
		fastTownHall->block(true);
		fastArmyPurchase->block(true);
		fastMarket.reset();
	}

	labelCreatureGrowth = std::make_shared<CMultiLineLabel>(Rect(4, 76, 50, 35), EFonts::FONT_SMALL, ETextAlignment::CENTERLEFT, Colors::YELLOW, LIBRARY->generaltexth->translate("core.genrltxt.265"));
	labelCreatureAvailable = std::make_shared<CMultiLineLabel>(Rect(349, 76, 57, 35), EFonts::FONT_SMALL, ETextAlignment::CENTERLEFT, Colors::YELLOW, LIBRARY->generaltexth->translate("core.genrltxt.266"));
}

void CTownItem::updateGarrisons()
{
	garr->selectSlot(nullptr);
	garr->setArmy(town->getUpperArmy(), EGarrisonType::UPPER);
	garr->setArmy(town->getVisitingHero(), EGarrisonType::LOWER);
	garr->recreateSlots();
}

bool CTownItem::holdsGarrison(const CArmedInstance * army)
{
	return army == town || army == town->getUpperArmy() || army == town->getVisitingHero();
}

void CTownItem::update()
{
	std::string incomeVal = std::to_string(town->dailyIncome()[EGameResID::GOLD]);
	if (incomeVal != income->getText())
		income->setText(incomeVal);

	heroes->update();

	for (size_t i=0; i<std::min(static_cast<int>(town->creatures.size()), GameConstants::CREATURES_PER_TOWN); i++)
	{
		growth[i]->update();
		available[i]->update();
	}
}

class ArtSlotsTab : public CIntObject
{
public:
	std::shared_ptr<CAnimImage> background;
	std::vector<std::shared_ptr<CArtPlace>> arts;

	ArtSlotsTab()
	{
		OBJECT_CONSTRUCTION;
		background = std::make_shared<CAnimImage>(AnimationPath::builtin("OVSLOT"), 4);
		pos = background->pos;
		for(int i=0; i<9; i++)
			arts.push_back(std::make_shared<CArtPlace>(Point(269+i*48, 66)));
	}
};

class BackpackTab : public CIntObject
{
public:
	std::shared_ptr<CAnimImage> background;
	std::vector<std::shared_ptr<CArtPlace>> arts;
	std::shared_ptr<CButton> btnLeft;
	std::shared_ptr<CButton> btnRight;

	BackpackTab()
	{
		OBJECT_CONSTRUCTION;
		background = std::make_shared<CAnimImage>(AnimationPath::builtin("OVSLOT"), 5);
		pos = background->pos;
		btnLeft = std::make_shared<CButton>(Point(269, 66), AnimationPath::builtin("HSBTNS3"), CButton::tooltip(), 0);
		btnRight = std::make_shared<CButton>(Point(675, 66), AnimationPath::builtin("HSBTNS5"), CButton::tooltip(), 0);
		for(int i=0; i<8; i++)
			arts.push_back(std::make_shared<CArtPlace>(Point(294+i*48, 66)));
	}
};

CHeroItem::CHeroItem(const CGHeroInstance * Hero)
	: hero(Hero)
{
	OBJECT_CONSTRUCTION;

	artTabs.resize(3);
	auto arts1 = std::make_shared<ArtSlotsTab>();
	auto arts2 = std::make_shared<ArtSlotsTab>();
	auto backpack = std::make_shared<BackpackTab>();
	artTabs[0] = arts1;
	artTabs[1] = arts2;
	artTabs[2] = backpack;
	arts1->recActions = SHARE_POS;
	arts2->recActions = SHARE_POS;
	backpack->recActions = SHARE_POS;

	name = std::make_shared<CLabel>(75, 7, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, hero->getNameTranslated());

	//layout is not trivial: MACH4 - catapult - excluded, MISC[x] rearranged
	assert(arts1->arts.size() == 9);
	assert(arts2->arts.size() == 9);

	CArtifactsOfHeroMain::ArtPlaceMap arts =
	{
		{ArtifactPosition::HEAD, arts1->arts[0]},
		{ArtifactPosition::SHOULDERS,arts1->arts[1]},
		{ArtifactPosition::NECK,arts1->arts[2]},
		{ArtifactPosition::RIGHT_HAND,arts1->arts[3]},
		{ArtifactPosition::LEFT_HAND,arts1->arts[4]},
		{ArtifactPosition::TORSO, arts1->arts[5]},
		{ArtifactPosition::RIGHT_RING,arts1->arts[6]},
		{ArtifactPosition::LEFT_RING, arts1->arts[7]},
		{ArtifactPosition::FEET, arts1->arts[8]},

		{ArtifactPosition::MISC1, arts2->arts[0]},
		{ArtifactPosition::MISC2, arts2->arts[1]},
		{ArtifactPosition::MISC3, arts2->arts[2]},
		{ArtifactPosition::MISC4, arts2->arts[3]},
		{ArtifactPosition::MISC5, arts2->arts[4]},
		{ArtifactPosition::MACH1, arts2->arts[5]},
		{ArtifactPosition::MACH2, arts2->arts[6]},
		{ArtifactPosition::MACH3, arts2->arts[7]},
		{ArtifactPosition::SPELLBOOK, arts2->arts[8]}
	};


	heroArts = std::make_shared<CArtifactsOfHeroKingdom>(arts, backpack->arts, backpack->btnLeft, backpack->btnRight);
	heroArts->setHero(hero);

	artsTabs = std::make_shared<CTabbedInt>(std::bind(&CHeroItem::onTabSelected, this, _1));

	artButtons = std::make_shared<CToggleGroup>(0);
	for(size_t it = 0; it<3; it++)
	{
		int stringID[3] = {259, 261, 262};

		std::string hover = LIBRARY->generaltexth->overview[13+it];
		std::string overlay = LIBRARY->generaltexth->overview[8+it];

		auto button = std::make_shared<CToggleButton>(Point(364+(int)it*112, 46), AnimationPath::builtin("OVBUTN3"), CButton::tooltip(hover, overlay), 0);
		button->setTextOverlay(LIBRARY->generaltexth->allTexts[stringID[it]], FONT_SMALL, Colors::YELLOW);
		artButtons->addToggle((int)it, button);
	}
	artButtons->addCallback(std::bind(&CTabbedInt::setActive, artsTabs, _1));
	artButtons->addCallback(std::bind(&CHeroItem::onArtChange, this, _1));
	artButtons->setSelected(0);

	garr = std::make_shared<CGarrisonInt>(Point(6, 78), 4, Point(), hero, nullptr, true, true);

	portrait = std::make_shared<CAnimImage>(AnimationPath::builtin("PortraitsLarge"), hero->getIconIndex(), 0, 5, 6);
	heroArea = std::make_shared<CHeroArea>(5, 6, hero);
	heroArea->addRClickCallback([this](){ ENGINE->windows().createAndPushWindow<CRClickPopupInt>(std::make_shared<CHeroWindow>(hero)); });

	name = std::make_shared<CLabel>(73, 7, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, hero->getNameTranslated());
	artsText = std::make_shared<CLabel>(320, 55, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, LIBRARY->generaltexth->overview[2]);

	for(size_t i=0; i<GameConstants::PRIMARY_SKILLS; i++)
	{
		auto data = std::make_shared<InfoBoxHeroData>(IInfoBoxData::HERO_PRIMARY_SKILL, hero, (int)i);
		heroInfo.push_back(std::make_shared<InfoBox>(Point(78+(int)i*36, 26), InfoBox::POS_DOWN, InfoBox::SIZE_SMALL, data));
	}

	int slots = 8;
	bool isMoreSkillsThanSlots = hero->secSkills.size() > slots;
	for(size_t i=0; i<slots; i++)
	{
		if(isMoreSkillsThanSlots && i == slots - 1)
		{
			Rect r(Point(410+(int)i*36, 5), Point(34, 28));
			heroInfoFull = std::make_shared<CMultiLineLabel>(r, EFonts::FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE, "...");
			heroInfoFullArea = std::make_shared<LRClickableAreaWText>(r, LIBRARY->generaltexth->translate("vcmi.kingdomOverview.secSkillOverflow.hover"), LIBRARY->generaltexth->translate("vcmi.kingdomOverview.secSkillOverflow.help"));
			continue;
		}
		auto data = std::make_shared<InfoBoxHeroData>(IInfoBoxData::HERO_SECONDARY_SKILL, hero, (int)i);
		heroInfo.push_back(std::make_shared<InfoBox>(Point(410+(int)i*36, 5), InfoBox::POS_NONE, InfoBox::SIZE_SMALL, data));
	}

	{
		auto data = std::make_shared<InfoBoxHeroData>(IInfoBoxData::HERO_SPECIAL, hero);
		heroInfo.push_back(std::make_shared<InfoBox>(Point(375, 5), InfoBox::POS_NONE, InfoBox::SIZE_SMALL, data));

		data = std::make_shared<InfoBoxHeroData>(IInfoBoxData::HERO_EXPERIENCE, hero);
		heroInfo.push_back(std::make_shared<InfoBox>(Point(330, 5), InfoBox::POS_INSIDE, InfoBox::SIZE_SMALL, data));

		data = std::make_shared<InfoBoxHeroData>(IInfoBoxData::HERO_MANA, hero);
		heroInfo.push_back(std::make_shared<InfoBox>(Point(280, 5), InfoBox::POS_INSIDE, InfoBox::SIZE_SMALL, data));
	}

	morale = std::make_shared<MoraleLuckBox>(true, Rect(225, 53, 30, 22), true);
	luck = std::make_shared<MoraleLuckBox>(false, Rect(225, 28, 30, 22), true);

	morale->set(hero);
	luck->set(hero);

	redraw();
}


void CHeroItem::updateGarrisons()
{
	garr->recreateSlots();
}

bool CHeroItem::holdsGarrison(const CArmedInstance * army)
{
	return hero == army;
}

std::shared_ptr<CIntObject> CHeroItem::onTabSelected(size_t index)
{
	return artTabs.at(index);
}

void CHeroItem::onArtChange(int tabIndex)
{
	if(isActive())
		redraw();
}

void CHeroItem::redraw()
{
	for(int i = 0; i<3; i++)
		artTabs.at(i)->setEnabled(artButtons->getSelected() == i);
	CIntObject::redraw();
}
