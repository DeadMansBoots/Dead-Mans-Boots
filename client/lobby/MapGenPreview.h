/*
 * MapGenPreview.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../gui/CIntObject.h"
#include "../render/Canvas.h"
#include "../../lib/json/JsonNode.h"

VCMI_LIB_NAMESPACE_BEGIN
class CMapGenOptions;
struct StartInfo;
VCMI_LIB_NAMESPACE_END

/// DMB (K, November 4th, line S035: "it would also be nice to use this system to generate a preview of what the map the user's currently
/// selected settings will generate (approximately)"): the lobby's map card shows a picture of the map a generator mod would make from the
/// lobby's current settings, drawn in the look of the template previews (zones by kind, dark borders between them, the starts as castles
/// in the player's colour, no numbers).
///
/// The picture comes from the generator itself: a generator whose mod.json lists "layout" among its "arguments" takes --layout FILE and
/// writes the map's layout as JSON there in a few seconds, without making the map (OmniMapGen's format: size, starts, and per level the
/// zone of each tile and each zone's kind). It is run with the arguments Begin would give it (MapGenerators::gameArguments) and a fixed
/// seed, so the picture changes only when a setting does. Hidden, off the game's main thread, one second after the last change, at most
/// one run at a time and 60 seconds each; the lobby never waits for it, and Begin never waits for it. A generator DMB's mod catalog does
/// not vouch for is never run for a preview (its picture stays empty; Generate and Begin still ask the player as before).
namespace MapGenPreview
{
	/// true when these options are a generator mod's random map and that generator takes "layout"
	bool applies(const CMapGenOptions & options);
	/// asks for the picture of these settings for the lobby card; nothing when they are the ones asked for last
	void request(const CMapGenOptions & options, const StartInfo & start);
	/// DMB (S246, K November 15th: the template and water layout menus' pictures "should look how the template maps look, but be focused on various
	/// templates with whatever the currently selected water layout is"): asks for the picture of these settings with some of the generator's arguments
	/// replaced ("--template" NAME, "--bio.waterShape" N, ...; added when absent). Every distinct set of arguments is run once, the newest asked first;
	/// the key names its layout for a MapGenPreviewBox. Empty when no picture applies.
	std::string requestWith(const CMapGenOptions & options, const StartInfo & start, const std::map<std::string, std::string> & replaced);
}

/// The picture of a layout (the lobby card's newest when its key is empty), one square per level side by side, drawn at the screen's own resolution
class MapGenPreviewBox : public CIntObject
{
	std::string key;
	bool drawEmpty;
	int drawnSerial = -1;
	int drawnScale = 0;
	Point drawnSize;
	std::vector<std::unique_ptr<Canvas>> panels;
	std::vector<Point> panelAt;

	void redrawPanels();
public:
	/// drawEmpty: an empty square while there is no layout (the card); without it nothing is drawn, so what lies under the box shows (the menus)
	explicit MapGenPreviewBox(const Rect & position, std::string key = {}, bool drawEmpty = true);
	void showAll(Canvas & to) override;
};
