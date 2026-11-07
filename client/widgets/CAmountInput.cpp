/*
 * CAmountInput.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CAmountInput.h"

#include "Slider.h"
#include "../gui/Shortcut.h"

CAmountInput::CAmountInput(const Rect & pos, EFonts font, const std::shared_ptr<CSlider> & slider_, const ToPosition & toPosition_, const ToShown & toShown_, bool allowGold_)
	: CTextInput(pos, font, ETextAlignment::CENTER, false)
	, slider(slider_)
	, toPosition(toPosition_)
	, toShown(toShown_)
	, allowGold(allowGold_)
{
	onTextFiltering = [allowGold_](std::string & text, const std::string & oldText){ amountFilter(text, oldText, allowGold_); };
	setCallback([this](const std::string & text){ typed(text); });
	showSlider();
}

void CAmountInput::amountFilter(std::string & text, const std::string & oldText, bool allowGold)
{
	// digits only, at most nine of them, and where gold is allowed one "g" at the end
	bool gold = allowGold && !text.empty() && (text.back() == 'g' || text.back() == 'G');
	std::string digits;
	for(char c : text)
		if(std::isdigit(static_cast<unsigned char>(c)) && digits.size() < 9)
			digits += c;
	size_t firstNonZero = digits.find_first_not_of('0');
	digits = firstNonZero == std::string::npos ? (digits.empty() ? "" : "0") : digits.substr(firstNonZero);
	text = digits + (gold ? "g" : "");
}

void CAmountInput::typed(const std::string & text)
{
	auto s = slider.lock();
	if(!s)
		return;

	bool gold = allowGold && !text.empty() && text.back() == 'g';
	std::string digits = gold ? text.substr(0, text.size() - 1) : text;
	int64_t value = digits.empty() ? 0 : std::stoll(digits);

	int position = std::clamp(toPosition(value, gold), 0, std::max(0, s->getAmount() - s->getCapacity()));
	applyingTyped = true;
	s->scrollTo(position);
	applyingTyped = false;

	// a count that cannot all be had shows the count that can (the window's limit, as the slider's Max gives it); a typed amount of gold
	// stays as typed, and the window shows the creatures it buys
	if(!gold && !digits.empty() && toShown(position) != value)
		setText(std::to_string(toShown(position)));
}

void CAmountInput::showSlider()
{
	if(auto s = slider.lock())
		setText(std::to_string(toShown(s->getValue())));
}

void CAmountInput::sliderMoved(int position)
{
	if(!applyingTyped)
		setText(std::to_string(toShown(position)));
}

void CAmountInput::onFocusGot()
{
	CTextInput::onFocusGot();
	setText("");
	updateLabel();
}

void CAmountInput::onFocusLost()
{
	showSlider();
	CTextInput::onFocusLost();
}

bool CAmountInput::captureThisKey(EShortcut key)
{
	return hasFocus() && key == EShortcut::GLOBAL_CANCEL;
}

void CAmountInput::keyPressed(EShortcut key)
{
	if(hasFocus() && key == EShortcut::GLOBAL_CANCEL)
	{
		removeFocus();   // onFocusLost writes the slider's number back
		return;
	}
	CTextInput::keyPressed(key);
}
