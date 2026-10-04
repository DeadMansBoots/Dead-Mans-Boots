/*
 * CDynLibHandler.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

VCMI_LIB_NAMESPACE_BEGIN

class CGlobalAI;
class CBattleGameInterface;

#if SCRIPTING_ENABLED
namespace scripting
{
	class Module;
}
#endif

class DLL_LINKAGE CDynLibHandler
{
public:
	static std::shared_ptr<CGlobalAI> getNewAI(const std::string & dllname);
	static std::shared_ptr<CBattleGameInterface> getNewBattleAI(const std::string & dllname);

	/// DMB: the library an AI name stands for: VCMI's own AI/<name> first, else an AI plugin of that
	/// name and kind among the active mods (modding/AIPlugins.h). Empty if neither can load.
	/// DMB: why an AI library's ABI number (lib/modding/AIAbi.h) is not the engine's, or nothing when it is.
	/// hasAbi: the library exports GetAiAbiVersion; abi: what it returned; builtFor: GetAiBuiltFor's text, if any
	static std::string abiProblem(bool hasAbi, int abi, const char * builtFor);

	static boost::filesystem::path findAILibrary(const std::string & aiName, bool battle);
#if SCRIPTING_ENABLED
	static std::shared_ptr<scripting::Module> getNewScriptingModule(const boost::filesystem::path & dllname);
#endif
};

VCMI_LIB_NAMESPACE_END
