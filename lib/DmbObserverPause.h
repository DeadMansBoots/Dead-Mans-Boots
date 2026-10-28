/*
 * DmbObserverPause.h, part of VCMI engine (DMB addition)
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <atomic>

/// DMB watched games (the project owner, October 23rd: "would like the ability to pause"): the host's observer bar holds the whole game, every AI and the turn timers. The bar sets
/// these; the server that runs inside the same client (a watched game's host) reads them, holds each request from a player while paused and stops its timers,
/// and lets one request through for each step. A server in another process never sees them, so the bar is offered on the host only.
namespace DmbObserverPause
{
inline std::atomic<bool> paused{false};
inline std::atomic<int> steps{0};
}
