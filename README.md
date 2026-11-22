# Dead Man's Boots

Dead Man's Boots (DMB) is a modern engine for playing Heroes of Might and Magic III. You bring your own copy of the game; DMB's launcher sets it up, installs and updates mods, and starts it. DMB keeps its own settings, saves and mods, apart from any other Heroes III engine you have installed.

In DMB, mods can change the game's interface as well as its content, and DMB's own mod catalog offers mods made for DMB beside the community's. A mod that carries program code runs only when the catalog vouches for that exact code.

## Download

Windows installers are on the [Releases](https://github.com/DeadMansBoots/Dead-Mans-Boots/releases) page. DMB does not include the game itself: you need your own copy of Heroes of Might and Magic III, and the launcher asks for its files the first time it starts. If you already play VCMI, it offers to copy your setup across, and leaves the original untouched.

The installer is not signed yet, so Windows may say it protected your PC. Choose More info, then Run anyway.

## Known problems and in construction

Built is approximate: how much of the feature exists and has been seen working in a real game. The current download is beta 19.9, a test build for playtesting.

In beta 19.9:

| Feature | Status | Built |
| --- | --- | --- |
| Installer, updates and the update notice | Working | 80% |
| Turn progress bars that show how far each player's turn is | Working | 80% |
| Folding the turn progress bars with a button, and showing chosen players' bars | Working | 80% |
| A picture in the random map lobby of the map your settings will make, full screen with a click, the underground one arrow away | Partly built | 60% |
| Brown leather around the menus and loading screens | Working | 90% |
| Loading text clear of the bar's gold ornament | Working | 90% |
| Mod catalog and mod list in the launcher, with the check for mods that need a newer game version | Working | 80% |
| "Install Anyway" for a mod version that needs a newer game version (installed and left switched off) | Partly built | 60% |
| Random maps made by OmniMapGen when you press Begin | Working | 70% |
| The map template panel: one scrolling page, full-width pictures of the map the generator makes for each template, full-screen view | Partly built | 60% |
| The Water layout choice: the list of layouts beside the chosen one's picture and description | Working | 80% |
| The Scenario Description shows the chosen template's own description | Working | 90% |
| Random water content: the water amount shows the range it is drawn from | Partly built | 50% |
| A failed update says why above the "Try Again" button | Working | 90% |
| Chat with All, Team and Spectators channels, each player's lines in their colour | Partly built | 70% |
| Watching a game without a seat (Pause, Speed, Battles) | Partly built | 60% |
| Music in a watched game | Partly built | 40% |
| Gameplay data recording to a folder on your PC | Partly built | 60% |
| Dark and gold look, the game's own lettering in the launcher, bigger map sizes | Partly built | 60% |
| Letting people join a running game, and coming back after a dropped connection | Partly built | 65% |
| Reporting a problem from inside the game (F12) | Partly built | 60% |
| A computer player and difficulty for each seat (the computer players that come with the game), and a computer player from a mod that cannot load says why | Partly built | 70% |
| Telling a joining player which content or mods differ from the host's | Partly built | 40% |
| Looking into every player's kingdom while watching: resources, towns, armies | Partly built | 60% |
| The gold buttons in the game and the launcher in the lobby BEGIN button's own gold, with its grain | Working | 80% |
| Each computer seat named after the computer player that plays it, and a seat's lobby windows in its player's colour | Partly built | 70% |
| A Template position on every map generator slider, and Reset hands the settings back to the template (OmniMapGen 0.3.37) | Partly built | 60% |
| The underground made like a second overworld, a tick on the Underground page (OmniMapGen 0.3.37) | Partly built | 50% |
| Special buildings from the original maps: Template, Random, Off or On, and how many (OmniMapGen 0.3.37) | Partly built | 50% |

Known problems in beta 19.9: The picture of the map your settings will make needs OmniMapGen 0.3.34 or newer, and OmniMapGen 0.3.36 or newer to colour each region by its terrain and to show the underground's cave floor with the solid rock left empty; older versions colour regions by their kind and fill the underground. The picture shows what your settings make, not the exact map you get when you press Begin, and it leaves out portals, underground passages and neutral towns. The Random water content's range needs OmniMapGen 0.3.36 or newer to draw the amount from it. In the AI Settings window the lettering on both of its buttons is not yet the font of the other custom buttons. The seat row's "Starting Resources" button still opens the game's Handicap window. The new map generator settings need OmniMapGen 0.3.37. Not yet shown working: scrolling the template panel. After going back to the older version of a mod you installed anyway, switch the mod on again in the mod list. Not yet shown in a game: chat lines from several players at once in their own colours (seen for one player so far); resizing the chat by dragging it; the brown leather behind a campaign's opening screen and behind a loading screen started by loading a save during a game; the one-line message for a player on another version, which has not been tried with a real older copy.

Next version, not in beta 19.9:

| Feature | Status | Built |
| --- | --- | --- |
| Objects drawn in the right order (a hedge no longer covers a tent, a building no longer hides behind a glacier) | Partly built | 80% |
| Any colour for any seat, and an options window for every seat, player or computer | In construction | 0% |
| The first redrawn interface, with the original look kept as an option | In construction | 10% |
| "Restrict all settings to original template preferences" for map templates | In construction | 0% |
| The host's full mod list compared with each joining player's, naming every difference | In construction | 0% |
| Choosing your own colours for the menus | In construction | 0% |
| Typing a number where you choose an amount | In construction | 0% |
| A host controls window and a Diplomacy screen | In construction | 0% |

A feature counts as built only once it has been seen working in a real game. Some 0% rows have code written but not yet seen working.

## What DMB adds to VCMI

- Compatibility for mods that change the game's interface, on top of the VCMI layer, installed and updated from the launcher like any other mod. A mod that brings code runs only when DMB's mod catalog vouches for that exact code.
- Its own mod catalog, [DMB-Mods-Repository](https://github.com/DeadMansBoots/DMB-Mods-Repository): VCMI's mods, plus mods made for DMB, all installed from the launcher.
- Its own user folder, `Documents\My Games\Dead Man's Boots`, so DMB and VCMI never share settings, saves or mods. Earlier versions called it `Documents\My Games\DMB`; the installer and the game move that folder to the new name, once they have checked DMB made it.
- A launcher in the game's leather, or dark, with the game's own buttons.
- A notice in the launcher when a new release is out.

## Bugs and ideas

Use the forms on the [Issues](https://github.com/DeadMansBoots/Dead-Mans-Boots/issues/new/choose) page. The launcher's help page links there, and it can export your logs to attach.

## Mods

Every mod lives in its own repository, wherever its author keeps it, and DMB's own team works the same way: [OmniAI](https://github.com/Renegade1993/OmniAI) and [OmniMapGen](https://github.com/Renegade1993/OmniMapGen) have pages of their own. To list a mod in DMB's launcher, open a pull request that adds its entry to [DMB-Mods-Repository](https://github.com/DeadMansBoots/DMB-Mods-Repository/tree/main/entries).

## Building from source

The `dmb` branch holds DMB's changes on top of VCMI 1.7.5, and it builds the way VCMI does: see VCMI's [Windows build guide](docs/developers/Building_Windows.md) and the other guides beside it. VCMI's own readme is still in [docs](docs/Readme.md).

## Credits and license

DMB is VCMI with changes, and VCMI's authors wrote nearly all of it: see [VCMI on GitHub](https://github.com/vcmi/vcmi) and [vcmi.eu](https://vcmi.eu/). Like VCMI, DMB is free software under the GNU General Public License, version 2 or later: see [license.txt](license.txt). DMB is not affiliated with the owners of Heroes of Might and Magic III.
