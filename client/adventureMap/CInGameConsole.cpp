/*
 * CInGameConsole.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "CInGameConsole.h"

#include "../CPlayerInterface.h"
#include "../CServerHandler.h"
#include "../GameChatHandler.h"
#include "../ClientCommandManager.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../gui/WindowHandler.h"
#include "../gui/Shortcut.h"
#include "../gui/TextAlignment.h"
#include "../media/ISoundPlayer.h"
#include "../render/Colors.h"
#include "../render/Canvas.h"
#include "../render/IScreenHandler.h"
#include "../adventureMap/AdventureMapInterface.h"
#include "../windows/CMessage.h"

#include "../../lib/CConfigHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/CThreadHelper.h"
#include "../../lib/texts/MetaString.h"
#include "../../lib/texts/TextOperations.h"

CInGameConsole::CInGameConsole()
	: CIntObject(KEYBOARD | TIME | TEXTINPUT)
	, prevEntDisp(-1)
{
	setRedrawParent(true);
}

void CInGameConsole::showAll(Canvas & to)
{
	show(to);
}

void CInGameConsole::show(Canvas & to)
{
	if (GAME->interface()->cingconsole != this)
		return;

	int number = 0;

	for(auto & text : texts)
	{
		Point leftBottomCorner(0, pos.h);
		Point textPosition(leftBottomCorner.x + 50, leftBottomCorner.y - texts.size() * 20 - 80 + number * 20);

		// DMB: each channel has its own colour: all green as before, team cyan, spectators grey
		const ColorRGBA colour = text.channel == EChatChannel::TEAM ? ColorRGBA(100, 210, 255) : text.channel == EChatChannel::SPECTATORS ? ColorRGBA(200, 200, 200) : Colors::GREEN;
		to.drawText(pos.topLeft() + textPosition, FONT_MEDIUM, colour, ETextAlignment::TOPLEFT, text.text);
		number++;
	}
}

void CInGameConsole::tick(uint32_t msPassed)
{
	// Check whether text input is active - we want to keep recent messages visible during this period
	if(isEnteringText())
		return;

	size_t sizeBefore = texts.size();

	for(auto & text : texts)
		text.timeOnScreen += msPassed;

	vstd::erase_if(
				texts,
				[&](const auto & value)
	{
		return value.timeOnScreen > defaultTimeout;
	}
	);

	if(sizeBefore != texts.size())
		ENGINE->windows().totalRedraw(); // FIXME: ingame console has no parent widget set
}

std::string CInGameConsole::channelLabel(EChatChannel channel)
{
	switch(channel)
	{
		case EChatChannel::TEAM:
			return LIBRARY->generaltexth->translate("vcmi.chat.channel.team");
		case EChatChannel::SPECTATORS:
			return LIBRARY->generaltexth->translate("vcmi.chat.channel.spectators");
		default:
			return LIBRARY->generaltexth->translate("vcmi.chat.channel.all");
	}
}

std::string CInGameConsole::shownEnteredText() const
{
	// the line being typed names its channel, so the player always knows who will read it; all is the plain case and needs no label
	return currentChannel == EChatChannel::ALL ? enteredText : "[" + channelLabel(currentChannel) + "] " + enteredText;
}

void CInGameConsole::addMessageSilent(const std::string & timeFormatted, const std::string & senderName, const std::string & messageText, EChatChannel channel)
{
	MetaString formatted = MetaString::createFromRawString("[%s] %s: %s");
	formatted.replaceRawString(timeFormatted);
	formatted.replaceRawString(channel == EChatChannel::ALL ? senderName : "[" + channelLabel(channel) + "] " + senderName);
	formatted.replaceRawString(messageText);

	// Maximum width for a text line is limited by:
	// 1) width of adventure map terrain area, for when in-game console is on top of advmap
	// 2) width of castle/battle window (fixed to 800) when this window is open
	// 3) arbitrary selected left and right margins
	int maxWidth = std::min( 800, adventureInt->terrainAreaPixels().w) - 100;

	auto splitText = CMessage::breakText(formatted.toString(), maxWidth, FONT_MEDIUM);

	for(const auto & entry : splitText)
		texts.push_back({entry, 0, channel});

	while(texts.size() > maxDisplayedTexts)
		texts.erase(texts.begin());
}

void CInGameConsole::addMessage(const std::string & timeFormatted, const std::string & senderName, const std::string & messageText, EChatChannel channel)
{
	addMessageSilent(timeFormatted, senderName, messageText, channel);

	ENGINE->windows().totalRedraw(); // FIXME: ingame console has no parent widget set

	int volume = ENGINE->sound().getVolume();
	if(volume == 0)
		ENGINE->sound().setVolume(settings["general"]["sound"].Integer());
	int handle = ENGINE->sound().playSound(AudioPath::builtin("CHAT"));
	if(volume == 0)
		ENGINE->sound().setCallback(handle, [&]() { if(!ENGINE->screenHandler().hasFocus()) ENGINE->sound().setVolume(0); });
}

bool CInGameConsole::captureThisKey(EShortcut key)
{
	if (!isEnteringText())
		return false;

	switch (key)
	{
		case EShortcut::GLOBAL_ACCEPT:
		case EShortcut::GLOBAL_CANCEL:
		case EShortcut::GAME_ACTIVATE_CONSOLE:
		case EShortcut::GLOBAL_BACKSPACE:
		case EShortcut::MOVE_UP:
		case EShortcut::MOVE_DOWN:
			return true;

		default:
			return false;
	}
}

void CInGameConsole::keyPressed (EShortcut key)
{
	if (GAME->interface()->cingconsole != this)
		return;

	if(!isEnteringText() && key != EShortcut::GAME_ACTIVATE_CONSOLE)
		return; //because user is not entering any text

	switch(key)
	{
	case EShortcut::GLOBAL_CANCEL:
		if(!enteredText.empty())
			endEnteringText(false);
		break;

	case EShortcut::GAME_ACTIVATE_CONSOLE:
		if(enteredText == "_")
		{
			// DMB: Tab with nothing typed yet switches the channel (all, team, spectators) instead of closing the line, as OpenRA's Tab does; Escape closes it
			currentChannel = static_cast<EChatChannel>((static_cast<int>(currentChannel) + 1) % 3);
			refreshEnteredText();
		}
		else if(!enteredText.empty())
			endEnteringText(false);
		else
			startEnteringText();
		break;

	case EShortcut::GLOBAL_ACCEPT:
		{
			if(!enteredText.empty())
			{
				bool anyTextExceptCaret = enteredText.size() > 1;
				endEnteringText(anyTextExceptCaret);
			}
			break;
		}
	case EShortcut::GLOBAL_BACKSPACE:
		{
			if(enteredText.size() > 1)
			{
				TextOperations::trimRightUnicode(enteredText,2);
				enteredText += '_';
				refreshEnteredText();
			}
			break;
		}
	case EShortcut::MOVE_UP:
		{
			if(previouslyEntered.empty())
				break;

			if(prevEntDisp == -1)
			{
				prevEntDisp = static_cast<int>(previouslyEntered.size() - 1);
				enteredText = previouslyEntered[prevEntDisp] + "_";
				refreshEnteredText();
			}
			else if( prevEntDisp > 0)
			{
				--prevEntDisp;
				enteredText = previouslyEntered[prevEntDisp] + "_";
				refreshEnteredText();
			}
			break;
		}
	case EShortcut::MOVE_DOWN:
		{
			if(prevEntDisp != -1 && prevEntDisp+1 < previouslyEntered.size())
			{
				++prevEntDisp;
				enteredText = previouslyEntered[prevEntDisp] + "_";
				refreshEnteredText();
			}
			else if(prevEntDisp+1 == previouslyEntered.size()) //useful feature
			{
				prevEntDisp = -1;
				enteredText = "_";
				refreshEnteredText();
			}
			break;
		}
	}
}

void CInGameConsole::textInputted(const std::string & inputtedText)
{
	if (GAME->interface()->cingconsole != this)
		return;

	if(!isEnteringText())
		return;

	enteredText.resize(enteredText.size()-1);

	enteredText += inputtedText;
	enteredText += "_";

	refreshEnteredText();
}

void CInGameConsole::textEdited(const std::string & inputtedText)
{
 //do nothing here
}

void CInGameConsole::showRecentChatHistory()
{
	auto const & history = GAME->server().getGameChat().getChatHistory();

	texts.clear();

	int entriesToShow = std::min<int>(maxDisplayedTexts, history.size());
	int firstEntryToShow = history.size() - entriesToShow;

	for (int i = firstEntryToShow; i < history.size(); ++i)
		addMessageSilent(history[i].dateFormatted, history[i].senderName, history[i].messageText, history[i].channel);

	ENGINE->windows().totalRedraw();
}

void CInGameConsole::startEnteringText()
{
	if (!isActive())
		return;

	if(isEnteringText())
	{
		// force-reset text input to re-show on-screen keyboard
		ENGINE->statusbar()->setEnteringMode(false);
		ENGINE->statusbar()->setEnteringMode(true);
		ENGINE->statusbar()->setEnteredText(shownEnteredText());
		return;
	}
		
	assert(currentStatusBar.expired());//effectively, nullptr check

	currentStatusBar = ENGINE->statusbar();

	enteredText = "_";

	ENGINE->statusbar()->setEnteringMode(true);
	ENGINE->statusbar()->setEnteredText(shownEnteredText());

	showRecentChatHistory();
}

void CInGameConsole::endEnteringText(bool processEnteredText)
{
	prevEntDisp = -1;
	if(processEnteredText)
	{
		std::string txt = enteredText.substr(0, enteredText.size()-1);
		previouslyEntered.push_back(txt);

		if(txt.at(0) == '/')
		{
			//some commands like gosolo don't work when executed from GUI thread
			auto threadFunction = [=]()
			{
				setThreadName("processCommand");
				ClientCommandManager commandController;
				commandController.processCommand(txt.substr(1), true);
			};

			std::thread clientCommandThread(threadFunction);
			clientCommandThread.detach();
		}
		else
		{
			// DMB: a prefix a: (all), t: (team) or s: (spectators) names the channel of this line and the lines after it
			if(txt.size() >= 2 && txt[1] == ':')
			{
				const char prefix = static_cast<char>(std::tolower(static_cast<unsigned char>(txt[0])));
				if(prefix == 'a' || prefix == 't' || prefix == 's')
				{
					currentChannel = prefix == 'a' ? EChatChannel::ALL : prefix == 't' ? EChatChannel::TEAM : EChatChannel::SPECTATORS;
					txt = txt.substr(2);
					while(!txt.empty() && txt.front() == ' ')
						txt.erase(txt.begin());
				}
			}
			if(!txt.empty())
				GAME->server().getGameChat().sendMessageGameplay(txt, currentChannel);
		}
	}
	enteredText.clear();

	auto statusbar = currentStatusBar.lock();
	assert(statusbar);

	if (statusbar)
		statusbar->setEnteringMode(false);

	currentStatusBar.reset();
}

void CInGameConsole::refreshEnteredText()
{
	auto statusbar = currentStatusBar.lock();
	assert(statusbar);

	if (statusbar)
		statusbar->setEnteredText(shownEnteredText());
}

bool CInGameConsole::isEnteringText() const
{
	return !enteredText.empty();
}
