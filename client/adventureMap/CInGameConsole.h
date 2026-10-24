/*
 * CInGameConsole.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../gui/CIntObject.h"

#include "../../lib/DmbChatRouting.h"

class CInGameConsole : public CIntObject
{
private:
	struct TextState
	{
		std::string text;
		uint32_t timeOnScreen;
		EChatChannel channel = EChatChannel::ALL; // DMB: the colour of the line
	};

	/// DMB (K, October 21st: a team chat mode): the channel the next line goes to; Tab with nothing typed yet cycles it, a prefix a: t: s: picks it for one line and keeps it
	EChatChannel currentChannel = EChatChannel::ALL;

	static std::string channelLabel(EChatChannel channel);
	std::string shownEnteredText() const;

	/// Currently visible texts in the overlay
	std::vector<TextState> texts;

	/// previously entered texts, for up/down arrows to work
	std::vector<std::string> previouslyEntered;

	/// displayed entry from previouslyEntered - if none it's -1
	int prevEntDisp;

	/// timeout for new texts (in ms)
	static constexpr int defaultTimeout = 10000;

	/// how many texts can be displayed simultaneously
	static constexpr int maxDisplayedTexts = 10;

	std::weak_ptr<IStatusBar> currentStatusBar;
	std::string enteredText;

	/// Returns true if console is active and player is currently entering text
	bool isEnteringText() const;

	void showRecentChatHistory();
	void addMessageSilent(const std::string & timeFormatted, const std::string & senderName, const std::string & messageText, EChatChannel channel = EChatChannel::ALL);
public:
	void addMessage(const std::string & timeFormatted, const std::string & senderName, const std::string & messageText, EChatChannel channel = EChatChannel::ALL);

	void tick(uint32_t msPassed) override;
	void show(Canvas & to) override;
	void showAll(Canvas & to) override;
	void keyPressed(EShortcut key) override;
	void textInputted(const std::string & enteredText) override;
	void textEdited(const std::string & enteredText) override;
	bool captureThisKey(EShortcut key) override;

	void startEnteringText();
	void endEnteringText(bool processEnteredText);
	/// DMB: whether a chat line is being typed (the auto-combat retreat question ends it, so its keys reach the question)
	bool isTyping() const { return isEnteringText(); }
	void refreshEnteredText();

	CInGameConsole();
};
