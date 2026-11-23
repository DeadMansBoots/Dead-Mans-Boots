/*
 * OptionsTab.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "OptionsTabBase.h"
#include "../windows/CWindowObject.h"
#include "../widgets/Scrollable.h"
#include "../../lib/ResourceSet.h"

VCMI_LIB_NAMESPACE_BEGIN
struct PlayerSettings;
struct PlayerInfo;
enum class PlayerStartingBonus : int8_t;
VCMI_LIB_NAMESPACE_END

class CLabel;
class CMultiLineLabel;
class CFilledTexture;
class CAnimImage;
class CComponentBox;
class CTextBox;
class CButton;
class CSlider;
class LRClickableArea;
class CTextInputWithConfirm;

class FilledTexturePlayerColored;
class TransparentFilledRectangle;

/// The options tab which is shown at the map selection phase.
class OptionsTab : public OptionsTabBase
{
	struct PlayerOptionsEntry;
	
	ui8 humanPlayers;
	std::map<PlayerColor, std::shared_ptr<PlayerOptionsEntry>> entries;
	
public:
	
	OptionsTab();
	void recreate();
	void onSetPlayerClicked(const PlayerSettings & ps) const;
	/// DMB (S466): every open town, hero or bonus picker closes (now, or when it is on top again): the tab behind them is not this one
	static void closePickers(const char * why);
	
	enum SelType
	{
		TOWN,
		HERO,
		BONUS
	};

	class HandicapWindow : public CWindowObject
	{
		std::shared_ptr<FilledTexturePlayerColored> backgroundTexture;

		std::vector<std::shared_ptr<CLabel>> labels;
		std::vector<std::shared_ptr<CAnimImage>> anim;
		std::vector<std::shared_ptr<TransparentFilledRectangle>> textinputbackgrounds;
		std::map<PlayerColor, std::map<EGameResID, std::shared_ptr<CTextInput>>> textinputs;
		/// DMB (S376 C, S398): each seat's difficulty row (what it starts with before any change) and its typeable "Start %" field
		std::map<PlayerColor, TResources> startingRows;
		std::map<PlayerColor, std::shared_ptr<CTextInput>> percentInputs;
		/// S332: each computer seat's "Sees map" (-1 as its AI decides, 0, 1) and the tick that shows what it comes to
		std::map<PlayerColor, int8_t> seesMap;
		std::map<PlayerColor, bool> seesMapDefault;
		std::map<PlayerColor, std::shared_ptr<TransparentFilledRectangle>> seesMapTicks;
		std::vector<std::shared_ptr<CIntObject>> seesMapParts;
		void showSeesMap(PlayerColor player);
		std::vector<std::shared_ptr<CButton>> buttons;

		void notFocusedClick() override;
	public:
		HandicapWindow();
	};

private:
	
	struct CPlayerSettingsHelper
	{
		const PlayerSettings & playerSettings;
		const SelType selectionType;

		CPlayerSettingsHelper(const PlayerSettings & playerSettings, SelType type)
			: playerSettings(playerSettings), selectionType(type)
		{}

		/// visible image settings
		size_t getImageIndex(bool big = false);
		AnimationPath getImageName(bool big = false);

		std::string getName(); /// name visible in options dialog
		std::string getTitle(); /// title in popup box
		std::string getSubtitle(); /// popup box subtitle
		std::string getDescription(); /// popup box description, not always present
	};

	class CPlayerOptionTooltipBox : public CWindowObject, public CPlayerSettingsHelper
	{
		std::shared_ptr<CFilledTexture> backgroundTexture;
		std::shared_ptr<CLabel> labelTitle;
		std::shared_ptr<CLabel> labelSubTitle;
		std::shared_ptr<CAnimImage> image;

		std::shared_ptr<CLabel> labelAssociatedCreatures;
		std::shared_ptr<CComponentBox> boxAssociatedCreatures;

		std::shared_ptr<CLabel> labelHeroSpeciality;
		std::shared_ptr<CAnimImage> imageSpeciality;
		std::shared_ptr<CLabel> labelSpecialityName;

		std::shared_ptr<CTextBox> textBonusDescription;

		void genHeader();
		void genTownWindow();
		void genHeroWindow();
		void genBonusWindow();

	public:
		CPlayerOptionTooltipBox(CPlayerSettingsHelper & helper);
	};

	class SelectionWindow : public CWindowObject
	{
		//const int ICON_SMALL_WIDTH = 48;
		const int ICON_SMALL_HEIGHT = 32;
		const int ICON_BIG_WIDTH = 58;
		const int ICON_BIG_HEIGHT = 64;
		const int TEXT_POS_X = 29;
		const int TEXT_POS_Y = 56;

		const int MAX_LINES = 5;
		const int MAX_ELEM_PER_LINES = 5;

		int elementsPerLine;

		std::shared_ptr<CSlider> slider;

		PlayerColor color;
		SelType type;

		std::shared_ptr<FilledTexturePlayerColored> backgroundTexture;
		std::vector<std::shared_ptr<CIntObject>> components;

		std::vector<FactionID> factions;
		std::vector<HeroTypeID> heroes;
		std::set<HeroTypeID> unusableHeroes;

		FactionID initialFaction;
		HeroTypeID initialHero;
		PlayerStartingBonus initialBonus;
		FactionID selectedFaction;
		HeroTypeID selectedHero;
		PlayerStartingBonus selectedBonus;

		std::set<FactionID> allowedFactions;
		std::set<HeroTypeID> allowedHeroes;
		std::vector<PlayerStartingBonus> allowedBonus;

		void genContentGrid(int lines);
		void genContentFactions();
		void genContentHeroes();
		void genContentBonus();

		void drawOutlinedText(int x, int y, ColorRGBA color, std::string text);
		std::tuple<int, int> calcLines(FactionID faction);
		void apply();
		void recreate(int sliderPos = 0);
		void setSelection();
		int getElement(const Point & cursorPosition);
		void setElement(int element, bool doApply);

		void sliderMove(int slidPos);

		void notFocusedClick() override;
		void clickReleased(const Point & cursorPosition) override;
		void showPopupWindow(const Point & cursorPosition) override;
		/// S448: a right click anywhere reaches the picker, so one outside it can close it
		bool receiveEvent(const Point & position, int eventType) const override;

		/// S426, S448: what the picker shows, read from its seat (the constructor, and every lobby update in place)
		void loadFromSeat();
		/// S448: the seat is gone or no longer this player's while another window is over the picker: it closes as soon as it is the top
		/// window again (activate), or at its next click
		bool seatGone = false;
		void activate() override;

	public:
		void reopen();
		/// S448, S466: closes now when on top, else as soon as it is on top again
		void closeWhenOnTop();

		SelectionWindow(const PlayerColor & color, SelType _type, int sliderPos = 0);
		~SelectionWindow();
	};

	/// Image with current town/hero/bonus
	struct SelectedBox : public Scrollable, public CPlayerSettingsHelper
	{
		std::shared_ptr<CAnimImage> image;
		std::shared_ptr<CLabel> subtitle;

		SelectedBox(Point position, PlayerSettings & playerSettings, SelType type);
		void showPopupWindow(const Point & cursorPosition) override;
		void clickReleased(const Point & cursorPosition) override;
		void scrollBy(int distance) override;

		void update();
	};

	struct PlayerOptionsEntry : public CIntObject
	{
		std::string name;
		std::unique_ptr<PlayerInfo> pi;
		std::unique_ptr<PlayerSettings> s;
		std::shared_ptr<CLabel> labelPlayerName;
		std::shared_ptr<CTextInputWithConfirm> labelPlayerNameEdit;
		/// DMB (S002): a computer seat shows its AI by name with the tier beside it; the host clicks the name to open the seat's AI Settings window
		std::shared_ptr<LRClickableArea> aiSeatArea;
		std::shared_ptr<CAnimImage> aiTierIcon;
		/// S332 (K 11-19: "i should be able to get the seat settings screen by clicking in the red highlighter area"): the "CPU" cell and the
		/// tier piece open the window too; "When a player sets custom handicaps/cheats, the chess-piece difficulty button should change to a
		/// wheel cog": a tile with VCMI's gear over the piece
		std::shared_ptr<LRClickableArea> aiTypeArea;
		std::shared_ptr<LRClickableArea> aiPieceArea;
		std::shared_ptr<TransparentFilledRectangle> customTile;
		std::shared_ptr<CPicture> customGear;
		std::shared_ptr<CMultiLineLabel> labelWhoCanPlay;
		std::shared_ptr<CPicture> background;
		std::shared_ptr<CButton> buttonTownLeft;
		std::shared_ptr<CButton> buttonTownRight;
		std::shared_ptr<CButton> buttonHeroLeft;
		std::shared_ptr<CButton> buttonHeroRight;
		std::shared_ptr<CButton> buttonBonusLeft;
		std::shared_ptr<CButton> buttonBonusRight;
		std::shared_ptr<CButton> flag;
		std::shared_ptr<SelectedBox> town;
		std::shared_ptr<SelectedBox> hero;
		std::shared_ptr<SelectedBox> bonus;
		std::shared_ptr<LRClickableArea> handicap;
		std::shared_ptr<CMultiLineLabel> labelHandicap;
		enum {HUMAN_OR_CPU, HUMAN, CPU} whoCanPlay;

		PlayerOptionsEntry(const PlayerSettings & S, const OptionsTab & parentTab);
		void hideUnavailableButtons();

	private:
		const OptionsTab & parentTab;

		void updateName();
	};
};
