/*
 * CreaturePurchaseCard.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CreaturePurchaseCard.h"

#include "CHeroWindow.h"
#include "QuickRecruitmentWindow.h"
#include "CCreatureWindow.h"

#include "../GameEngine.h"
#include "../gui/Shortcut.h"
#include "../gui/TextAlignment.h"
#include "../gui/WindowHandler.h"
#include "../widgets/Buttons.h"
#include "../widgets/Slider.h"
#include "../widgets/CAmountInput.h"
#include "InfoWindows.h"
#include "../widgets/TextControls.h"
#include "../widgets/CreatureCostBox.h"

#include "../../lib/CCreatureHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/texts/CGeneralTextHandler.h"

void CreaturePurchaseCard::initButtons()
{
	initMaxButton();
	initMinButton();
	initCreatureSwitcherButton();
}

void CreaturePurchaseCard::initMaxButton()
{
	maxButton = std::make_shared<CButton>(Point(pos.x + 52, pos.y + 180), AnimationPath::builtin("QuickRecruitmentWindow/QuickRecruitmentAllButton.def"), CButton::tooltip(), std::bind(&CSlider::scrollToMax,slider), EShortcut::RECRUITMENT_MAX);
}

void CreaturePurchaseCard::initMinButton()
{
	minButton = std::make_shared<CButton>(Point(pos.x, pos.y + 180), AnimationPath::builtin("QuickRecruitmentWindow/QuickRecruitmentNoneButton.def"), CButton::tooltip(), std::bind(&CSlider::scrollToMin,slider), EShortcut::RECRUITMENT_MIN);
}

void CreaturePurchaseCard::initCreatureSwitcherButton()
{
	creatureSwitcher = std::make_shared<CButton>(Point(pos.x + 18, pos.y-37), AnimationPath::builtin("iDv6432.def"), CButton::tooltip(), [&](){ switchCreatureLevel(); }, EShortcut::RECRUITMENT_SWITCH_LEVEL);
}

void CreaturePurchaseCard::switchCreatureLevel()
{
	OBJECT_CONSTRUCTION;
	auto index = vstd::find_pos(upgradesID, creatureOnTheCard->getId());
	auto nextCreatureId = vstd::circularAt(upgradesID, ++index);
	creatureOnTheCard = nextCreatureId.toCreature();
	picture = std::make_shared<CCreaturePic>(picture->pos.x - pos.x, picture->pos.y - pos.y, creatureOnTheCard);
	creatureClickArea = std::make_shared<CCreatureClickArea>(Point(picture->pos.x - pos.x, picture->pos.y - pos.y), picture, creatureOnTheCard);
	parent->updateAllSliders();
	cost->set(creatureOnTheCard->getFullRecruitCost() * slider->getValue());
}

void CreaturePurchaseCard::initAmountInfo()
{
	availableAmount = std::make_shared<CLabel>(pos.x + 25, pos.y + 146, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW);
	updateAmountInfo(0);
}

void CreaturePurchaseCard::updateAmountInfo(int value)
{
	availableAmount->setText(std::to_string(maxAmount-value));
	if(purchaseAmount)
		purchaseAmount->sliderMoved(value);
}

void CreaturePurchaseCard::initSlider()
{
	slider = std::make_shared<CSlider>(Point(pos.x, pos.y + 158), 102, std::bind(&CreaturePurchaseCard::sliderMoved, this, _1), 0, maxAmount, 0, Orientation::HORIZONTAL);
	// DMB (S016): where the bought number was shown, it is typed too: a number of creatures, or gold ending in g, rounded down to whole
	// creatures and clamped to this card's slider, whose range the window keeps at what the gold left over the other cards pays for
	purchaseAmount = std::make_shared<CAmountInput>(Rect(pos.x + 52, pos.y + 137, 48, 18), FONT_SMALL, slider,
		[this](int64_t value, bool gold) -> int
		{
			int64_t each = creatureOnTheCard->getFullRecruitCost()[EGameResID::GOLD];
			int64_t count = gold ? (each > 0 ? value / each : std::numeric_limits<int>::max()) : value;
			return static_cast<int>(std::min<int64_t>(count, std::numeric_limits<int>::max()));
		},
		[](int position) -> int64_t { return position; }, true);
	purchaseAmount->setPopupCallback([](){ CRClickPopup::createAndPush(LIBRARY->generaltexth->translate("vcmi.recruitWindow.typedAmount")); });
}

void CreaturePurchaseCard::initCostBox()
{
	cost = std::make_shared<CreatureCostBox>(Rect(pos.x+2, pos.y + 194, 97, 74), "");
	cost->createItems(creatureOnTheCard->getFullRecruitCost());
}

void CreaturePurchaseCard::sliderMoved(int to)
{
	updateAmountInfo(to);
	cost->set(creatureOnTheCard->getFullRecruitCost() * to);
	parent->updateAllSliders();
}

CreaturePurchaseCard::CreaturePurchaseCard(const std::vector<CreatureID> & creaturesID, Point position, int creaturesMaxAmount, QuickRecruitmentWindow * parents)
	: upgradesID(creaturesID),
	parent(parents),
	maxAmount(creaturesMaxAmount)
{
	creatureOnTheCard = upgradesID.back().toCreature();
	moveTo(Point(position.x, position.y));
	initView();
}

void CreaturePurchaseCard::initView()
{
	picture = std::make_shared<CCreaturePic>(pos.x, pos.y, creatureOnTheCard);
	background = std::make_shared<CPicture>(ImagePath::builtin("QuickRecruitmentWindow/CreaturePurchaseCard.png"), pos.x-4, pos.y-50);
	creatureClickArea = std::make_shared<CCreatureClickArea>(Point(pos.x, pos.y), picture, creatureOnTheCard);

	initAmountInfo();
	initSlider();
	initButtons(); // order important! buttons need slider!
	initCostBox();
}

CreaturePurchaseCard::CCreatureClickArea::CCreatureClickArea(const Point & position, const std::shared_ptr<CCreaturePic> creaturePic, const CCreature * creatureOnTheCard)
	: CIntObject(SHOW_POPUP),
	creatureOnTheCard(creatureOnTheCard)
{
	pos.x += position.x;
	pos.y += position.y;
	pos.w = CREATURE_WIDTH;
	pos.h = CREATURE_HEIGHT;
}

void CreaturePurchaseCard::CCreatureClickArea::showPopupWindow(const Point & cursorPosition)
{
	ENGINE->windows().createAndPushWindow<CStackWindow>(creatureOnTheCard, true);
}
