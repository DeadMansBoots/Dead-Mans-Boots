/*
 * Harvest.cpp, part of VCMI engine (Dead Man's Boots)
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "Harvest.h"

#include "../PlayRecorder.h"

#include "../../lib/CConfigHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/VCMIDirs.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/modding/ModDescription.h"
#include "../../lib/modding/CModVersion.h"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <mutex>
#include <set>

/// the engine's settings handle, named before Harvest::Settings (the UI's functions) hides it
using EngineSettingsHandle = Settings;

namespace Harvest
{
namespace
{
/// the version of play.jsonl's line format (docs/modders/DMB_Harvest_Schema.md); raised when a field changes meaning
constexpr int SCHEMA_VERSION = 1;

std::string jsonText(const std::string & text)
{
	std::string out = "\"";
	for(unsigned char c : text)
	{
		switch(c)
		{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if(c < 0x20)
				{
					char buf[8];
					std::snprintf(buf, sizeof(buf), "\\u%04x", c);
					out += buf;
				}
				else
					out += static_cast<char>(c);
		}
	}
	return out + "\"";
}
}

void Game::write(const std::string & kind, const std::string & fields) const
{
	PlayRecorder::get().writeLine(kind, fields);
}

struct Registry::Impl
{
	std::recursive_mutex mutex;
	std::vector<std::unique_ptr<Exporter>> exporters;
	/// this game's: the enabled exporters (the ticks as they were at game start), the ones that threw, the game
	std::vector<Exporter *> active;
	std::set<Exporter *> failed;
	Game game;
	bool recording = false;
	std::string started;
};

Registry & Registry::get()
{
	static Registry instance;
	static std::once_flag once;
	std::call_once(once, []()
	{
		instance.impl = std::make_shared<Impl>();
		registerExporters(instance);
	});
	return instance;
}

void Registry::add(std::unique_ptr<Exporter> exporter)
{
	if(!exporter)
		return;
	std::lock_guard lock(impl->mutex);
	for(const auto & e : impl->exporters)
		if(e->id() == exporter->id())
		{
			logGlobal->warn("Harvest: a second exporter with id '%s' is ignored", exporter->id());
			return;
		}
	impl->exporters.push_back(std::move(exporter));
}

std::vector<ExporterInfo> Registry::list() const
{
	std::lock_guard lock(impl->mutex);
	std::vector<ExporterInfo> result;
	for(const auto & e : impl->exporters)
		result.push_back({e->id(), e->name(), e->description(), e->enabledByDefault()});
	return result;
}

bool Registry::recording() const
{
	std::lock_guard lock(impl->mutex);
	return impl->recording;
}

/// Calls f on every enabled exporter that has not thrown this game; one that throws is skipped from then on and logged once
template<typename F>
static void each(Registry::Impl & impl, const char * point, F && f)
{
	for(auto * e : impl.active)
	{
		if(impl.failed.count(e))
			continue;
		try
		{
			f(*e);
		}
		catch(const std::exception & ex)
		{
			impl.failed.insert(e);
			logGlobal->error("Harvest: exporter '%s' failed at %s (%s) and is off for the rest of this game", e->id(), point, ex.what());
		}
		catch(...)
		{
			impl.failed.insert(e);
			logGlobal->error("Harvest: exporter '%s' failed at %s and is off for the rest of this game", e->id(), point);
		}
	}
}

static void writeManifest(Registry::Impl & impl, bool final)
{
	try
	{
		std::string out = "{\n";
		out += "\t\"schema\" : " + std::to_string(SCHEMA_VERSION) + ",\n";
		out += "\t\"gameStartTime\" : " + std::to_string(impl.game.gameStartTime) + ",\n";
		out += "\t\"map\" : " + jsonText(impl.game.map) + ",\n";
		out += "\t\"seat\" : " + jsonText(impl.game.seat.isValidPlayer() ? impl.game.seat.toString() : std::string()) + ",\n";
#ifdef DMB_VERSION_STRING
		out += "\t\"dmbVersion\" : " + jsonText(DMB_VERSION_STRING) + ",\n";
#endif
		out += "\t\"started\" : " + jsonText(impl.started) + ",\n";
		out += "\t\"complete\" : " + std::string(final ? "true" : "false") + ",\n";
		out += "\t\"mods\" : [";
		bool first = true;
		if(LIBRARY && LIBRARY->modh)
			for(const auto & modID : LIBRARY->modh->getActiveMods())
			{
				const auto & mod = LIBRARY->modh->getModInfo(modID);
				out += std::string(first ? "\n" : ",\n") + "\t\t{ \"id\" : " + jsonText(modID) + ", \"version\" : " + jsonText(mod.getVersion().toString()) + " }";
				first = false;
			}
		out += first ? "],\n" : "\n\t],\n";
		out += "\t\"exporters\" : [";
		first = true;
		for(auto * e : impl.active)
		{
			out += std::string(first ? "\n" : ",\n") + "\t\t{ \"id\" : " + jsonText(e->id()) + ", \"version\" : " + jsonText(e->version());
			out += ", \"failed\" : " + std::string(impl.failed.count(e) ? "true" : "false");
			std::vector<std::string> lines;
			if(final && !impl.failed.count(e))
			{
				try
				{
					lines = e->manifest();
				}
				catch(...)
				{
				}
			}
			out += ", \"notes\" : [";
			for(size_t i = 0; i < lines.size(); i++)
				out += (i ? ", " : "") + jsonText(lines[i]);
			out += "] }";
			first = false;
		}
		out += first ? "]\n" : "\n\t]\n";
		out += "}\n";
		boost::filesystem::ofstream file(impl.game.folder / "manifest.json", std::ios::out | std::ios::trunc);
		file << out;
	}
	catch(...)
	{
	}
}

void Registry::gameStart(int64_t gameStartTime, const std::string & map, PlayerColor seat, const boost::filesystem::path & folder)
{
	std::lock_guard lock(impl->mutex);
	impl->active.clear();
	impl->failed.clear();
	impl->recording = false;
	if(!Settings::enabled())
		return;
	impl->game = Game();
	impl->game.gameStartTime = gameStartTime;
	impl->game.map = map;
	impl->game.seat = seat;
	impl->game.folder = folder;
	impl->started = boost::posix_time::to_iso_extended_string(boost::posix_time::second_clock::local_time());
	for(const auto & e : impl->exporters)
		if(Settings::exporterEnabled(e->id()))
			impl->active.push_back(e.get());
	impl->recording = true;
	writeManifest(*impl, false);
	each(*impl, "game start", [&](Exporter & e) { e.gameStart(impl->game); });
}

void Registry::turnStart(PlayerColor player, int day)
{
	std::lock_guard lock(impl->mutex);
	if(impl->recording)
		each(*impl, "turn start", [&](Exporter & e) { e.turnStart(impl->game, player, day); });
}

void Registry::request(const Line & line)
{
	std::lock_guard lock(impl->mutex);
	if(impl->recording)
		each(*impl, "request", [&](Exporter & e) { e.request(impl->game, line); });
}

void Registry::answer(const Line & line)
{
	std::lock_guard lock(impl->mutex);
	if(impl->recording)
		each(*impl, "answer", [&](Exporter & e) { e.answer(impl->game, line); });
}

void Registry::battleEnd(const Line & line)
{
	std::lock_guard lock(impl->mutex);
	if(impl->recording)
		each(*impl, "battle end", [&](Exporter & e) { e.battleEnd(impl->game, line); });
}

void Registry::other(const Line & line)
{
	std::lock_guard lock(impl->mutex);
	if(impl->recording)
		each(*impl, "other", [&](Exporter & e) { e.other(impl->game, line); });
}

void Registry::gameEnd(const std::string & why)
{
	std::lock_guard lock(impl->mutex);
	if(!impl->recording)
		return;
	each(*impl, "game end", [&](Exporter & e) { e.gameEnd(impl->game, why); });
	writeManifest(*impl, true);
	impl->recording = false;
	impl->active.clear();
}

bool Registry::exporterActive(const std::string & id) const
{
	std::lock_guard lock(impl->mutex);
	if(!impl->recording)
		return false;
	for(auto * e : impl->active)
		if(e->id() == id)
			return !impl->failed.count(e);
	return false;
}

namespace Settings
{
bool enabled()
{
	return settings["general"]["harvestEnabled"].Bool();
}

void setEnabled(bool on)
{
	EngineSettingsHandle s = settings.write["general"]["harvestEnabled"];
	s->Bool() = on;
}

std::string outputFolder()
{
	return settings["general"]["harvestFolder"].String();
}

boost::filesystem::path resolvedOutputFolder()
{
	const std::string set = outputFolder();
	if(!set.empty())
		return boost::filesystem::path(set);
	return VCMIDirs::get().userDataPath() / "PlayLogs";
}

void setOutputFolder(const std::string & folder)
{
	EngineSettingsHandle s = settings.write["general"]["harvestFolder"];
	s->String() = folder;
}

bool exporterEnabled(const std::string & id)
{
	const JsonNode & node = settings["general"]["harvestExporters"][id];
	if(node.isBool())
		return node.Bool();
	for(const auto & e : Registry::get().list())
		if(e.id == id)
			return e.enabledByDefault;
	return false;
}

void setExporterEnabled(const std::string & id, bool on)
{
	EngineSettingsHandle s = settings.write["general"]["harvestExporters"][id];
	s->Bool() = on;
}
}
}
