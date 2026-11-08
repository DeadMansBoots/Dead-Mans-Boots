# Dead Man's Boots

Dead Man's Boots (DMB) is a modern engine for playing Heroes of Might and Magic III. You bring your own copy of the game; DMB's launcher sets it up, installs and updates mods, and starts it. DMB keeps its own settings, saves and mods, apart from any other Heroes III engine you have installed.

In DMB, mods can change the game's interface as well as its content, and DMB's own mod catalog offers mods made for DMB beside the community's. A mod that carries program code runs only when the catalog vouches for that exact code.

## Download

Windows installers are on the [Releases](https://github.com/DeadMansBoots/Dead-Mans-Boots/releases) page. DMB does not include the game itself: you need your own copy of Heroes of Might and Magic III, and the launcher asks for its files the first time it starts. If you already play VCMI, it offers to copy your setup across, and leaves the original untouched.

The installer is not signed yet, so Windows may say it protected your PC. Choose More info, then Run anyway.

## Known problems and in construction

Built is approximate: how much of the feature exists and has been seen working in a real game. The current download is beta 19.2, a test build for playtesting.

In beta 19.2:

| Feature | Status | Built |
| --- | --- | --- |
| Installer, updates and the update notice | Working | 80% |
| Turn progress bars that show how far each player's turn is | Working | 80% |
| Mod catalog and mod list in the launcher | Working | 70% |
| Random maps made by OmniMapGen when you press Begin | Working | 70% |
| Chat with All, Team and Spectators channels | Partly built | 70% |
| Dark and gold look, the game's own lettering in the launcher, bigger map sizes | Working | 60% |
| Letting people join a running game, and coming back after a dropped connection | Partly built | 60% |
| Reporting a problem from inside the game (F12) | Partly built | 60% |
| A record of your own games on your PC, for finding bugs | Working | 60% |

Known problem in beta 19.2: watching a game as an observer does not work properly yet. Clicks on the map are lost while a computer player's hero is moving. This is fixed in a test build.

Next version, not in beta 19.2:

| Feature | Status | Built |
| --- | --- | --- |
| Objects drawn in the right order (a hedge no longer covers a tent) | Working | 80% |
| Observer mode: leave your seat, start a game with every seat a computer, and watch with Pause, Speed and a Battles switch; watched games skip battles by default | Working | 70% |
| Music in a watched game | Partly built | 40% |
| The first redrawn interface, with the original look kept as an option | In construction | 10% |
| A per-seat switch for OmniAI's chat lines, and one for their sound | In construction | 0% |
| Looking into every player's kingdom while watching: resources, towns, armies | In construction | 0% |
| Choosing the computer player and its difficulty for each seat in the lobby (difficulty changes only the two standard computer players for now) | In construction | 0% |
| Typing a number where you choose an amount | In construction | 0% |
| A host controls window and a Diplomacy screen | In construction | 0% |
| The host's map list and the map date on the New Game card | In construction | 0% |
| The starting resource bonus of a Random town | Known problem | 0% |

A feature counts as built only once it has been seen working in a real game. The 0% rows have code written but not yet seen working.

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
