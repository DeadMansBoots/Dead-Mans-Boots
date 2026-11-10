/*
 * Exporters.cpp, part of VCMI engine (Dead Man's Boots)
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "Harvest.h"

#include "OmniAIReasons.h"

/// DMB (S093 93.5, 93.8): the list of exporters, in the order the settings page shows them. A new exporter is one line here.
namespace Harvest
{
namespace
{
/// The play recorder's own lines (19.3's PlayLogs), one exporter per kind of line, so each has its tick (93.5). They need no call
/// point: the recorder writes their lines into play.jsonl when Registry::exporterActive(id) says so.
class RecorderPart : public Exporter
{
	std::string id_, name_, description_;

public:
	RecorderPart(std::string id, std::string name, std::string description)
		: id_(std::move(id)), name_(std::move(name)), description_(std::move(description))
	{
	}
	std::string id() const override { return id_; }
	std::string name() const override { return name_; }
	std::string description() const override { return description_; }
	std::string version() const override { return "1"; }
};
}

void registerExporters(Registry & registry)
{
	registry.add(std::make_unique<RecorderPart>("actions", "Actions",
		"Every order each seat this computer runs gives: moves, builds, recruits, trades, spells, battle commands, end of turn."));
	registry.add(std::make_unique<RecorderPart>("answers", "Game answers",
		"What the game did in reply: fights with both armies and losses, objects visited, resources, levels."));
	registry.add(std::make_unique<RecorderPart>("snapshots", "Daily state",
		"Each seat's resources, towns, heroes and armies at the start of its turn."));
	registry.add(std::make_unique<RecorderPart>("interface", "Interface",
		"Windows opened and closed, what was selected, and settings changed with old and new value."));
	registry.add(std::make_unique<OmniAIReasons>());
}
}
