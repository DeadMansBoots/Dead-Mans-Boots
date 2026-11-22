/*
 * AISettingsWindow.h, part of VCMI engine (DMB addition)
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../windows/CWindowObject.h"
#include "../../lib/constants/EntityIdentifiers.h"

VCMI_LIB_NAMESPACE_BEGIN
struct PlayerSettings;
struct Handicap;
VCMI_LIB_NAMESPACE_END

class CLabel;
class CTextInput;
class CMultiLineLabel;
class CAnimImage;
class CButton;
class LRClickableArea;
class TransparentFilledRectangle;
class FilledTexturePlayerColored;

/// DMB (S002, K, November 7th: "that ai config settings let me put any mix of any ai at any difficulty in any slot"; November 5th: "a full 'AI Settings' menu per slot"): one window per
/// computer seat in the lobby, opened by the host from the seat's name. The adventure AI and the battle AI of the seat are picked from the lists of what is installed, the difficulty
/// from the five chess pieces or "as the map". Every click is sent to the server at once (IServerAPI::setPlayerAI) and the window shows what the lobby state says, so the host and
/// the guests see the same. Each seat is independent: nothing is greyed because of another seat.
class AISettingsWindow final : public CWindowObject
{
	struct Choice
	{
		std::string name;
		std::shared_ptr<TransparentFilledRectangle> mark;
		std::shared_ptr<CLabel> label;
		std::shared_ptr<LRClickableArea> area;
	};

	PlayerColor color;
	std::shared_ptr<FilledTexturePlayerColored> backgroundTexture;
	std::vector<std::shared_ptr<CLabel>> labels;
	std::vector<std::shared_ptr<CAnimImage>> anim;
	std::vector<Choice> adventureChoices;
	std::vector<Choice> battleChoices;
	/// index 0 is "as the map", 1 to 5 are the tiers Pawn to King
	std::vector<std::shared_ptr<TransparentFilledRectangle>> tierMarks;
	std::vector<std::shared_ptr<CButton>> tierButtons; // the chess pieces; "as the map" and the reset are in `buttons` (S301)
	std::shared_ptr<CMultiLineLabel> description;
	/// S073: "Show this AI's thoughts in the chat", for an adventure AI that can write them
	std::shared_ptr<TransparentFilledRectangle> thoughtsBox;
	std::shared_ptr<TransparentFilledRectangle> thoughtsTick;
	std::shared_ptr<CLabel> thoughtsLabel;
	std::shared_ptr<LRClickableArea> thoughtsArea;
	std::vector<std::shared_ptr<CButton>> buttons;
	/// S424 (K 11-21: "damn you guys didn't even get color picking per seat in here?!"; S376 B: "color menu is everything i wants"): the
	/// Seat Color list, every colour, the seat's own framed, one another seat has greyed and marked; a pick of a held colour swaps the two
	struct ColourCell
	{
		std::shared_ptr<TransparentFilledRectangle> swatch;
		std::shared_ptr<TransparentFilledRectangle> mark;
		std::shared_ptr<TransparentFilledRectangle> heldDot;
		std::shared_ptr<CLabel> label;
		std::shared_ptr<LRClickableArea> area;
	};
	std::vector<ColourCell> colourCells;
	std::shared_ptr<CLabel> colourTitle;
	/// the widgets a human seat's window does not show (its AI, tier, thoughts and reset)
	std::vector<std::shared_ptr<CIntObject>> computerOnly;
	/// S380 (K 11-19: "Yes, build all of them in this order: your four ticked ones first"): the seat's movement, spell point and army
	/// percents (typeable, 0 to 200) and a starting artifact, sent with the OK button
	std::shared_ptr<CTextInput> movementInput;
	std::shared_ptr<CTextInput> manaInput;
	std::shared_ptr<CTextInput> armyInput;
	std::shared_ptr<CLabel> artifactLabel;
	std::shared_ptr<LRClickableArea> artifactArea;
	std::vector<std::shared_ptr<CIntObject>> bonusParts;
	ArtifactID chosenArtifact = ArtifactID::NONE;
	/// S380 (the rest of the S330 rows, S396 row 29): the More Bonuses window edits this copy of the seat's handicap; OK in either window
	/// sends it with the four fields above
	std::shared_ptr<Handicap> pendingBonuses;
	std::shared_ptr<CLabel> moreLabel;
	std::shared_ptr<LRClickableArea> moreArea;
	void chooseArtifact();
	void sendBonuses() const;

	const PlayerSettings * seat() const;
	void buildChoices(bool battleList, std::vector<Choice> & into, int x);
	void send(const std::string & adventureAI, const std::string & battleAI, int tier) const;
	/// the same, with the thoughts flag the seat has now
	void sendKeepingThoughts(const std::string & adventureAI, const std::string & battleAI, int tier) const;
	bool adventureAICanThink(const std::string & name) const;
	void notFocusedClick() override;

public:
	explicit AISettingsWindow(PlayerColor seatColor);

	/// shows the seat as the lobby state holds it; closes the window when the seat is no computer's any more
	void refresh();

	/// "Nullkiller2" is shown as "Nullkiller 2"; a plugin's name as it is
	static std::string displayName(const std::string & name);
	/// the seat is on the team of a human player (the launcher's Adventure Map Allies apply, as CClient::aiNameForPlayer chooses)
	static bool alliedToHuman(const PlayerSettings & seat);
	/// the AI that plays the seat: its own choice, else the launcher's default, by stored name
	static std::string resolvedAdventure(const PlayerSettings & seat);
	static std::string resolvedBattle(const PlayerSettings & seat);
	/// the tier in effect, 0 (Pawn) to 4 (King): the seat's own, else the map's difficulty
	static int effectiveTier(const PlayerSettings & seat);
};
