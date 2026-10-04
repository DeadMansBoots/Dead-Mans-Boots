/*
 * MenuBackgrounds.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

VCMI_LIB_NAMESPACE_BEGIN

/// One of the main menu pictures (GamSelBk) that the player's own game archives hold: each release of the game
/// (Shadow of Death, Armageddon's Blade, Horn of the Abyss, any other archive) ships its own art for the same menu.
struct DLL_LINKAGE MenuEra
{
	std::string id; // the archive's file name in lower case, without its extension: "h3bitmap", "hota"
	std::string title; // what the player is shown
	boost::filesystem::path archive;
	/// the same release's archive of sprites, when it holds the menu's own buttons (H3sprite.lod beside H3bitmap.lod, H3ab_spr.lod
	/// beside H3ab_bmp.lod): each release draws its buttons to go with its picture, and the pink glow of the one set that the game
	/// resolves shows as a halo on any other release's picture. Empty when there is none (Horn of the Abyss keeps its buttons in
	/// its own menu mod).
	boost::filesystem::path sprites;
};

/// DMB: the main menu's backgrounds, found in the installed game's files when the game starts (nothing is shipped, K,
/// September 26th: "occasionally see the real Heroes III main menu splash screens from RoE, Armageddon's Blade, Shadow of
/// Death and other release eras"). Archives are found, not named: every .lod in the game's Data folders that holds a
/// GamSelBk is an era, so a game with another archive, or with fewer, shows what it has. A picture that another archive
/// also holds byte for byte is listed once.
class DLL_LINKAGE MenuBackgrounds
{
public:
	/// every distinct main menu picture the installed archives hold, in a fixed order (older releases first)
	static const std::vector<MenuEra> & eras();

	/// the picture's own bytes (a Heroes III .pcx), empty when the archive cannot be read
	static std::vector<ui8> pcx(const MenuEra & era);

	/// a button's .def (name without extension, "MMENUNG") out of the release's sprite archive; empty when it holds none
	static std::vector<ui8> sprite(const MenuEra & era, const std::string & name);

	/// A Heroes III .pcx as 8 bit RGB triples, row by row, from the top. False for anything but the two layouts the game's
	/// pictures have (a palette at the end of the picture, or plain blue-green-red).
	static bool decode(const std::vector<ui8> & pcx, int & width, int & height, std::vector<ui8> & rgb);
};

VCMI_LIB_NAMESPACE_END
