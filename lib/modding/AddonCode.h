/*
 * AddonCode.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

VCMI_LIB_NAMESPACE_BEGIN

/// DMB: the code an addon mod brings (an AI plugin's "ai" folder, a map generator's "generator" folder)
/// runs without a question only when DMB's mod catalog vouches for it (K, September 26th; October 18th: "we should never
/// lock the user out": a mismatch is a notice the player answers, with Run it anyway always offered, never a refusal;
/// the lobby asks before Begin and the answer is remembered for that exact code, runAnywayFile). A catalog entry pins its mod's
/// code with "codeSha256", one hash or a list of them; the launcher saves the pins of the default
/// catalog (the one DMB ships with), and the game checks the folder against them before it runs
/// anything from it. A tester's PC may also hold builds approved there before the catalog lists them
/// (testedPinsFile), each by its exact hash. The setting mods.allowUnlistedCode lets developers run
/// code they built themselves.
namespace AddonCode
{
	/// The addon API level this DMB offers mods; mod.json's "dmb": { "api": N } asks for at least N.
	/// 1: AI plugins, map generators and the catalog's pins (DMB 0.1.0-rc.1 and rc.2).
	/// 2: the "pages" layout widget, mod.json "tabPages", settings-bound labels, and this check.
	/// 3: a map generator that makes the game's map at Begin (mapGenerator "atBegin", --factions).
	/// 4: chooser buttons (a "setting" with "options") and number-keyed "valueTexts" in layouts.
	/// 5: main menu backgrounds a mod adds to the pool (config/mainMenuBackgrounds.json).
	constexpr int API_LEVEL = 5;

	/// A mod's own folder on disk, absolute, found in any Mods folder; a submod's is inside its parent's
	DLL_LINKAGE std::optional<boost::filesystem::path> modFolder(const std::string & modID);

	/// SHA-256 of a file as 64 lowercase hex digits; empty when the file cannot be read
	DLL_LINKAGE std::string fileHash(const boost::filesystem::path & file);

	/// The hash a catalog pin names: each file under the folder as a line "<its fileHash>  <its path>\n",
	/// the path relative to the folder with "/" separators, the lines sorted by path, and then the SHA-256
	/// of those lines together. They are the lines sha256sum prints. Empty when the folder cannot be read.
	DLL_LINKAGE std::string folderHash(const boost::filesystem::path & folder);

	/// Where the launcher keeps the default catalog's pins: <user cache>/downloads/dmbCodePins.json
	DLL_LINKAGE boost::filesystem::path pinsFile();

	/// Builds approved for testing on this PC, in the same form as the catalog's pins, by those who hand
	/// a tester a build before its release: <user config>/dmbTestedCode.json. The launcher never writes it.
	DLL_LINKAGE boost::filesystem::path testedPinsFile();

	/// What the player chose to run anyway, by mod and exact code hash, in the same form as the catalog's pins:
	/// <user config>/dmbRunAnyway.json. The game writes it (rememberRunAnyway) when the player answers the notice.
	DLL_LINKAGE boost::filesystem::path runAnywayFile();

	/// Works out the hash of the folder into the cache that trustProblem and rememberRunAnyway use, so a later check does not wait for it. Reads no settings: safe on a worker thread.
	DLL_LINKAGE void warmFolderHash(const boost::filesystem::path & folder);

	/// Remembers the code now in the folder as one the player chose to run anyway (its exact hash, for the mod modID): at once for the rest
	/// of this run, which trustProblem consults beside the files (the local server runs in this process), and in runAnywayFile for later runs.
	/// Returns empty when the file kept it, else why it could not (the answer then holds only until the game quits; the caller tells the player)
	DLL_LINKAGE std::string rememberRunAnyway(const std::string & modID, const boost::filesystem::path & folder);

	/// Why the code in the folder, from the mod modID, is not one the catalog vouches for; empty when it is (or the player
	/// chose to run it anyway). When `answerable` is given, it says whether the reason is for a notice with a choice (the code is not
	/// listed, or is not the listed code): a folder that cannot be read is a plain error, since running it anyway could not work.
	DLL_LINKAGE std::string trustProblem(const std::string & modID, const boost::filesystem::path & folder, bool * answerable = nullptr);
}

VCMI_LIB_NAMESPACE_END
