/*
 * CTransferResources.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "CTransferResources.h"

#include "../../GameEngine.h"
#include "../../GameInstance.h"
#include "../../gui/Shortcut.h"
#include "../../widgets/Buttons.h"
#include "../../widgets/TextControls.h"
#include "../../widgets/CTextInput.h"

#include "../../CPlayerInterface.h"

#include "../../../lib/callback/CCallback.h"
#include "../../../lib/texts/CGeneralTextHandler.h"
#include "../../../lib/mapObjects/IMarket.h"
#include "../../../lib/texts/MetaString.h"
#include "../../../lib/GameLibrary.h"

namespace
{
/// the quantity field: a click empties it so the digits typed are the amount (the slider's number is written back when the field is left)
class AmountInput final : public CTextInput
{
public:
	std::function<void()> whenLeft;
	AmountInput(const Rect & area, EFonts font, ETextAlignment alignment) : CTextInput(area, font, alignment, false) {}

protected:
	void onFocusGot() override
	{
		CTextInput::onFocusGot();
		setText("");
		updateLabel();
	}
	void onFocusLost() override
	{
		CTextInput::onFocusLost();
		if(whenLeft)
			whenLeft();
	}
};
}

CTransferResources::CTransferResources(const IMarket * market, const CGHeroInstance * hero)
	: CMarketBase(market, hero)
	, CResourcesSelling([this](const std::shared_ptr<CTradeableItem> & heroSlot){CTransferResources::onSlotClickPressed(heroSlot, bidTradePanel);})
	, CMarketSlider([this](int newVal){CMarketSlider::onOfferSliderMoved(newVal); syncAmountInput();})
	, CMarketTraderText(Point(28, 48))
{
	OBJECT_CONSTRUCTION;

	labels.emplace_back(std::make_shared<CLabel>(titlePos.x, titlePos.y, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, LIBRARY->generaltexth->allTexts[158]));
	labels.emplace_back(std::make_shared<CLabel>(445, 56, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, LIBRARY->generaltexth->allTexts[169]));
	deal = std::make_shared<CButton>(dealButtonPosWithSlider, AnimationPath::builtin("TPMRKB.DEF"),
		LIBRARY->generaltexth->zelp[595], [this](){CTransferResources::makeDeal();}, EShortcut::MARKET_DEAL);

	// Player's resources
	assert(bidTradePanel);
	bidTradePanel->moveTo(pos.topLeft() + Point(40, 183));

	// Players panel
	offerTradePanel = std::make_shared<PlayersPanel>([this](const std::shared_ptr<CTradeableItem> & heroSlot)
		{
			CTransferResources::onSlotClickPressed(heroSlot, offerTradePanel);
		});
	offerTradePanel->moveTo(pos.topLeft() + Point(333, 84));

	CMarketBase::update();
	CTransferResources::deselect();
	placeAmountInput();
}

void CTransferResources::placeAmountInput()
{
	OBJECT_CONSTRUCTION;
	// over the number under the bid showcase (the label is centred 35 right and 55 down from the slot's corner)
	const Point centre = bidTradePanel->showcaseSlot->pos.topLeft() + Point(35, 55);
	const Rect area(centre - pos.topLeft() - Point(30, 9), Point(60, 18));
	auto input = std::make_shared<AmountInput>(area, FONT_SMALL, ETextAlignment::CENTER);
	input->whenLeft = [this](){ syncAmountInput(); redraw(); };
	input->setCallback([this](const std::string & text){ amountTyped(text); });
	amountInput = input;
	syncAmountInput();
}

void CTransferResources::syncAmountInput()
{
	if(!amountInput || syncingAmount)
		return;
	if(bidTradePanel->isHighlighted() && offerTradePanel->isHighlighted())
	{
		amountInput->enable();
		amountInput->setFilterNumber(0, std::max(1, offerSlider->getAmount()));
		amountInput->setText(std::to_string(offerSlider->getValue()));
	}
	else
		amountInput->disable();
}

void CTransferResources::amountTyped(const std::string & text)
{
	if(syncingAmount || !bidTradePanel->isHighlighted() || !offerTradePanel->isHighlighted())
		return;
	int value = 0;
	try
	{
		value = text.empty() ? 0 : std::stoi(text);
	}
	catch(const std::exception &)
	{
		value = offerSlider->getAmount(); // more digits than a number holds: all of it
	}
	syncingAmount = true; // the slider's own callback must not rewrite the field under the typing hand
	offerSlider->scrollTo(std::min(std::max(value, 0), offerSlider->getAmount()));
	syncingAmount = false;
	updateShowcases();
	redraw();
}

void CTransferResources::deselect()
{
	CMarketBase::deselect();
	CMarketSlider::deselect();
	CMarketTraderText::deselect();
}

void CTransferResources::makeDeal()
{
	if(auto toTrade = offerSlider->getValue(); toTrade != 0)
	{
		GAME->interface()->cb->trade(market->getObjInstanceID(), EMarketMode::RESOURCE_PLAYER, GameResID(bidTradePanel->getHighlightedItemId()),
			PlayerColor(offerTradePanel->getHighlightedItemId()), toTrade, hero);
		CMarketTraderText::makeDeal();
		deselect();
	}
}

CMarketBase::MarketShowcasesParams CTransferResources::getShowcasesParams() const
{
	if(bidTradePanel->isHighlighted() && offerTradePanel->isHighlighted())
		return MarketShowcasesParams
		{
			ShowcaseParams {amountInput ? std::string() : std::to_string(offerSlider->getValue()), bidTradePanel->getHighlightedItemId()}, // the quantity field draws the number
			ShowcaseParams {LIBRARY->generaltexth->capColors[offerTradePanel->getHighlightedItemId()], offerTradePanel->getHighlightedItemId()}
		};
	else
		return MarketShowcasesParams {std::nullopt, std::nullopt};
}

void CTransferResources::highlightingChanged()
{
	if(bidTradePanel->isHighlighted() && offerTradePanel->isHighlighted())
	{
		offerSlider->setAmount(GAME->interface()->cb->getResourceAmount(GameResID(bidTradePanel->getHighlightedItemId())));
		offerSlider->scrollTo(0);
		offerSlider->block(false);
		maxAmount->block(false);
		deal->block(!GAME->interface()->makingTurn);
	}
	CMarketBase::highlightingChanged();
	CMarketTraderText::highlightingChanged();
	syncAmountInput();
}

std::string CTransferResources::getTraderText()
{
	if(bidTradePanel->isHighlighted() && offerTradePanel->isHighlighted())
	{
		MetaString message = MetaString::createFromTextID("core.genrltxt.165");
		message.replaceName(GameResID(bidTradePanel->getHighlightedItemId()));
		message.replaceName(PlayerColor(offerTradePanel->getHighlightedItemId()));
		return message.toString();
	}
	else
	{
		return madeTransaction ? LIBRARY->generaltexth->allTexts[166] : LIBRARY->generaltexth->allTexts[167];
	}
}
