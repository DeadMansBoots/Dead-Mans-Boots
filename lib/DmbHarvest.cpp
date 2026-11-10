/*
 * DmbHarvest.cpp, part of VCMI engine (Dead Man's Boots)
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "DmbHarvest.h"

#include "CConfigHandler.h"
#include "DmbRelease.h"
#include "GameLibrary.h"
#include "VCMIDirs.h"
#include "modding/CModHandler.h"
#include "modding/ModDescription.h"
#include "modding/CModVersion.h"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <set>

VCMI_LIB_NAMESPACE_BEGIN

namespace
{
constexpr uint64_t GAME_LIMIT_BYTES = 100ull * 1024 * 1024; // one game's files stop here (as the play recorder's file did)
constexpr uint64_t FOLDER_LIMIT_BYTES = 1024ull * 1024 * 1024; // the output folder's games together
constexpr size_t GAME_LIMIT_COUNT = 40; // games kept in the output folder

std::string escape(const std::string & text)
{
	std::string out;
	out.reserve(text.size() + 8);
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
					snprintf(buf, sizeof(buf), "\\u%04x", c);
					out += buf;
				}
				else
					out += static_cast<char>(c);
		}
	}
	return out;
}

std::string jsonQuoted(const std::string & text)
{
	return "\"" + escape(text) + "\"";
}

std::string nowText()
{
	return boost::posix_time::to_iso_extended_string(boost::posix_time::microsec_clock::local_time()).substr(0, 23);
}

/// a game folder or a file this framework (or the play recorder of 19.3) made: its name starts with the date, "2026-11-10_..."
bool ownEntry(const boost::filesystem::path & path)
{
	const std::string name = path.filename().string();
	if(name.size() < 11 || name[4] != '-' || name[7] != '-' || name[10] != '_')
		return false;
	for(int i : {0, 1, 2, 3, 5, 6, 8, 9})
		if(!std::isdigit(static_cast<unsigned char>(name[i])))
			return false;
	boost::system::error_code ec;
	if(boost::filesystem::is_directory(path, ec))
		return boost::filesystem::exists(path / "manifest.json", ec);
	return path.extension() == ".jsonl";
}

uint64_t sizeOf(const boost::filesystem::path & path)
{
	boost::system::error_code ec;
	if(!boost::filesystem::is_directory(path, ec))
		return boost::filesystem::file_size(path, ec);
	uint64_t total = 0;
	for(boost::filesystem::recursive_directory_iterator it(path, ec), end; !ec && it != end; it.increment(ec))
		if(boost::filesystem::is_regular_file(it->path(), ec))
			total += boost::filesystem::file_size(it->path(), ec);
	return total;
}

/// the oldest games go first when there are more than 40 or more than 1 GB; only the folders and files this framework made are ever removed
void pruneOldGames(const boost::filesystem::path & folder)
{
	try
	{
		std::vector<std::pair<std::string, uint64_t>> entries;
		boost::system::error_code ec;
		for(boost::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
			if(ownEntry(it->path()))
				entries.emplace_back(it->path().string(), sizeOf(it->path()));
		std::sort(entries.begin(), entries.end()); // the names start with the date and time: oldest first
		uint64_t total = 0;
		for(const auto & entry : entries)
			total += entry.second;
		size_t first = 0;
		while(first < entries.size() && (entries.size() - first >= GAME_LIMIT_COUNT || total > FOLDER_LIMIT_BYTES))
		{
			total -= entries[first].second;
			boost::filesystem::remove_all(entries[first].first, ec);
			++first;
		}
	}
	catch(...)
	{
	}
}

/// an exporter that writes the events of its topics as they come
class TopicExporter final : public HarvestExporter
{
	std::string key;
	std::string title;
	std::string text;
	std::set<std::string> topics;
	bool byDefault;

public:
	TopicExporter(std::string key, std::string title, std::string text, std::set<std::string> topics, bool byDefault = true)
		: key(std::move(key)), title(std::move(title)), text(std::move(text)), topics(std::move(topics)), byDefault(byDefault)
	{
	}
	std::string id() const override { return key; }
	std::string name() const override { return title; }
	std::string description() const override { return text; }
	bool onByDefault() const override { return byDefault; }
	bool wants(const std::string & topic) const override { return topics.count(topic) != 0; }
};
}

void HarvestExporter::eventHappened(const HarvestEvent & event, HarvestGame & game)
{
	game.line(id(), event.kind, event.body);
}

struct Harvest::Impl final : public HarvestGame
{
	mutable std::recursive_mutex mutex;
	std::vector<std::shared_ptr<HarvestExporter>> exporters;
	std::vector<std::shared_ptr<HarvestExporter>> active; // the ticked ones, fixed when a game starts
	boost::filesystem::path gameFolder;
	boost::filesystem::ofstream events;
	std::map<std::string, std::unique_ptr<boost::filesystem::ofstream>> ownFiles;
	/// DMB (S093, OmniAI Dev): exporters that threw this game (not called again until the next game, marked in the manifest), and the
	/// files each exporter wrote through appendFile (listed in the manifest)
	std::set<const HarvestExporter *> failed;
	std::map<std::string, std::set<std::string>> filesBy;
	GameInfo info;
	std::string started;
	uint64_t bytes = 0;
	int64_t lines = 0;
	bool open = false;
	bool full = false;
	int day = 0;
	std::chrono::steady_clock::time_point lastFlush = std::chrono::steady_clock::now();

	boost::filesystem::path folder() const override
	{
		return gameFolder;
	}

	/// calls one exporter; one that throws is logged once, skipped for the rest of the game and marked "failed" in the manifest
	template<typename F>
	void call(const std::shared_ptr<HarvestExporter> & exporter, const char * point, F && f)
	{
		if(failed.count(exporter.get()))
			return;
		try
		{
			f();
		}
		catch(const std::exception & e)
		{
			failed.insert(exporter.get());
			logGlobal->error("Harvest: exporter '%s' failed at %s (%s); it is off for the rest of this game", exporter->id(), point, e.what());
		}
		catch(...)
		{
			failed.insert(exporter.get());
			logGlobal->error("Harvest: exporter '%s' failed at %s; it is off for the rest of this game", exporter->id(), point);
		}
	}

	/// counts against the game's limit; false when the game is full (one "file_full" line is written then)
	bool roomFor(size_t size)
	{
		if(full)
			return false;
		if(bytes + size > GAME_LIMIT_BYTES)
		{
			full = true;
			const std::string last = "{\"v\":" + std::to_string(SCHEMA_VERSION) + ",\"t\":" + jsonQuoted(nowText()) + ",\"ev\":\"file_full\",\"x\":\"core\",\"limitBytes\":"
				+ std::to_string(GAME_LIMIT_BYTES) + "}\n";
			events << last;
			events.flush();
			return false;
		}
		bytes += size;
		return true;
	}

	void line(const std::string & exporter, const std::string & kind, const std::string & body) override
	{
		std::lock_guard lock(mutex);
		if(!open)
			return;
		std::string text = "{\"v\":" + std::to_string(SCHEMA_VERSION) + ",\"t\":" + jsonQuoted(nowText()) + ",\"day\":" + std::to_string(day) + ",\"seat\":"
			+ jsonQuoted(info.seat) + ",\"ev\":" + jsonQuoted(kind) + ",\"x\":" + jsonQuoted(exporter) + (body.empty() ? "" : "," + body) + "}\n";
		if(!roomFor(text.size()))
			return;
		events << text;
		++lines;
		// a hero's step is several answers; a flush each made moving choppy (K, October 10th): the stream is flushed at most every half second for them
		const auto now = std::chrono::steady_clock::now();
		if(kind != "game" || now - lastFlush > std::chrono::milliseconds(500))
		{
			events.flush();
			lastFlush = now;
		}
	}

	void appendFile(const std::string & exporter, const std::string & name, const std::string & text) override
	{
		std::lock_guard lock(mutex);
		if(!open || !roomFor(text.size()))
			return;
		std::string fileName = exporter + "-" + name;
		for(auto & c : fileName)
			if(!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.')
				c = '_';
		filesBy[exporter].insert(fileName);
		auto & file = ownFiles[fileName];
		if(!file)
			file = std::make_unique<boost::filesystem::ofstream>(gameFolder / fileName, std::ios::out | std::ios::app | std::ios::binary);
		*file << text;
		file->flush();
	}

	void writeManifest(const std::string & ended, const std::string & why)
	{
		std::string mods;
		try
		{
			if(LIBRARY && LIBRARY->modh)
				for(const auto & mod : LIBRARY->modh->getActiveMods())
					mods += std::string(mods.empty() ? "" : ",") + "{\"id\":" + jsonQuoted(mod) + ",\"version\":" + jsonQuoted(LIBRARY->modh->getModInfo(mod).getVersion().toString()) + "}";
		}
		catch(...)
		{
		}
		std::string list;
		for(const auto & exporter : exporters)
		{
			const bool on = std::find(active.begin(), active.end(), exporter) != active.end();
			std::string files;
			const auto written = filesBy.find(exporter->id());
			if(written != filesBy.end())
				for(const auto & f : written->second)
					files += std::string(files.empty() ? "" : ",") + jsonQuoted(f);
			list += std::string(list.empty() ? "" : ",") + "{\"id\":" + jsonQuoted(exporter->id()) + ",\"name\":" + jsonQuoted(exporter->name()) + ",\"version\":"
				+ jsonQuoted(exporter->version()) + ",\"on\":" + (on ? "true" : "false") + ",\"failed\":" + (failed.count(exporter.get()) ? "true" : "false")
				+ ",\"files\":[" + files + "]}";
		}
		const std::string text = "{\n\t\"schema\" : " + std::to_string(SCHEMA_VERSION)
			+ ",\n\t\"gameId\" : " + jsonQuoted(info.map + "#" + std::to_string(info.gameStartTime))
			+ ",\n\t\"dmbVersion\" : " + jsonQuoted(DMB_RELEASE_TEXT)
			+ ",\n\t\"map\" : " + jsonQuoted(info.map)
			+ ",\n\t\"seat\" : " + jsonQuoted(info.seat)
			+ ",\n\t\"difficulty\" : " + std::to_string(info.difficulty)
			+ ",\n\t\"mode\" : " + std::to_string(info.mode)
			+ ",\n\t\"gameStartTime\" : " + std::to_string(info.gameStartTime)
			+ ",\n\t\"started\" : " + jsonQuoted(started)
			+ ",\n\t\"ended\" : " + jsonQuoted(ended)
			+ ",\n\t\"complete\" : " + (ended.empty() ? "false" : "true")
			+ ",\n\t\"why\" : " + jsonQuoted(why)
			+ ",\n\t\"lines\" : " + std::to_string(lines)
			+ ",\n\t\"mods\" : [" + mods + "]"
			+ ",\n\t\"exporters\" : [" + list + "]\n}\n";
		boost::filesystem::ofstream file(gameFolder / "manifest.json", std::ios::out | std::ios::trunc | std::ios::binary);
		file << text;
	}
};

Harvest::Harvest()
	: impl(std::make_shared<Impl>())
{
	// the built-in exporters; the first ones are what the play recorder wrote in 19.3, so the same lines are there with the switch on
	add(std::make_shared<TopicExporter>("actions", "Actions", "Every request each seat this PC runs sends: moves, builds, recruits, trades, spells, battle commands, end of turn.", std::set<std::string>{"request"}));
	add(std::make_shared<TopicExporter>("answers", "Game answers", "What the game answered and applied: objects visited, resources, levels, heroes and towns changing.", std::set<std::string>{"answer"}));
	add(std::make_shared<TopicExporter>("battles", "Battles", "Battles, with both armies, every action and the losses.", std::set<std::string>{"battle"}));
	add(std::make_shared<TopicExporter>("snapshots", "Turn snapshots", "Each seat's state at the start of its turn: resources, income, towns, buildings, heroes and armies.", std::set<std::string>{"snapshot"}));
	add(std::make_shared<TopicExporter>("results", "Results", "Who won or lost, and how.", std::set<std::string>{"result"}));
	add(std::make_shared<TopicExporter>("settings", "Settings", "The settings when the game starts, and every change made during it.", std::set<std::string>{"setting"}));
	add(std::make_shared<TopicExporter>("mods", "Mods", "The mods in use and their versions, when the game starts.", std::set<std::string>{"mods"}));
	add(std::make_shared<TopicExporter>("interface", "Windows and selections", "Windows opened and closed, and what the player selected.", std::set<std::string>{"interface"}));
	add(std::make_shared<TopicExporter>("ai-reasons", "Computer players' reasons", "Why a computer player chose what it did, from the computer players that report it (OmniAI).", std::set<std::string>{"ai_reason"}));
	add(std::make_shared<TopicExporter>("screenshots", "Screenshots", "A picture of the screen at the start of each of your turns, at most 300 a game.", std::set<std::string>{"picture"}, false));
}

HarvestAutoAdd::HarvestAutoAdd(std::shared_ptr<HarvestExporter> exporter)
{
	Harvest::get().add(std::move(exporter));
}

Harvest & Harvest::get()
{
	static Harvest instance;
	return instance;
}

void Harvest::add(std::shared_ptr<HarvestExporter> exporter)
{
	if(!exporter)
		return;
	std::lock_guard lock(impl->mutex);
	for(const auto & existing : impl->exporters)
		if(existing->id() == exporter->id())
			return;
	impl->exporters.push_back(std::move(exporter));
}

std::vector<std::shared_ptr<HarvestExporter>> Harvest::exporters() const
{
	std::lock_guard lock(impl->mutex);
	return impl->exporters;
}

bool Harvest::switchedOn()
{
	const auto & node = settings["general"]["harvest"]["enabled"];
	return node.isBool() ? node.Bool() : true;
}

bool Harvest::ticked(const HarvestExporter & exporter)
{
	const auto & node = settings["general"]["harvest"]["exporters"][exporter.id()];
	return node.isBool() ? node.Bool() : exporter.onByDefault();
}

boost::filesystem::path Harvest::outputFolder()
{
	const std::string chosen = settings["general"]["harvest"]["folder"].String();
	if(!chosen.empty())
		return boost::filesystem::path(chosen);
	return VCMIDirs::get().userDataPath() / "PlayLogs";
}

void Harvest::startGame(const GameInfo & info)
{
	try
	{
		std::lock_guard lock(impl->mutex);
		if(impl->open || !switchedOn())
			return;
		impl->active.clear();
		for(const auto & exporter : impl->exporters)
			if(ticked(*exporter))
				impl->active.push_back(exporter);
		if(impl->active.empty())
			return;

		const auto folder = outputFolder();
		boost::filesystem::create_directories(folder);
		pruneOldGames(folder);
		std::string map = info.map;
		const auto slash = map.find_last_of("/\\");
		if(slash != std::string::npos)
			map = map.substr(slash + 1);
		if(map.empty())
			map = "random";
		for(auto & c : map)
			if(!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_')
				c = '_';
		std::string stamp = boost::posix_time::to_iso_extended_string(boost::posix_time::second_clock::local_time());
		for(auto & c : stamp)
			if(c == 'T')
				c = '_';
			else if(c == ':')
				c = '-';
		impl->gameFolder = folder / (stamp + "_" + map.substr(0, 60));
		for(int n = 2; boost::filesystem::exists(impl->gameFolder); ++n)
			impl->gameFolder = folder / (stamp + "_" + map.substr(0, 60) + "_" + std::to_string(n));
		boost::filesystem::create_directories(impl->gameFolder);
		impl->events.open(impl->gameFolder / "events.jsonl", std::ios::out | std::ios::app | std::ios::binary);
		impl->open = impl->events.is_open();
		if(!impl->open)
			return;
		impl->info = info;
		impl->started = nowText();
		impl->bytes = 0;
		impl->lines = 0;
		impl->full = false;
		impl->ownFiles.clear();
		impl->failed.clear();
		impl->filesBy.clear();
		impl->writeManifest("", "");
		for(const auto & exporter : impl->active)
			impl->call(exporter, "game start", [&]() { exporter->gameStarted(*impl); });
	}
	catch(...)
	{
		impl->open = false;
	}
}

void Harvest::stopGame(const std::string & why)
{
	std::lock_guard lock(impl->mutex);
	if(!impl->open)
		return;
	for(const auto & exporter : impl->active)
		impl->call(exporter, "game end", [&]() { exporter->gameEnded(why, *impl); });
	try
	{
		impl->writeManifest(nowText(), why);
		impl->events.close();
		impl->ownFiles.clear();
	}
	catch(...)
	{
	}
	impl->open = false;
	impl->active.clear();
}

bool Harvest::isOpen() const
{
	std::lock_guard lock(impl->mutex);
	return impl->open;
}

bool Harvest::wanted(const std::string & topic) const
{
	std::lock_guard lock(impl->mutex);
	if(!impl->open)
		return false;
	for(const auto & exporter : impl->active)
		if(exporter->wants(topic))
			return true;
	return false;
}

void Harvest::emit(const std::string & topic, const std::string & kind, const std::string & body)
{
	std::lock_guard lock(impl->mutex);
	if(!impl->open)
		return;
	// the game's own lines (its start and end) are written once, whichever exporters are ticked
	if(topic == "core")
	{
		impl->line("core", kind, body);
		return;
	}
	const HarvestEvent event{topic, kind, body};
	for(const auto & exporter : impl->active)
	{
		if(!exporter->wants(topic))
			continue;
		impl->call(exporter, topic.c_str(), [&]() { exporter->eventHappened(event, *impl); });
	}
}

void Harvest::setDay(int day)
{
	std::lock_guard lock(impl->mutex);
	impl->day = day;
}

boost::filesystem::path Harvest::gameFolder() const
{
	std::lock_guard lock(impl->mutex);
	return impl->open ? impl->gameFolder : boost::filesystem::path();
}

VCMI_LIB_NAMESPACE_END
