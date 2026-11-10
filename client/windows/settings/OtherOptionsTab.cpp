/*
 * VcmiSettingsWindow.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "OtherOptionsTab.h"

#include "../../../lib/filesystem/ResourcePath.h"
#include "../../GameEngine.h"
#include "../../widgets/Buttons.h"
#include "../../CServerHandler.h"
#include "../../GameInstance.h"
#include "../../../lib/networkPacks/DmbRejoin.h"
#include "CConfigHandler.h"
#include "../../widgets/CTextInput.h"
#include "../../../lib/DmbHarvest.h"

static void setBoolSetting(std::string group, std::string field, bool value)
{
	Settings fullscreen = settings.write[group][field];
	fullscreen->Bool() = value;
}

OtherOptionsTab::OtherOptionsTab() : InterfaceObjectConfigurable()
{
	OBJECT_CONSTRUCTION;

	const JsonNode config(JsonPath::builtin("config/widgets/settings/otherOptionsTab.json"));
	addCallback("availableCreaturesAsDwellingLabelChanged", [](bool value)
	{
		return setBoolSetting("gameTweaks", "availableCreaturesAsDwellingLabel", value);
	});
	addCallback("compactTownCreatureInfoChanged", [](bool value)
	{
		return setBoolSetting("gameTweaks", "compactTownCreatureInfo", value);
	});
	// DMB hot join control (K, November 2nd): the host opens or closes the running game to new and returning players; a guest sees the box but cannot change it
	addCallback("allowJoinChanged", [](bool value)
	{
		setBoolSetting("server", "allowJoin", value);
		JoinPolicy policy;
		policy.allowJoin = value;
		policy.allowRejoin = true; // DMB (K, November 4th, "Resync never fired"): the box is for new players; a dropped player can always come back to their seat
		GAME->server().sendLobbyPack(policy);
	});
	// DMB (S093 93.1b; K 11-07: "a switch that gets turned on in DMB for data acquisition", answer 2 "yeah" to a checkbox in the game's own settings
	// window with the output folder beside it): the same settings the launcher's Gameplay Data page writes; a change takes effect at the next game
	addCallback("harvestChanged", [](bool value)
	{
		Settings harvest = settings.write["general"]["harvest"]["enabled"];
		harvest->Bool() = value;
	});
	build(config);

	if(auto harvest = widget<CToggleButton>("harvestCheckbox"))
		harvest->setSelectedSilent(settings["general"]["harvest"]["enabled"].Bool());
	if(auto folder = widget<CTextInput>("harvestFolderInput"))
	{
		// the folder as set; empty means the game's PlayLogs folder, which the field shows until the player types another
		const std::string set = settings["general"]["harvest"]["folder"].String();
		folder->setText(set.empty() ? Harvest::outputFolder().string() : set);
		folder->setCallback([](const std::string & text)
		{
			Settings harvestFolder = settings.write["general"]["harvest"]["folder"];
			harvestFolder->String() = text;
		});
	}

	std::shared_ptr<CToggleButton> availableCreaturesAsDwellingLabelCheckbox = widget<CToggleButton>("availableCreaturesAsDwellingLabelCheckbox");
	availableCreaturesAsDwellingLabelCheckbox->setSelected(settings["gameTweaks"]["availableCreaturesAsDwellingLabel"].Bool());

	if(auto allowJoin = widget<CToggleButton>("allowJoinCheckbox"))
	{
		allowJoin->setSelectedSilent(settings["server"]["allowJoin"].Bool());
		allowJoin->block(!GAME->server().isHost());
	}

	std::shared_ptr<CToggleButton> compactTownCreatureInfo = widget<CToggleButton>("compactTownCreatureInfoCheckbox");
	compactTownCreatureInfo->setSelected(settings["gameTweaks"]["compactTownCreatureInfo"].Bool());
}


