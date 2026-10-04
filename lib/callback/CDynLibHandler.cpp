/*
 * CDynLibHandler.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CDynLibHandler.h"

#include "CGlobalAI.h"

#include "../VCMIDirs.h"
#include "../modding/AIPlugins.h"
#include "../modding/AIAbi.h"

#ifdef STATIC_AI
#  ifdef ENABLE_NULLKILLER_AI
#    include "../../AI/Nullkiller/AIGateway.h"
#  endif
#  ifdef ENABLE_NULLKILLER2_AI
#    include "../../AI/Nullkiller2/AIGateway.h"
#  endif
#  ifdef ENABLE_BATTLE_AI
#    include "../../AI/BattleAI/BattleAI.h"
#  endif
#  ifdef ENABLE_STUPID_AI
#    include "../../AI/StupidAI/StupidAI.h"
#  endif
#  ifdef ENABLE_MMAI
#    include "../../AI/MMAI/MMAI.h"
#  endif
#  include "../../AI/EmptyAI/CEmptyAI.h"
#else
# ifdef VCMI_WINDOWS
#  include <windows.h> //for .dll libs
# else
#  include <dlfcn.h>
# endif // VCMI_WINDOWS
#endif // STATIC_AI

VCMI_LIB_NAMESPACE_BEGIN

/// DMB: a library the game will not run because its ABI number is not the engine's (lib/modding/AIAbi.h)
class AIAbiMismatch : public std::runtime_error
{
public:
	using std::runtime_error::runtime_error;
};

std::string CDynLibHandler::abiProblem(bool hasAbi, int abi, const char * builtFor)
{
	if(hasAbi && abi == AIAbi::VERSION)
		return {};
	if(!hasAbi)
		return "it carries no AI ABI number (it was built before DMB checked them, or without DMB's AI SDK); rebuild it against the SDK "
			"of this DMB (AI ABI " + std::to_string(AIAbi::VERSION) + ", DMB " DMB_VERSION_STRING ")";
	return "it was built for AI ABI " + std::to_string(abi) + (builtFor && *builtFor ? std::string(" (DMB ") + builtFor + ")" : std::string())
		+ ", and this game is AI ABI " + std::to_string(AIAbi::VERSION) + " (DMB " DMB_VERSION_STRING "); rebuild it against the SDK of this DMB";
}

template<typename rett>
std::shared_ptr<rett> createAny(const boost::filesystem::path & libpath, const std::string & methodName)
{
#ifdef STATIC_AI
	// android currently doesn't support loading libs dynamically, so the access to the known libraries
	// is possible only via specializations of this template
	throw std::runtime_error("Could not resolve ai library " + libpath.generic_string());
#else
	using TGetAIFun = void (*)(std::shared_ptr<rett> &);
	using TGetNameFun = void (*)(char *);

	char temp[150];

	TGetAIFun getAI = nullptr;
	TGetNameFun getName = nullptr;
	using TGetAbiFun = int (*)();
	using TGetBuiltForFun = const char * (*)();
	TGetAbiFun getAbi = nullptr;
	TGetBuiltForFun getBuiltFor = nullptr;

#ifdef VCMI_WINDOWS
#ifdef __MINGW32__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
	HMODULE dll = LoadLibraryExW(libpath.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (dll)
	{
		getName = reinterpret_cast<TGetNameFun>(GetProcAddress(dll, "GetAiName"));
		getAI = reinterpret_cast<TGetAIFun>(GetProcAddress(dll, methodName.c_str()));
		getAbi = reinterpret_cast<TGetAbiFun>(GetProcAddress(dll, "GetAiAbiVersion"));
		getBuiltFor = reinterpret_cast<TGetBuiltForFun>(GetProcAddress(dll, "GetAiBuiltFor"));
	}
#ifdef __MINGW32__
#pragma GCC diagnostic pop
#endif
#else // !VCMI_WINDOWS
	void *dll = dlopen(libpath.string().c_str(), RTLD_LOCAL | RTLD_LAZY);
	if (dll)
	{
		getName = reinterpret_cast<TGetNameFun>(dlsym(dll, "GetAiName"));
		getAI = reinterpret_cast<TGetAIFun>(dlsym(dll, methodName.c_str()));
		getAbi = reinterpret_cast<TGetAbiFun>(dlsym(dll, "GetAiAbiVersion"));
		getBuiltFor = reinterpret_cast<TGetBuiltForFun>(dlsym(dll, "GetAiBuiltFor"));
	}
	else
	{
		logGlobal->error("Cannot open dynamic library '%s'. Reason: %s", libpath.string(), dlerror());
	}
#endif // VCMI_WINDOWS

	if (!dll)
	{
		logGlobal->error("Cannot open dynamic library (%s). Throwing...", libpath.string());
		throw std::runtime_error("Cannot open dynamic library");
	}
	else if(!getName || !getAI)
	{
		logGlobal->error("%s does not export method %s", libpath.string(), methodName);
#ifdef VCMI_WINDOWS
		FreeLibrary(dll);
#else
		dlclose(dll);
#endif
		throw std::runtime_error("Cannot find method " + methodName);
	}

	// DMB: the library's ABI number is read from the library itself, before anything of it runs
	if(const std::string problem = CDynLibHandler::abiProblem(getAbi != nullptr, getAbi ? getAbi() : 0, getBuiltFor ? getBuiltFor() : nullptr); !problem.empty())
	{
		logGlobal->error("%s cannot be used: %s", libpath.string(), problem);
#ifdef VCMI_WINDOWS
		FreeLibrary(dll);
#else
		dlclose(dll);
#endif
		throw AIAbiMismatch(problem);
	}

	getName(temp);
	logGlobal->info("Loaded %s", temp);

	std::shared_ptr<rett> ret;
	getAI(ret);
	if(!ret)
		logGlobal->error("Cannot get AI!");

	return ret;
#endif // STATIC_AI
}

#ifdef STATIC_AI

template<>
std::shared_ptr<CGlobalAI> createAny(const boost::filesystem::path & libpath, const std::string & methodName)
{
#ifdef ENABLE_NULLKILLER2_AI
	if(libpath.stem() == "libNullkiller2")
		return std::make_shared<NK2AI::AIGateway>();
#endif

#ifdef ENABLE_NULLKILLER_AI
	if(libpath.stem() == "libNullkiller")
		return std::make_shared<NKAI::AIGateway>();
#endif

	return std::make_shared<CEmptyAI>();
}

template<>
std::shared_ptr<CBattleGameInterface> createAny(const boost::filesystem::path & libpath, const std::string & methodName)
{
#ifdef ENABLE_BATTLE_AI
	if(libpath.stem() == "libBattleAI")
		return std::make_shared<CBattleAI>();
#endif

#ifdef ENABLE_STUPID_AI
	if(libpath.stem() == "libStupidAI")
		return std::make_shared<CStupidAI>();
#endif

#ifdef ENABLE_MMAI
	if(libpath.stem() == "libMMAI")
		return std::make_shared<MMAI::BAI::Router>();
#endif

	return std::make_shared<CEmptyAI>();
}

#endif // STATIC_AI

boost::filesystem::path CDynLibHandler::findAILibrary(const std::string & aiName, bool battle)
{
#ifdef STATIC_AI
	// no libraries to look for: createAny picks the built-in AI by name
	return VCMIDirs::get().fullLibraryPath("AI", aiName);
#else
	const boost::filesystem::path stock = VCMIDirs::get().fullLibraryPath("AI", aiName);
	if(boost::filesystem::exists(stock))
		return stock;

	for(const auto & plugin : AIPlugins::active())
	{
		if(plugin.name != aiName || !(battle ? plugin.battle : plugin.adventure))
			continue;
		if(plugin.problem.empty())
			return plugin.path;
		logGlobal->warn("AI plugin %s (mod %s) cannot load: %s", plugin.name, plugin.modID, plugin.problem);
	}
	return {};
#endif
}

template<typename rett>
std::shared_ptr<rett> createAnyAI(const std::string & dllname, const std::string & methodName)
{
	const bool battle = methodName == "GetNewBattleAI";
	std::string name = dllname;
	boost::filesystem::path filePath = CDynLibHandler::findAILibrary(name, battle);

	// DMB: an AI the settings name but the game does not have (a plugin removed or refused, say)
	// falls back to the first of VCMI's own that is here, where it used to end the game
	if(filePath.empty())
	{
		const std::vector<std::string> fallbacks = battle
			? std::vector<std::string>{"BattleAI", "StupidAI"}
			: std::vector<std::string>{"Nullkiller2", "Nullkiller", "EmptyAI"};
		for(const auto & fallback : fallbacks)
		{
			filePath = CDynLibHandler::findAILibrary(fallback, battle);
			if(!filePath.empty())
			{
				logGlobal->warn("AI %s is not installed or cannot load; %s plays instead", dllname, fallback);
				name = fallback;
				break;
			}
		}
		if(filePath.empty())
			filePath = VCMIDirs::get().fullLibraryPath("AI", dllname); // nothing better: fail as before
	}

	logGlobal->info("Opening %s from %s", name, filePath.string());
	std::shared_ptr<rett> ret;
	try
	{
		ret = createAny<rett>(filePath, methodName);
	}
	catch(const AIAbiMismatch & mismatch)
	{
		// DMB: a library built for another AI ABI is not run; VCMI's own AI plays, as when the plugin is missing, and the log says why
		logGlobal->warn("AI %s is refused: %s", name, mismatch.what());
		const std::vector<std::string> fallbacks = battle
			? std::vector<std::string>{"BattleAI", "StupidAI"}
			: std::vector<std::string>{"Nullkiller2", "Nullkiller", "EmptyAI"};
		for(const auto & fallback : fallbacks)
		{
			const auto path = CDynLibHandler::findAILibrary(fallback, battle);
			if(path.empty() || fallback == name)
				continue;
			try
			{
				logGlobal->info("Opening %s from %s", fallback, path.string());
				ret = createAny<rett>(path, methodName);
				name = fallback;
				break;
			}
			catch(const std::exception & e)
			{
				logGlobal->warn("AI %s cannot be used either: %s", fallback, e.what());
			}
		}
		if(!ret)
			throw;
	}
	ret->dllName = name;
	return ret;
}

std::shared_ptr<CGlobalAI> CDynLibHandler::getNewAI(const std::string & dllname)
{
	return createAnyAI<CGlobalAI>(dllname, "GetNewAI");
}

std::shared_ptr<CBattleGameInterface> CDynLibHandler::getNewBattleAI(const std::string & dllname)
{
	return createAnyAI<CBattleGameInterface>(dllname, "GetNewBattleAI");
}

#if SCRIPTING_ENABLED
std::shared_ptr<scripting::Module> CDynLibHandler::getNewScriptingModule(const boost::filesystem::path & dllname)
{
	return createAny<scripting::Module>(dllname, "GetNewModule");
}
#endif

VCMI_LIB_NAMESPACE_END
