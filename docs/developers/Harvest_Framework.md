# Gameplay data framework (Dead Man's Boots)

Dead Man's Boots can record what happens in a game into a folder on the player's PC, for analytics, AI training and bug hunting. Nothing is sent anywhere. The code is `lib/DmbHarvest.h` and `lib/DmbHarvest.cpp`; the game's own events come from `client/PlayRecorder.cpp`.

## For the player

The launcher's Settings have a "Gameplay Data" page:

- the switch, "Record gameplay data on this PC" (on by default);
- the output folder, with a "Choose Folder" button (by default the `PlayLogs` folder in the game's user folder);
- "What is recorded": a list with one checkbox per exporter, folded until "Show the List" is pressed.

A change applies from the next game. With the switch off, nothing is written.

## The files

Each game gets a folder of its own in the output folder, named `<date>_<time>_<map>` (for example `2026-11-10_08-20-14_Jebus_Cross`). It contains:

- `manifest.json`: the game id (`gameId`, the map and the game's start time), the Dead Man's Boots version, the map, the seat of this PC's player, difficulty, mode, when the file started and ended and why, the number of lines, every active mod with its version, and every exporter with its version and whether it was on.
- `events.jsonl`: one JSON object per line, in the order things happened.
- files an exporter writes for itself, named `<exporter>-<name>`, and `pictures\` when the Screenshots exporter is on.

Limits: a game stops writing at 100 MB (its last line is `"ev":"file_full"`); the output folder keeps the newest 40 games and at most 1 GB. Only folders and files this framework made (a name that starts with a date, and for a folder a `manifest.json` inside) are ever removed.

## A line of events.jsonl (schema version 1)

Every line has this head, then the members of its event:

| Key | Meaning |
| --- | --- |
| `v` | the schema version, `1` |
| `t` | local time, `2026-11-10T08:20:14.123` |
| `day` | the game day (0 before the first) |
| `seat` | the colour of this PC's player, `red` |
| `ev` | the kind of line (below) |
| `x` | the exporter that wrote it, or `core` |

Kinds, by the built-in exporter that writes them:

| Exporter (`x`) | `ev` | Contents |
| --- | --- | --- |
| `core` | `game_start`, `game_end`, `file_full` | the game folder, map, difficulty, mode, `gameStartTime`; why the game ended |
| `actions` | `request` | every request a seat this PC runs sends (`player`, and `ai` with the AI's name for an AI seat): moves, builds, recruits, stack and artifact moves, trades, spells, battle commands, end of turn |
| `answers` | `game` | what the server answered and the client applied: objects visited, resources, levels, heroes and towns changing |
| `battles` | `game` | the battle packs: the start with both armies, every action, the result with the losses |
| `snapshots` | `snapshot` | a seat at the start of its turn (`reason`, `player`): resources, income, towns, buildings, mines, dwellings, armies, experience, every hero in full, every town in full |
| `results` | `result` | a player won or lost (`player`, `result`) |
| `settings` | `settings_at_start`, `setting` | every setting when the game starts; a change with `file`, `name`, `old`, `new` |
| `mods` | `mods` | the active mods and their versions |
| `interface` | `window_open`, `window_close`, `select` | windows and the selected object |
| `ai-reasons` | `ai_reason` | what a computer player reports about its choices (OmniAI) |
| `screenshots` | `picture` | the file name of the picture in `pictures\` (off by default) |

The lines of `actions`, `answers`, `battles`, `snapshots`, `results`, `settings` and `interface` are the lines the play recorder of beta 19.3 wrote, with the head above added; a reader of 19.3's `PlayLogs\*.jsonl` reads them unchanged.

Only the seats this PC runs are recorded: on a guest's PC another PC's human player's private state is never written.

## Writing an exporter

An exporter is a class derived from `HarvestExporter`:

```cpp
#include "DmbHarvest.h"

/// writes each seat's gold at the start of its turn to gold.csv
class GoldByDay final : public HarvestExporter
{
public:
	std::string id() const override { return "gold-by-day"; }
	std::string name() const override { return "Gold by day"; }
	std::string description() const override { return "Each seat's gold at the start of its turn, as a table."; }
	bool onByDefault() const override { return false; }
	bool wants(const std::string & topic) const override { return topic == "snapshot"; }

	void gameStarted(HarvestGame & game) override
	{
		game.appendFile(id(), "gold.csv", "player,gold\n");
	}

	void eventHappened(const HarvestEvent & event, HarvestGame & game) override
	{
		const JsonNode line(reinterpret_cast<const std::byte *>(("{" + event.body + "}").data()), event.body.size() + 2, "harvest");
		game.appendFile(id(), "gold.csv", line["player"].String() + "," + std::to_string(line["gold"].Integer()) + "\n");
	}
};
```

Register it once. In `lib`, put `DMB_HARVEST_EXPORTER(GoldByDay)` at file scope in the exporter's own `.cpp` and add the file to `lib/CMakeLists.txt`: it is registered when the library loads, listed in the launcher's Settings, and `DmbHarvest.cpp` does not change. Elsewhere (a client or an AI library), call `Harvest::get().add(std::make_shared<GoldByDay>());` before a game starts; the game uses it and remembers its tick by its id. An exporter that throws is switched off for the rest of that game, logged once, and marked `"failed": true` in `manifest.json`, which also lists the files each exporter wrote.

The points an exporter is called at: `gameStarted`, `eventHappened` for every event of a topic it `wants`, and `gameEnded`. The topics are `request`, `answer`, `battle`, `snapshot`, `result`, `setting`, `mods`, `interface`, `ai_reason` and `picture`. The default `eventHappened` writes the event as one line of `events.jsonl`.

Any code can add events: `Harvest::get().record(topic, kind, body)`, where `body` is the members of a JSON object without the braces, for example `"\"player\":\"red\",\"why\":\"the guard is too strong\""`. An AI library emits its reasons with the topic `ai_reason`.
