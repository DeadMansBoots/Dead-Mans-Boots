/*
 * Harvest.h, part of VCMI engine (Dead Man's Boots)
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../lib/constants/EntityIdentifiers.h"

/// DMB (S093, K 11-07: "a switch that gets turned on in DMB for data acquisition for various outputs, with the framework supporting any
/// outputs you all can think to build exporters for"; roster\SPECTABLE-S093-S095-S101-1110.md rows 93.5 to 93.10).
///
/// The data-acquisition framework. One switch (Settings::enabled), one output folder (Settings::outputFolder), one tick per exporter
/// (Settings::exporterEnabled). Each game the switch is on gets its own folder, <output folder>/<date>_<time>_<map>/, with:
///   play.jsonl     every line the enabled built-in exporters write, one JSON object per line, the same lines as 19.3's PlayLogs
///                  (93.5, 93.7); the schema is docs/modders/DMB_Harvest_Schema.md, its version is "schema" in the manifest
///   manifest.json  the game id (gameStartTime and the map, S077 R4), DMB's version, the map, the active mods with versions, and every
///                  exporter that ran with its version and the files it wrote (93.9)
///   <anything>     an exporter's own files (Game::folder), e.g. omniai/decisions-red.jsonl
/// With the switch off nothing is written and no exporter is called. Local to this PC: no packet, no save change, nothing in the lobby
/// (S077's hard condition, 95.7). Bounded (93.10): play.jsonl stops at 100 MB; the output folder keeps the newest 40 game folders and
/// at most 1 GB, oldest removed first.
namespace Harvest
{
/// What an exporter knows about the game being recorded
struct Game
{
	/// the game id with map (S077 R4): StartInfo::startTime, set once by the host, the same on every PC of the game
	int64_t gameStartTime = 0;
	/// StartInfo::mapname, as the recorder's game_start line writes it
	std::string map;
	/// the seat this client plays (none for an observer without a seat)
	PlayerColor seat;
	/// this game's folder; an exporter writes its own files only inside it
	boost::filesystem::path folder;

	/// Appends one line to play.jsonl: fields is a JSON object body without braces ("\"a\":1,\"b\":\"x\""); the framework adds
	/// t (local time), day, seat and ev = kind in front, as the recorder does. Dropped silently past the 100 MB cap.
	void write(const std::string & kind, const std::string & fields) const;
};

/// One recorded line as the built-in recorder made it, handed to exporters at each call point
struct Line
{
	/// the recorder's ev: request, game, snapshot, result, window_open, window_close, select, setting, ...
	std::string kind;
	/// the JSON object body without braces, as the recorder built it
	std::string fields;
	int day = 0;
	/// the player the line is about, when it is about one
	PlayerColor player;
};

/// An exporter (93.8). Subclass it in its own .cpp under client/harvest/, give it an id and texts, override the call points it
/// needs, and add one line for it to the list in client/harvest/Exporters.cpp (registerExporters). Adding one changes nothing in
/// the framework (Harvest.cpp). (No self-registering static objects: the client is a static library, and the linker drops an object
/// file nothing refers to.) Every call is made inside a try block on
/// the client's thread: an exporter that throws is skipped for the rest of the game and the game goes on. Keep each call short.
class Exporter
{
public:
	virtual ~Exporter() = default;

	/// stable key, the settings key of its tick, e.g. "actions", "omniai.reasons"; never shown
	virtual std::string id() const = 0;
	/// the text beside its tick in the list (93.3)
	virtual std::string name() const = 0;
	/// one short plain line under the name (93.3)
	virtual std::string description() const = 0;
	/// written into the manifest beside the id
	virtual std::string version() const = 0;
	/// its tick when the player has never set it (93.4: the team's exporters on, screenshots off)
	virtual bool enabledByDefault() const { return true; }

	/// call points (93.8), in game order
	virtual void gameStart(const Game & game) {}
	/// the start of a seat's turn, for every seat this client runs (the human's and its AI seats; never another PC's human, S077 R3)
	virtual void turnStart(const Game & game, PlayerColor player, int day) {}
	/// a request a seat of this client sends to the server, before it is sent
	virtual void request(const Game & game, const Line & line) {}
	/// what the server answered: a pack before the client applies it
	virtual void answer(const Game & game, const Line & line) {}
	/// the end of a battle, with both armies and losses
	virtual void battleEnd(const Game & game, const Line & line) {}
	/// any other recorder line (windows, selection, settings changed, the result line)
	virtual void other(const Game & game, const Line & line) {}
	/// the game ends, is left or the client closes; the last call of a game
	virtual void gameEnd(const Game & game, const std::string & why) {}
	/// extra manifest lines: files written, counts (called after gameEnd)
	virtual std::vector<std::string> manifest() const { return {}; }
};

/// What the settings UI lists (93.3), in registration order
struct ExporterInfo
{
	std::string id;
	std::string name;
	std::string description;
	bool enabledByDefault = true;
};

/// The registry. The client calls the call points through the recorder; the UI only lists and ticks.
class Registry
{
public:
	static Registry & get();

	/// used by registerExporters; ids must be unique (a second one with the same id is ignored and logged)
	void add(std::unique_ptr<Exporter> exporter);
	/// every registered exporter, for the settings page's list
	std::vector<ExporterInfo> list() const;

	/// called by PlayRecorder at its own points; each forwards to the enabled exporters and does nothing with the switch off
	void gameStart(int64_t gameStartTime, const std::string & map, PlayerColor seat);
	void turnStart(PlayerColor player, int day);
	void request(const Line & line);
	void answer(const Line & line);
	void battleEnd(const Line & line);
	void other(const Line & line);
	void gameEnd(const std::string & why);
	/// true between gameStart and gameEnd of a game the switch was on for
	bool recording() const;

private:
	Registry() = default;
	struct Impl;
	std::shared_ptr<Impl> impl;
};

/// client/harvest/Exporters.cpp: the list of exporters, one line each; called once by Registry::get()
void registerExporters(Registry & registry);

/// The settings the UI reads and writes (93.1 to 93.4). Local settings (settings.json, "general"), never sent anywhere. A change made
/// during a game takes effect at the next game's start; the recorder logs it as any setting change.
namespace Settings
{
	/// the master switch, general.harvestEnabled (default on, 93.4)
	bool enabled();
	void setEnabled(bool on);

	/// general.harvestFolder as set ("" = the default)
	std::string outputFolder();
	/// the folder games are written to: the setting, or <user data>/PlayLogs when it is empty
	boost::filesystem::path resolvedOutputFolder();
	void setOutputFolder(const std::string & folder);

	/// general.harvestExporters.<id>; an id never set gives that exporter's enabledByDefault()
	bool exporterEnabled(const std::string & id);
	void setExporterEnabled(const std::string & id, bool on);
}
}

