/*
 * launchertooltips.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "StdInc.h"

/// DMB: a plain tooltip for each launcher control, one line each, from tooltips.json (made from `For K\Launcher-Tooltips-To-Edit.md` by
/// DMB\release\tooltips_from_md.py; K edits the file, the script makes the json). A control that already has a tooltip keeps it.
namespace LauncherTooltips
{
/// the controls of one window, named as its .ui file names them: the key in the json is "<ui file>/<control name>"
void apply(QWidget * root, const QString & uiFile);
}
