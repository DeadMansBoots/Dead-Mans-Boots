/*
 * CTransferResources.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CMarketBase.h"

class CTextInput;

class CTransferResources : public CResourcesSelling, public CMarketSlider, public CMarketTraderText
{
public:
	CTransferResources(const IMarket * market, const CGHeroInstance * hero);
	void deselect() override;
	void makeDeal() override;

private:
	CMarketBase::MarketShowcasesParams getShowcasesParams() const override;
	void highlightingChanged() override;
	std::string getTraderText() override;

	/// DMB (K, November 2nd: "would like the ability to type in the send or trade fields"): the quantity under the bid showcase is typed as well as slid. The number the
	/// player types moves the slider (clamped to what the player owns); the slider's moves are written back into the field.
	std::shared_ptr<CTextInput> amountInput;
	bool syncingAmount = false;
	void placeAmountInput();
	void syncAmountInput();
	void amountTyped(const std::string & text);
};
