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
	/// the picture's name in the archive: GamSelBk is the menu, GamSelB0 and GamSelB1 are the game's other two full-screen title pictures
	std::string entry = "GAMSELBK.PCX";
	boost::filesystem::path archive;
	/// the same release's archive of sprites, when it holds the menu's own buttons (H3sprite.lod beside H3bitmap.lod, H3ab_spr.lod
	/// beside H3ab_bmp.lod): each release draws its buttons to go with its picture, and the pink glow of the one set that the game
	/// resolves shows as a halo on any other release's picture. Empty when there is none (Horn of the Abyss keeps its buttons in
	/// its own menu mod).
	boost::filesystem::path sprites;
};

/// A mod (or sub-mod) that adds a main menu of its own: its config/mainmenu.json lays out the whole window, picture and buttons.
struct DLL_LINKAGE MenuMod
{
	std::string id; // the mod's id as the game knows it, in lower case: "hota.mainmenu", "tides-of-war.tow-main-menu"
	std::string title; // what the player is shown: "Horn of the Abyss menu"
	boost::filesystem::path folder; // the mod's own folder (its mod.json, Content.zip or Content)
};

/// A picture the player dropped into the MenuBackgrounds folder of their own DMB folder (a .png or .bmp of any size; the game's image library here reads no JPEG): it joins the
/// rotation and the Settings choice under its file name.
struct DLL_LINKAGE MenuFile
{
	std::string id; // "file:" and the file's name in lower case
	std::string title; // the file's name without its extension, words apart: "Dusk over the Abyss"
	std::string slug; // the file's name without its extension in lower case, anything but letters and digits a dash: the name of its generated image
	boost::filesystem::path path;
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

	/// every installed mod that adds a main menu (active or not: two menu mods conflict, so only one is ever active), found in the
	/// Mods folders of the game and of the player, sub-mods included; one that conflicts with the active ones is still listed
	static const std::vector<MenuMod> & menuMods();

	/// an installed mod's folder by its id ("h3-themes._support.originalbuttons": each part a mod inside the one before), empty when none
	static boost::filesystem::path modFolder(const std::string & id);

	/// the ids a mod's mod.json says it depends on, in lower case
	static std::vector<std::string> modDepends(const boost::filesystem::path & folder);

	/// the folder the player drops pictures into (their DMB folder's MenuBackgrounds), made when it is first asked for
	static boost::filesystem::path dropInFolder();

	/// every picture in that folder, by file name
	static const std::vector<MenuFile> & dropIns();

	/// the folder the player drops loading screens into (their DMB folder's LoadingScreens, with an Upscaled folder beside the pictures
	/// as MenuBackgrounds has), made when it is first asked for
	static boost::filesystem::path loadingFolder();

	/// every loading screen in that folder, by file name (the same picture files as the menu's: any size, cut to 800x600 from the middle)
	static const std::vector<MenuFile> & loadingScreens();

	/// the picture's own bytes (a Heroes III .pcx), empty when the archive cannot be read
	static std::vector<ui8> pcx(const MenuEra & era);

	/// a button's .def (name without extension, "MMENUNG") out of the release's sprite archive; empty when it holds none
	static std::vector<ui8> sprite(const MenuEra & era, const std::string & name);

	/// the same button with the pink glow baked into it (made to match the one picture of its release) taken away (a thin dark edge stays against the icon), so
	/// it sits on any other picture; the button as a .def, empty when the file is not one this reads
	static std::vector<ui8> neutralSprite(const MenuEra & era, const std::string & name);

	/// whether the button's first frame is a cut-out (a good part of it transparent) and not a piece of the release's background picture
	/// drawn round the button, as Armageddon's Blade's sub menu buttons are: only a cut-out can sit on another picture
	static bool isCutout(const MenuEra & era, const std::string & name);

	/// A Heroes III .pcx as 8 bit RGB triples, row by row, from the top. False for anything but the two layouts the game's
	/// pictures have (a palette at the end of the picture, or plain blue-green-red).
	static bool decode(const std::vector<ui8> & pcx, int & width, int & height, std::vector<ui8> & rgb);
};

VCMI_LIB_NAMESPACE_END
