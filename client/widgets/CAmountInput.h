/*
 * CAmountInput.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CTextInput.h"

class CSlider;

/// DMB (S016; K 11-02: "would like the ability to type in the send or trade fields"; 11-05: "Typed amounts should be anywhere an amount is
/// specified, including for creatures. Where the amount doesn't line up with the creature, the amount should be rounded down to the lower
/// creature purchase number. The player can then click the right arrow to see what the round up result would look like....assuming they have
/// the coin"; 11-07: "should be able to do both, but either way it shouldn't allow a player to hit enter and get a number they can't afford
/// after doing so").
///
/// A typed amount bound to a window's slider. Typing moves the slider to the step the text asks for, rounded down and clamped to the
/// slider's range; the owner keeps that range at what the player can pay and the window can give, so Enter, which acts on the slider's
/// value, never buys more. Moving the slider (or its arrows, Max, None) writes the slider's number back into the field. Where the owner allows
/// it, a number ending in "g" is an amount of gold, and the owner's conversion rounds it down to whole steps; the text stays as typed while
/// the window shows the result, and becomes the step count when the field loses focus.
///
/// The field does not take focus when the window opens, so the window's own letter keys (M for max, U for upgrade) work until it is clicked.
/// With the field focused, Escape restores the slider's number and leaves the field; Enter is left to the window's accept button.
class CAmountInput final : public CTextInput
{
public:
	/// typed number, and whether it ended in "g" -> slider position, before clamping
	using ToPosition = std::function<int(int64_t value, bool gold)>;
	/// slider position -> the number the field shows for it
	using ToShown = std::function<int64_t(int position)>;

	CAmountInput(const Rect & pos, EFonts font, const std::shared_ptr<CSlider> & slider, const ToPosition & toPosition, const ToShown & toShown, bool allowGold);

	/// the owner's slider callback calls this: the field follows the slider unless the move came from typing
	void sliderMoved(int position);

	bool captureThisKey(EShortcut key) override;
	void keyPressed(EShortcut key) override;

private:
	std::weak_ptr<CSlider> slider;
	ToPosition toPosition;
	ToShown toShown;
	bool allowGold;
	bool applyingTyped = false;

	void typed(const std::string & text);
	void showSlider();
	void onFocusLost() override;
	static void amountFilter(std::string & text, const std::string & oldText, bool allowGold);
};
