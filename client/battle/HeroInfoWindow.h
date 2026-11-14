/*
 * HeroInfoWindow.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../windows/CWindowObject.h"

class CLabel;
class CAnimImage;
class FilledTexturePlayerColored;

VCMI_LIB_NAMESPACE_BEGIN
struct InfoAboutHero;
class CGHeroInstance;
VCMI_LIB_NAMESPACE_END

class HeroInfoBasicPanel : public CIntObject //extracted from InfoWindow to fit better as non-popup embed element
{
private:
	std::shared_ptr<CPicture> background;
	std::vector<std::shared_ptr<CLabel>> labels;
	std::vector<std::shared_ptr<CAnimImage>> icons;

public:
	HeroInfoBasicPanel(const InfoAboutHero & hero, const Point * position, bool initializeBackground = true);

	void show(Canvas & to) override;

	void initializeData(const InfoAboutHero & hero);
	void update(const InfoAboutHero & updatedInfo);
};

class HeroInfoWindow : public CWindowObject
{
private:
	std::shared_ptr<HeroInfoBasicPanel> content;

public:
	HeroInfoWindow(const InfoAboutHero & hero, const Point * position);
};

/// DMB (S167, K November 12th: "i want the ability to right-click my character inventory and see what i have equipped during battles"): beside the stock
/// hero popup of a battle, the artifacts the player's own hero wears, while the right button is held
class HeroArtifactsPopup : public CWindowObject
{
	std::shared_ptr<FilledTexturePlayerColored> backgroundTexture;
	std::shared_ptr<CLabel> title;
	std::vector<std::shared_ptr<CAnimImage>> icons;

public:
	/// `beside`: the stock popup's top left corner; `toTheLeft`: drawn left of it (the defender's side), else right of it
	HeroArtifactsPopup(const CGHeroInstance * hero, const Point & beside, bool toTheLeft);
};
