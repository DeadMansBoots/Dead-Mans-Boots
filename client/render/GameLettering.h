/*
 * GameLettering.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "EFont.h"
#include "../../lib/Rect.h"

class IImage;

/// DMB: the lettering Heroes III bakes into its own buttons, for words DMB draws in their place (the project owner,
/// September 27th: DMB's art matches the game's own lettering). The letters come from the game's own
/// bitmap fonts, read from its font files at their own pixel size, so they come out the same at any
/// screen scale (the fonts VCMI draws text with are already scaled to the screen).
namespace GameLettering
{
	struct Letters
	{
		int width = 0;  ///< the text's box: the letters' advances, and the font's height
		int height = 0;
		std::vector<std::vector<bool>> pixels; ///< [row][column], true where a letter is (its shadow left out)
	};

	/// `text` in `font`, the whole box the game would draw it in, or trimmed to the letters themselves;
	/// empty when the font's file is missing or the text has no letters
	Letters letters(EFonts font, const std::string & text, bool trim);

	/// `text` carved into gold as the game carves the words of its gold buttons (RANSHOW, read pixel by
	/// pixel): black letters, a brown edge on their left, a white light on their right and underneath,
	/// transparent around. At the screen's scale, one pixel wider than the text's box on every side.
	std::shared_ptr<IImage> carved(EFonts font, const std::string & text);

	struct ButtonWords
	{
		std::shared_ptr<IImage> image; ///< at the screen's scale; transparent where the gold shows through
		Rect letters; ///< the letters' black in the image: the words' width, and the lettering's height
	};

	/// `text` in capitals in DMB's own button face (Data/DMBButtonFace.ttf, the face the launcher's buttons use), its capitals 22
	/// game pixels tall as the lettering of the game's gold buttons on their 40 pixel bars (the project owner, October 12th 2026: "build OUR OWN FONT SET"),
	/// carved: a dark letter with a light edge to its lower left, smoothed at the screen's scale. Every letter, digit and mark the face
	/// has, the accented capitals included. No image when the face's file is missing or the text has a character the face lacks.
	ButtonWords buttonWords(const std::string & text);
}
