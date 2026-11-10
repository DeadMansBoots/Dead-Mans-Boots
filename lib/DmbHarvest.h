/*
 * DmbHarvest.h, part of VCMI engine (Dead Man's Boots)
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <boost/filesystem/path.hpp>

VCMI_LIB_NAMESPACE_BEGIN

/// DMB (S093): the gameplay data framework. K, November 7th: "a switch that gets turned on in DMB for data acquisition for various outputs, with the
/// framework supporting any outputs you all can think to build exporters for"; "it covers any and all information you might want to extract from a gameplay
/// session"; "I want it built into dmb so other devs may use it as well (analytics or whatever)"; November 10th: "switch on whatever you need" and "yes,
/// output folder on pc". Everything is written to a folder on this PC; nothing is sent anywhere.
///
/// How it works: the game hands every event to Harvest::record (the play recorder in the client does it for the human's and the AI seats this PC runs; an AI
/// library may emit its own, such as OmniAI's reasons). Each exporter says which topics it wants; an event goes to every exporter that is ticked and wants
/// it. The built-in exporters write the event as one JSON line into the game's events.jsonl; an exporter of your own can do anything with it (its own file,
/// a summary at the end). The format of every line and file is in docs/developers/Harvest_Framework.md.
///
/// Writing an exporter: derive from HarvestExporter, give it an id, a name and a description, say which topics it wants, and register it once with
/// DMB_HARVEST_EXPORTER(MyExporter) at file scope in its own .cpp in lib (or Harvest::get().add(std::make_shared<MyExporter>()) from code that runs
/// before a game starts). An exporter in lib is then listed by the launcher's Settings too, and DmbHarvest.cpp never changes for it.

/// One game's folder and its files, as an exporter sees them
class DLL_LINKAGE HarvestGame
{
public:
	virtual ~HarvestGame() = default;
	/// the game's own folder: <output folder>\<date>_<time>_<map>
	virtual boost::filesystem::path folder() const = 0;
	/// one line into the game's events.jsonl, tagged with the exporter; body is the members of a JSON object, without the braces
	virtual void line(const std::string & exporter, const std::string & kind, const std::string & body) = 0;
	/// text added to a file of the exporter's own in the game folder (<exporter>-<name>); the game's disk limit counts it
	virtual void appendFile(const std::string & exporter, const std::string & name, const std::string & text) = 0;
};

/// One event of a game
struct DLL_LINKAGE HarvestEvent
{
	/// what an exporter asks for: "request", "answer", "battle", "snapshot", "result", "setting", "interface", "ai_reason", "picture", ...
	std::string topic;
	/// the "ev" the line is written with ("request", "game", "snapshot", ...), kept as the play recorder of 19.3 wrote it
	std::string kind;
	/// the members of a JSON object, without the braces (the head "v", "t", "day", "seat", "ev" and "x" is added by the framework)
	std::string body;
};

class DLL_LINKAGE HarvestExporter
{
public:
	virtual ~HarvestExporter() = default;
	/// a short fixed key: the settings remember the tick by it (general.harvest.exporters.<id>)
	virtual std::string id() const = 0;
	virtual std::string name() const = 0;
	/// one plain sentence, shown in the launcher's list
	virtual std::string description() const = 0;
	virtual std::string version() const { return "1"; }
	/// ticked on a fresh install
	virtual bool onByDefault() const { return true; }
	/// the topics this exporter is given
	virtual bool wants(const std::string & topic) const = 0;

	/// the points it is called at: a game starts, an event happens (a request, an answer, a turn start's snapshot, a battle's end, the result...), the game ends
	virtual void gameStarted(HarvestGame & game) {}
	/// default: the event as one line of events.jsonl
	virtual void eventHappened(const HarvestEvent & event, HarvestGame & game);
	virtual void gameEnded(const std::string & why, HarvestGame & game) {}
};

class DLL_LINKAGE Harvest
{
public:
	/// the version of the line format (docs/developers/Harvest_Framework.md); every line carries it as "v"
	static constexpr int SCHEMA_VERSION = 1;

	static Harvest & get();

	void add(std::shared_ptr<HarvestExporter> exporter);
	std::vector<std::shared_ptr<HarvestExporter>> exporters() const;

	/// the switch (settings general.harvest.enabled); read when a game starts
	static bool switchedOn();
	/// whether an exporter is ticked (general.harvest.exporters.<id>, or its default)
	static bool ticked(const HarvestExporter & exporter);
	/// where the games go: general.harvest.folder, or the game's PlayLogs folder when that is empty
	static boost::filesystem::path outputFolder();

	struct GameInfo
	{
		std::string map;
		std::string seat;
		int difficulty = 0;
		int mode = 0;
		int64_t gameStartTime = 0;
	};
	/// opens the game's folder and its manifest; nothing happens when the switch is off
	void startGame(const GameInfo & info);
	void stopGame(const std::string & why);
	bool isOpen() const;
	/// whether a ticked exporter wants this topic (the recorder builds an event only then)
	bool wanted(const std::string & topic) const;
	/// DMB: not named emit, which Qt defines as a macro (the launcher includes this header)
	void record(const std::string & topic, const std::string & kind, const std::string & body);
	/// the game day for the lines' "day" (the client sets it; an AI's line takes the last one set)
	void setDay(int day);
	/// the game's folder, empty when no game is open
	boost::filesystem::path gameFolder() const;

private:
	Harvest();
	struct Impl;
	std::shared_ptr<Impl> impl;
};

/// DMB (S093 93.8, OmniAI Dev): registers an exporter when the library loads, from the exporter's own file. Windows, Linux and macOS build lib as
/// a shared library, which keeps every object file. A build with ENABLE_STATIC_LIBS (Android, iOS) can drop an object file nothing refers to:
/// there, call Harvest::get().add from code that is linked instead.
struct DLL_LINKAGE HarvestAutoAdd
{
	explicit HarvestAutoAdd(std::shared_ptr<HarvestExporter> exporter);
};

#define DMB_HARVEST_EXPORTER(Class) static HarvestAutoAdd dmbHarvestAutoAdd_##Class(std::make_shared<Class>());

VCMI_LIB_NAMESPACE_END
