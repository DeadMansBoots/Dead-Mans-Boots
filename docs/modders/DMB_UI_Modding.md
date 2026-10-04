# UI modding standards (Dead Man's Boots)

This page is the contract between Dead Man's Boots (DMB) and mods that change or add to its
screens. It says where a mod hooks in, and what it does so that it never collides with another mod
doing something similar. It grows: each time a mod needs a hook nobody planned for, the hook is
added to DMB and written down here, with the addon API level that brings it.

Layouts are VCMI's configurable widgets ([Configurable_Widgets.md](Configurable_Widgets.md)). Mods
that bring code, AI plugins and map generators, are in [DMB_Addons.md](DMB_Addons.md). A map
generator mod's own tab, and the golden arrows that switch between it and VCMI's own random map, are
a separate contract: [DMB_Map_Generation_Modes.md](DMB_Map_Generation_Modes.md).

## The addon API level

A mod names the lowest DMB addon API level it needs in its `mod.json`:

```json
"dmb" : { "api" : 2 }
```

On a DMB below that level the mod counts as incompatible: the launcher shows it so, and the game
does not load it, as VCMI does with `compatibility`. DMB versions before the level existed (0.1.0
test candidates 1 and 2) do not know the key and cannot enforce it; a mod that needs level 2 should
not be offered to them.

| Level | Brings |
| --- | --- |
| 1 | AI plugins, map generators, the mod catalog's code pins |
| 2 | the `pages` widget, `tabPages`, settings-bound labels, this check |
| 3 | map generators that make the game's map at Begin ([DMB_Addons.md](DMB_Addons.md)), `valueTexts` |
| 4 | chooser buttons, `valueTexts` for numbers |
| 5 | main menu backgrounds a mod adds to the pool (below) |

## Rules every UI mod follows

1. Add through a hook; do not replace a whole file to add to it. When two mods replace the same
   layout file, only the one loaded last shows, and the other mod's change is lost without a word.
   A page for someone else's tab goes in through `tabPages`, below.
2. Put your mod's ID in every name you create: text keys (`vcmi.<modID>.<name>`), stored settings
   (`persistent:<modID>/<name>`), pages ids, and your own image names. A name without it will one
   day meet another mod's.
3. Store settings in `persistent:` paths. `persistentStorage.json` has no schema, so nothing
   erases your keys, including an official VCMI client that shares the user folder.
4. Use only the callbacks the screen you extend offers, by their names below. A callback name that
   does not exist is logged and does nothing.
5. Name the addon API level you need in `mod.json`, as above, whenever you use anything from this
   page.

## The pages widget

Any layout may hold a widget of type `pages`: several page layouts shown one at a time, with the
shown page's title between a previous and a next arrow, the way Heroes III's own Random Map Setup
pages.

```json
{
    "type" : "pages",
    "name" : "pages",
    "id" : "myModTab",
    "pages" : [
        { "layout" : "config/widgets/myModTab/first.json", "title" : "vcmi.myMod.page.first" },
        { "layout" : "config/widgets/myModTab/second.json", "title" : "vcmi.myMod.page.second" }
    ],
    "position" : { "x" : 0, "y" : 0 },
    "title" : { "font" : "big", "color" : "yellow", "alignment" : "center", "position" : { "x" : 222, "y" : 36 } },
    "previous" : { "image" : "SCNRBLF", "position" : { "x" : 66, "y" : 28 } },
    "next" : { "image" : "SCNRBRT", "position" : { "x" : 362, "y" : 28 } },
    "remember" : "persistent:myMod/lastPage"
}
```

- `id` is how other mods find the widget to add pages to it. Leave it out, and nobody can.
- `pages` lists the layout's own pages. Each layout path is found in the mod the tab comes from.
- `position` is where the pages' layouts sit in the owner; the title and the arrows are placed in
  the owner's own coordinates.
- `title` is a label; it shows the shown page's `title` text. A title longer than the room between
  the arrows is cut to fit, so keep page titles short: about 20 letters in the big font.
- `previous` and `next` are buttons. They step through the pages and wrap around, the keyboard's
  left and right arrows press them, and their hover text names the page they lead to. With one
  page they are hidden. Either may be left out.
- `remember` keeps the shown page between visits.

Each page gets its owner's callbacks, conditionals and variables, so a page's button reaches the
same code as a button on the owner. A page has no background of its own; the owner's shows through.

## Adding a page to another tab: tabPages

A mod adds pages to any pages widget, of its own or of another mod, by the widget's `id`:

```json
"tabPages" : [
    { "target" : "mapGen", "layout" : "config/widgets/myMod/page.json", "title" : "vcmi.myMod.page" }
]
```

The pages of every enabled mod join after the widget's own, in load order. The layout path is found
in the mod that adds the page. The page gets the owner's callbacks like the owner's own pages. A mod
that is disabled, or incompatible under the addon API level, adds nothing.

Pages ids in use:

| id | Screen | Callbacks a page may use |
| --- | --- | --- |
| `randomMap` | the lobby's Random Map tab | `toggleMapSize`, `toggleTwoLevels`, `setPlayersCount`, `setTeamsCount`, `setCompOnlyPlayers`, `setCompOnlyTeams`, `setWaterContent`, `setMonsterStrength`, `teamAlignments` |
| `advancedOptions` | the lobby's Advanced Options | the tab's own, as in `config/widgets/playerOptionsTab.json` |
| `turnOptions` | the lobby's Turn Options | the tab's own, as in `config/widgets/turnOptionsTab.json` |
| `extraOptions` | the lobby's Extra Options | the tab's own, as in `config/widgets/extraOptionsTab.json` |
| `mapGen` | OmniMapGen's tab, from its release that uses the pages widget | `activateMapGenPage`, `resetMapGenDefaults`, `generateMapGenMap`, `chooseMapGenTemplate` |

A stock screen has no pages of its own. Until a mod adds one, it looks as it always has; then its
own content becomes page 1, its title shows the page's title between the arrows, and what belongs
to every page stays: the background and the title, and on the Random Map tab Show Random Maps too.
A page sits on the screen's background picture, so what that picture draws shows behind it: on
Advanced Options, the empty column boxes and the turn slider of the original screen. A page that
needs the room covers them with a picture of its own. More stock screens will be listed here as
they take pages.

## Settings-bound widgets

A widget whose config names a `setting` opens on the stored value and writes every change back, so a
page of options needs no code:

- `toggleGroup`: the stored number selects a toggle;
- `toggleButton`: on or off;
- `slider`: a number; `valueLabel` names a label that shows it, formatted by `valueMin`,
  `valueMax`, `valueStep`, `valueDefault`, `valueDecimals`, `valueDisplayScale`, `valueSuffix` and
  `valueNames`. VCMI lets every horizontal slider take the left and right arrow keys, so on a screen
  with several they all move at once. On a page of a pages widget, and on a map generator's tab,
  sliders take no keys (the arrows turn the page); elsewhere `"keyboard": false` turns them off for
  one slider, and `"keyboard": true` turns them back on;
- `label`: shows the stored text, or its `emptyText` while there is none (level 2); a stored value
  named in `valueTexts` shows that text instead, such as `"valueTexts": { "random":
  "vcmi.myMod.random" }` (level 3). A number is named as it would be written, `"2"` or `"0.5"`
  (level 4);
- `button` with `options`: a chooser (level 4). Pressed, it lists the options' texts under its
  `title` (and `help`, when given) and stores the value picked; labels on the same layout bound to
  the setting show the new value at once. Each option is `[value, text]`, the value a number or a
  string:

```json
{ "type" : "button", "image" : "RanButton150", "position" : { "x" : 40, "y" : 150 },
  "setting" : "persistent:myMod/waterShape",
  "options" : [ [ 0, "vcmi.myMod.waterShape.lakes" ], [ 1, "vcmi.myMod.waterShape.seas" ] ],
  "title" : "vcmi.myMod.waterShape.title" }
```

  A chooser needs no `callback`; OmniMapGen's layouts name it `chooseMapGenOption`, which is the
  same.

A `setting` is a path with `/` between its parts: `persistent:myMod/speed` in
`persistentStorage.json`, or a plain path into `settings.json`, which the settings schema must
declare or VCMI erases it.

## Art DMB generates for layouts

DMB ships no game art. It builds these from the player's own Heroes III files when the game starts,
and a layout names them like any image. They are shared: use them, never replace them.

| Name | Size | Made from | For |
| --- | --- | --- | --- |
| `RanButton150`, `RanButton83`, `RanButton62`, `RanButton50` | that wide, 32 high | RANWEAK, without its word | a row of 2, 3, 4 or 5 choices, as the Random Map Setup's |
| `RanShowButton337`, `RanShowButton166` | that wide, 40 high | RANSHOW, without its words | the big gold button, full width or two side by side |
| `MapGenButton64`, `MapGenButton80`, `MapGenButton190` | that wide, 20 high | GSPBUT2 | lobby-style buttons |
| `DmbSizeS`, `DmbSizeM`, `DmbSizeL`, `DmbSizeXL` | 44 by 33 | RANSIZS to RANSIZX, as they are | map sizes, as the Random Map Setup's |
| `DmbSizeH`, `DmbSizeXH`, `DmbSizeG`, `DmbSizeC` | 44 by 33 | RANSIZX, lettered anew from the game's BIGFONT capitals, embossed as its letters are | sizes past XL, and a custom size (level 4) |
| `DmbSizeS33` to `DmbSizeC33` (S, M, L, XL, H, XH, G, C) | 33 by 33 | RANSIZX with eleven columns out of its middle, every size lettered as H to C are | all eight and the game's two-level button (`RANUNDR`, 44 wide) in one 312 pixel row (level 4) |

The Ran buttons have RANWEAK's four frames; as toggle buttons they take `"imageOrder": [0, 1, 1, 3]`,
as the Random Map Setup's do, so a selected one wears the gold frame. A button's word is a label in
its `items`, centred on it; the stock look is `"font": "big"`, yellow on the blue buttons, and on the
gold `"style": "engraved"` (level 4), the words written as the game writes its own gold buttons'. A
plain color is written `"color": [0, 0, 0, 255]` (a color name VCMI knows, or red, green, blue and
alpha).

An engraved label in the big font is drawn in the lettering of the game's gold buttons (BEGIN, SAVE,
RESTART): capitals 22 pixels tall, lifted from the player's own copies of those buttons, so they match
the game's to the pixel. No font of the game's has that lettering, and it has these letters only: A to
Z without J, Q and Z, and the space. It needs room: the words stay off the scrollwork at the bar's
ends, 21 pixels at each, so `RanShowButton166` holds about five letters and `RanShowButton337` about
thirteen. Some widths, in pixels: Begin 106, Reset 107, Create 133, Defaults 178, Generate 183.
Words that do not fit, or need a letter it lacks, or a game in another language, are carved in the
font instead: black letters with a brown edge on their left and a light on their right and underneath.
Any other font is carved that way too.

## Main menu backgrounds

The main menu picks its picture from a pool, each start (`random`, the default) or the one the player chose in the
launcher's Settings, General, Main menu background. The pool holds the menu as the active mods configure it (a mod's own
`config/mainmenu.json`, buttons and all), one picture for each release of the game whose archive holds one (found in the
player's own game files when the game starts: Heroes III Complete, Armageddon's Blade, Horn of the Abyss, any other `.lod`
with a `GamSelBk`), and every picture a mod lists. A picture from a release or a mod is drawn behind the base game's own
menu window, so its buttons stand where the game's do: the art has to leave them room (800x600, the buttons down the right
side, as in the game's own).

Every installed mod that adds a whole menu of its own (a `config/mainmenu.json`: Horn of the Abyss's, Tides of War's, in a
mod or in a sub-mod) is an entry of its own in the pool, named after its top mod ("Horn of the Abyss menu"), whether or not
that mod is active. Menu mods conflict with one another, so only one can be active; choosing another's entry adds its files
to the game for that session and shows its picture and buttons as that mod lays them out. The setting stores `mod:<mod id>`.
"Random" is drawn once each time the game starts.

A player adds any picture of their own by putting a `.png` or `.bmp` (not `.jpg`: the game's image library reads no JPEG) into the `MenuBackgrounds` folder of their
DMB folder (Documents\My Games\Dead Man's Boots\MenuBackgrounds, made by the game the first time it starts). Each file is an
entry named after the file (`dusk_over_the_abyss.png` is "Dusk over the abyss"), joins the random draw, and is drawn behind the
first release's buttons; the setting stores `file:<file name>`. Art for the menu leaves the right side free, as the game's own
does (800x600 proportions, any larger size of the same shape).

The loading screen works the same way: a `.png` or `.bmp` put into the `LoadingScreens` folder of the DMB folder (made by the game the
first time it starts) is an entry named after the file and joins the loading screens. The setting `general.loadingScreen` is `random`
(one of them, or the game's own, each time a game loads; the default), `default` (the game's own) or `file:<file name>`; the launcher's
Settings, General page has the choice beside the main menu background. The picture is cut to 800x600 from the middle, as the menu's
is, and should carry its own progress bar frame at x 395, y 548 (360 by 18 pixels), where the game draws the progress blocks and the
phase text. A larger copy made for sharper screens goes in `LoadingScreens\Upscaled\DATA2X`, `DATA3X` and `DATA4X` (1600x1200,
2400x1800 and 3200x2400), named `DMBLoad-<file name without its extension, lower case, anything but letters and digits a dash>.png`.

A mod adds pictures with a file `config/mainMenuBackgrounds.json` (every active mod's file is merged; level 5):

```json
{
	"backgrounds" : [
		{ "id" : "mymod.dusk", "title" : "Dusk", "image" : "MYMODDUSK" }
	]
}
```

`id` is what the setting stores (put the mod's name in it so two mods never share one), `image` the name of a picture the
mod provides (an `Images` file, 800x600). A mod switched off drops out of the pool; a player whose setting names an entry
that is gone gets `random` again. A mod that wants its own buttons as well sets `config/mainmenu.json` as before, and
that menu is the pool's "As the active mods set it".

## Asking for a hook

A mod that needs a hook this page does not list opens an idea on
[DMB's issues](https://github.com/DeadMansBoots/Dead-Mans-Boots/issues/new/choose), naming the
screen and what the mod wants to add there. The hook it becomes is added to DMB, gets the next
addon API level, and is written down here.
