# DMB data acquisition: files, line schema and writing an exporter

Schema version 1 (the `schema` field of each game's `manifest.json`). Code: `client/harvest/` and `client/PlayRecorder.cpp`.

## Where the files go

DMB's settings have one switch for data acquisition (on by default), an output folder (empty means
`<user data>/PlayLogs`) and one tick per exporter. Each game played with the switch on gets its own folder:

    <output folder>/<YYYY-MM-DD>_<HH-MM-SS>_<map>/
        play.jsonl       the recorded lines (below)
        manifest.json    what this folder is
        <exporter files> anything an exporter writes, for example omniai/decisions-red.jsonl

Nothing is written with the switch off. Everything stays on this computer: no packet, no save change, nothing in the lobby, so
players with different settings play together. Limits: `play.jsonl` stops at 100 MB (its last line is then `file_full`); the
output folder keeps the newest 40 games and at most 1 GB, oldest first out. A change to the settings takes effect at the next game.

## manifest.json

| key | meaning |
|---|---|
| `schema` | this document's version |
| `gameStartTime` | the game id: the time the host started the game (StartInfo), the same on every computer in that game |
| `map` | the map as the game names it (StartInfo mapname); with `gameStartTime` it joins files from several computers or programs |
| `seat` | the colour this computer plays, empty for an observer |
| `dmbVersion` | the DMB version that wrote the folder |
| `started` | local time the folder was made |
| `complete` | false while the game runs or when it ended without closing (a crash); true after the game ended normally |
| `mods` | the active mods, each `{ "id", "version" }` |
| `exporters` | each exporter that was ticked: `{ "id", "version", "failed", "notes" }`; `failed` is true when it threw and was switched off for the rest of the game |

## play.jsonl

One JSON object per line. Every line starts with:

| key | meaning |
|---|---|
| `t` | local time, `YYYY-MM-DDTHH:MM:SS.mmm` |
| `day` | the game day (1 is the first day), when a game is running |
| `seat` | this computer's colour |
| `ev` | the kind of line |

Kinds, and the exporter whose tick decides whether they are written:

| `ev` | exporter | what it is |
|---|---|---|
| `game_start` | always | `file`, `map`, `difficulty`, `mode`, `gameStartTime` |
| `settings_at_start` | always | every setting as the game starts |
| `request` | `actions` | an order a seat of this computer sends: `type`, its fields, `player`, `ai` for an AI seat |
| `game` | `answers` | what the game did in reply: `type` (the pack) and its fields; fights carry both armies and losses |
| `snapshot` | `snapshots` | a seat at the start of its turn: `reason`, `player`, resources, income, towns, buildings, heroes, armies, experience |
| `window_open`, `window_close` | `interface` | `window` |
| `select` | `interface` | `object` |
| `setting` | `interface` | `file`, `name`, `old`, `new` |
| `result` | always | a player won or lost: `player`, `result` |
| `game_end` | always | `why` |
| `file_full` | always | `limitBytes` |

A reader must skip kinds and keys it does not know: later versions add them. A key changes meaning only with a new `schema`.

## Writing an exporter

1. A class deriving from `Harvest::Exporter` (`client/harvest/Harvest.h`) in its own file under `client/harvest/`: an `id` (also its
   settings key, never shown), a `name` and a one-line `description` (shown beside its tick), a `version`, and
   `enabledByDefault()` if it should start off.
2. Override the call points it needs: `gameStart`, `turnStart` (each seat this computer runs), `request`, `answer`, `battleEnd`,
   `other`, `gameEnd`, and `manifest` for notes in `manifest.json`. Each `Line` has `kind`, `fields` (the JSON body without braces),
   `day` and `player`.
3. Write only inside `Game::folder`, or add lines to `play.jsonl` with `Game::write(kind, fields)`.
4. Add one line for it to `registerExporters` in `client/harvest/Exporters.cpp`, and the file to `client/CMakeLists.txt`. Nothing
   else changes.

Calls run on the game's own thread inside a try block: an exporter that throws is switched off for the rest of that game and noted
in the manifest; the game goes on. Keep each call short. An exporter never records another computer's human player.
