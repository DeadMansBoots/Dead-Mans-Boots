/*
 * AIAbi.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../Global.h"

VCMI_LIB_NAMESPACE_BEGIN

/// DMB: the binary interface between the game and an AI library. A C++ AI shares classes, standard library and Boost types with
/// the engine, so a library only works with an engine whose shared classes are laid out as they were when it was built. This
/// number says which layout that is. It is raised in any change that alters a class an AI uses (the interfaces in lib/callback,
/// the game state classes in lib/gameState and lib/mapObjects, the bonus system, the serializer), and in nothing else, so a
/// DMB release that leaves those alone loads the plugins built against the one before it.
///
/// An AI library built against DMB's SDK carries the number in two exports, which DMB_AI_ABI_EXPORTS writes (put it once in the
/// library's main.cpp). The loader reads the number from the library itself, so a library cannot claim another one in its
/// mod.json, and refuses a library whose number is not the engine's, or that has none, with a message saying which it was built
/// for and which the game is.
namespace AIAbi
{
	constexpr int VERSION = 1;
}

VCMI_LIB_NAMESPACE_END

#ifdef DMB_VERSION_STRING
#  define DMB_AI_BUILT_FOR DMB_VERSION_STRING
#else
#  define DMB_AI_BUILT_FOR "unknown"
#endif

/// the two exports an AI library carries: its ABI number, and the DMB version it was built against (for messages only)
#define DMB_AI_ABI_EXPORTS \
	extern "C" DLL_EXPORT int GetAiAbiVersion() { return VCMI_LIB_WRAP_NAMESPACE(AIAbi)::VERSION; } \
	extern "C" DLL_EXPORT const char * GetAiBuiltFor() { return DMB_AI_BUILT_FOR; }
