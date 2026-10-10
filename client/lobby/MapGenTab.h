/*
 * MapGenTab.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../gui/InterfaceObjectConfigurable.h"
#include "../../lib/modding/MapGenerators.h"

class CTabbedInt;
class CLabel;

/// DMB: the tab of a map generator that a mod brings (lib/modding/MapGenerators.h), a third
/// map-selection tab beside Scenarios and Random Map, reached from its own lobby button. Everything
/// it shows comes from the mod. The mod's tab layout lists the pages and holds the defaults; each
/// page is a layout of settings-bound widgets; the texts are the mod's translations. Generate runs
/// the mod's command with those settings, once DMB's mod catalog has vouched for the mod's generator
/// folder (AddonCode::trustProblem), and selects the new map in the scenario list. The settings
/// live in persistentStorage.json (mapGen.map.*, mapGen.params.*, mapGen.preset), which no schema
/// can erase.
///
/// The command gets --w --h --players --humans --seed --out, --underground 1 on a two-level map,
/// --declaremods 1 or 0, --vcmiroot and --vcmiuserdir (the game's data and user folders),
/// --template when one is chosen, --preset, and each setting of mapGen.params as --bio.<name>
/// (rivers as --rivers). It writes a .vmap at --out. Its output goes to extmapgen_log.txt in the
/// logs folder, and a line there starting "Error: " is what the player is shown when it fails.
class MapGenPage : public InterfaceObjectConfigurable
{
public:
	/// `setup` runs before the layout is built (to add callbacks) and `after` once it is (to fill
	/// labels from settings)
	MapGenPage(const JsonNode & layout, const std::function<void(MapGenPage &)> & setup,
		const std::function<void(MapGenPage &)> & after);

	void setCallback(const std::string & name, std::function<void(int)> callback);
	/// told of each change to a setting its widgets are bound to, as the tab is
	void setOnSettingChanged(std::function<void(const std::string &)> callback);
	std::shared_ptr<CLabel> label(const std::string & name) const;
	std::shared_ptr<CToggleGroup> toggleGroup(const std::string & name) const;
	std::shared_ptr<CTextInput> textInput(const std::string & name) const;
};

class MapGenTab : public InterfaceObjectConfigurable
{
	MapGeneratorInfo generator;
	/// the pages of a tab layout that holds a "pages" widget named "pages" (LayoutPages)
	std::shared_ptr<LayoutPages> layoutPages;
	/// the pages of an older tab layout: its root "pages" list, opened by activateMapGenPage buttons
	std::shared_ptr<CTabbedInt> pages;
	std::vector<std::string> pageFiles;
	JsonNode defaults;
	/// re-entry guard for the generator child, shared with its completion
	/// dispatch so it clears even if the tab is gone
	std::shared_ptr<bool> generating;
	/// lives as long as the tab does: a rebuild of the pages that waits for the click that asked for
	/// it to end (refreshPagesAfterClick) holds a weak reference to it, to know the tab is still there
	std::shared_ptr<bool> alive = std::make_shared<bool>(true);
	/// the hover box (HoverTip, MapGenTab.cpp): the engine's status bar while the tab is shown
	std::shared_ptr<IStatusBar> hoverTip;
	/// a control's name, as it is written to the status bar, to what the control does: the tab's own layout (its page buttons),
	/// and each page's own, since a page's control can carry the name of its page button (K, October 4th: the Underground
	/// check showed the page button's help)
	std::map<std::string, std::string> helpBodies;
	std::vector<std::map<std::string, std::string>> pageHelpBodies;
	void collectHelp(const JsonNode & items, std::map<std::string, std::string> & into);
	/// the custom size boxes are drawn grey while the size is Random (they stay there: typing in one picks a custom size)
	void greyCustomSize();
	/// the module's "reset" block (or its defaults): what the Reset button puts every setting to
	JsonNode resetValues;
	bool refreshQueued = false;

	std::shared_ptr<CIntObject> createPage(size_t index);
	void openPage(size_t index);
	void refreshPages();
	/// refreshPages() for a change made by a widget on the page itself, which it cannot free while
	/// that widget's own handlers run
	void refreshPagesAfterClick();
	void resetToDefaults();
	void chooseTemplate();
	/// the templates the generator's module lists as approved (its approved-templates.json), in lower case
	std::vector<std::string> approvedNames() const;
	/// a stored template choice that is no longer approved is cleared (a stale preset cannot make a hidden template)
	void dropUnapprovedTemplate() const;
	/// VCMI's team grid (callback chooseMapGenTeams), stored as map/teams
	void chooseTeams();
	/// VCMI's custom size window (callback chooseMapGenCustomSize), stored as map/width and map/height
	void chooseCustomSize();
	void clearCustomSize();
	/// the player's own saved settings (callbacks saveMapGenPreset and loadMapGenPreset), a JSON file
	/// each in the user folder's MapGenPresets/<mod id>, in place of the tab's
	void savePreset();
	void loadPreset();
	boost::filesystem::path presetFolder() const;
	void generate();

	int mapSetting(const std::string & key) const;
	std::string templateName() const;
	/// the chosen template as a label shows it: its name, "(Random)" or the tab's "none" text
	std::string templateLabel() const;
	std::string preset() const;
	/// the lobby's random map as the tab's settings describe it, counts left on Random rolled
	std::shared_ptr<CMapGenOptions> lobbyOptions() const;

public:
	explicit MapGenTab(const MapGeneratorInfo & generator);

	const MapGeneratorInfo & getGenerator() const;
	/// DMB (K, October 9th): the page of the tab shown now, and a way to show one (the guests' lobby follows the host's page); and who is told
	/// when a page is shown
	size_t currentPage() const;
	void showPage(size_t index);
	std::function<void(size_t)> onPageShown;
	/// the host's settings of this generator as JSON text, for the guests (K, October 10th); told to onSettingsChanged when the host changes them
	std::string stateText() const;
	std::function<void()> onSettingsChanged;
	/// a guest: shows the host's settings in place of its own (kept in memory, and put back by restoreOwnState when the lobby closes)
	void applyHostState(const std::string & text);
	void restoreOwnState();
	void restoreOwnStateAfterCrash();
	boost::filesystem::path ownStateFile() const;
	/// the guest's own settings while the host's are shown, and the host's text last shown
	JsonNode ownState;
	bool ownStateKept = false;
	std::string appliedHostState;

	/// For a generator that makes the game's map at Begin (MapGeneratorInfo::atBegin): puts the lobby's
	/// random-map entry together from the tab's settings, the slots everyone sees and the towns they
	/// pick included, marked for this generator, as the Random Map tab does for VCMI's own. The host's
	/// server then runs the generator when the host presses Begin (MapGenerators::generateForGame).
	/// Read: map/size (or map/width and map/height), map/underground, map/humans and params/compOnly
	/// (-1 for Random), map/teams (a team number per player, in colour order).
	void updateMapInfoByHost();
};
