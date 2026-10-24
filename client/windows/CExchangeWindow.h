/*
 * CExchangeWindow.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CWindowWithArtifacts.h"
#include "../widgets/CExchangeController.h"

class CGarrisonSlot;
class IImage;
class CMultiLineLabel;
class LRClickableAreaWText;

class CExchangeWindow : public CStatusbarWindow, public IGarrisonHolder, public CWindowWithArtifacts
{
	std::array<std::shared_ptr<CLabel>, 2> titles;
	std::vector<std::shared_ptr<CAnimImage>> primSkillImages;//shared for both heroes
	std::array<std::vector<std::shared_ptr<CLabel>>, 2> primSkillValues;
	std::array<std::shared_ptr<CAnimImage>, 2> specImages;
	std::array<std::shared_ptr<CAnimImage>, 2> expImages;
	std::array<std::shared_ptr<CLabel>, 2> expValues;
	std::array<std::shared_ptr<CAnimImage>, 2> manaImages;
	std::array<std::shared_ptr<CLabel>, 2> manaValues;

	std::vector<std::shared_ptr<LRClickableAreaWTextComp>> primSkillAreas;
	std::array<std::vector<std::shared_ptr<CSecSkillPlace>>, 2> secSkills;
	std::array<std::shared_ptr<CMultiLineLabel>, 2> secSkillsFull;
	std::array<std::shared_ptr<LRClickableAreaWText>, 2> secSkillsFullArea;

	std::array<std::shared_ptr<CHeroArea>, 2> heroAreas;
	std::array<std::shared_ptr<LRClickableAreaWText>, 2> specialtyAreas;
	std::array<std::shared_ptr<LRClickableAreaWText>, 2> experienceAreas;
	std::array<std::shared_ptr<LRClickableAreaWText>, 2> spellPointsAreas;

	std::array<std::shared_ptr<MoraleLuckBox>, 2> morale;
	std::array<std::shared_ptr<MoraleLuckBox>, 2> luck;

	std::shared_ptr<CButton> quit;
	std::array<std::shared_ptr<CButton>, 2> questlogButton;

	std::shared_ptr<CGarrisonInt> garr;
	std::shared_ptr<CButton> buttonMoveUnitsFromLeftToRight;
	std::shared_ptr<CButton> buttonMoveUnitsFromRightToLeft;
	std::shared_ptr<CButton> buttonMoveArtifactsFromLeftToRight;
	std::shared_ptr<CButton> buttonMoveArtifactsFromRightToLeft;

	std::shared_ptr<CButton> exchangeUnitsButton;
	std::shared_ptr<CButton> exchangeArtifactsButton;

	std::vector<std::shared_ptr<CButton>> moveUnitFromLeftToRightButtons;
	std::vector<std::shared_ptr<CButton>> moveUnitFromRightToLeftButtons;
	std::shared_ptr<CButton> backpackButtonLeft;
	std::shared_ptr<CButton> backpackButtonRight;
	CExchangeController controller;

	void creatureArrowButtonCallback(bool leftToRight, SlotID slotID);
	void moveArtifactsCallback(bool leftToRight);
	void swapArtifactsCallback();
	void moveUnitsShortcut(bool leftToRight);
	void backpackShortcut(bool leftHero);
	void questLogShortcut();

	std::array<const CGHeroInstance *, 2> heroInst;
	std::array<std::shared_ptr<CArtifactsOfHeroMain>, 2> artifs;

	const CGarrisonSlot * getSelectedSlotID() const;
	/// True when this player does not own the hero, so nothing may be taken from it
	bool isHeroBlocked(size_t leftRight) const;

	QueryID queryID;

	/// DMB: the other player's pointer in a shared trade (K, October 24th: "THEIR MOUSE SHOULD BE THEIR COLOR, ANYTHING THEY HAVE SELECTED/ARE MOVING
	/// SHOULD HAVE A OUTLINE WITH THEIR COLOR")
	PlayerColor allyColor = PlayerColor::CANNOT_DETERMINE;
	Point allyTarget = Point(-1, -1);
	double allyX = -1;
	double allyY = -1;
	int allySelection = -1;
	std::shared_ptr<IImage> allyArtImage;
	int allyArtIndex = -1;
	/// this player's pointer, as last sent
	Point ownPointer = Point(-1, -1);
	Point sentPointer = Point(-2, -2);
	int sentSelection = -2;
	uint32_t sinceSent = 0;

	bool sharesPointer() const;
	int ownSelection() const;
	void sendPointer(const Point & position, int selection);
	void drawAllyPointer(Canvas & to);

public:
	CExchangeWindow(ObjectInstanceID hero1, ObjectInstanceID hero2, QueryID queryID);
	/// DMB: the trade this window answers (an invalid ID for a window opened from a town)
	QueryID tradeQuery() const { return queryID; }
	/// DMB: the other player closed the shared trade; this window and what was opened from it close
	void closeTrade();
	/// DMB: the other player's pointer, from the server
	void showAllyPointer(PlayerColor player, const Point & position, int selection);

	void mouseMoved(const Point & cursorPosition, const Point & lastUpdateDistance) override;
	void tick(uint32_t msPassed) override;
	void showAll(Canvas & to) override;
	void show(Canvas & to) override;
	void deactivate() override;
	void activate() override;

	void keyPressed(EShortcut key) override;

	void updateArtifacts() override;

	// IGarrisonHolder impl
	void updateGarrisons() override;
	bool holdsGarrison(const CArmedInstance * army) override;

};
