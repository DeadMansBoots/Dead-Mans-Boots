/*
 * OmniAIReasons.cpp, part of VCMI engine (Dead Man's Boots)
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "OmniAIReasons.h"

#include "../../lib/VCMIDirs.h"

#include <boost/filesystem.hpp>
#include <fstream>
#include <regex>

namespace Harvest
{
namespace
{
/// OmniAI's folder, as OmniAI itself picks it (OmniAI.cpp omniDir): OMNIAI_DIR when set (test harnesses), else <user data>/OmniAI
boost::filesystem::path omniFolder()
{
	if(const char * own = std::getenv("OMNIAI_DIR"); own && *own)
		return boost::filesystem::path(own);
	return VCMIDirs::get().userDataPath() / "OmniAI";
}

/// True when the file's start event (line 1; within the first 20 lines to be safe) names this game. A file with no start event, or
/// from an OmniAI older than its R6 build (no gameStartTime), never matches.
bool namesThisGame(const boost::filesystem::path & file, int64_t gameStartTime, const std::string & map)
{
	std::ifstream in(file.string());
	std::string line;
	static const std::regex timeKey(R"re("gameStartTime"\s*:\s*(-?\d+))re");
	static const std::regex mapKey(R"re("mapname"\s*:\s*"((?:[^"\\]|\\.)*)")re");
	for(int i = 0; i < 20 && std::getline(in, line); i++)
	{
		if(line.find("\"t\":\"start\"") == std::string::npos)
			continue;
		std::smatch t;
		std::smatch m;
		if(!std::regex_search(line, t, timeKey) || !std::regex_search(line, m, mapKey))
			return false;
		std::string name = m[1].str();
		// OmniAI escapes backslashes and quotes in its strings; undo the two a map path can hold
		for(size_t p = 0; (p = name.find("\\\\", p)) != std::string::npos; ++p)
			name.replace(p, 2, "\\");
		for(size_t p = 0; (p = name.find("\\\"", p)) != std::string::npos; ++p)
			name.replace(p, 2, "\"");
		return std::stoll(t[1].str()) == gameStartTime && name == map;
	}
	return false;
}
}

void OmniAIReasons::gameStart(const Game & game)
{
	copied.clear();
}

void OmniAIReasons::gameEnd(const Game & game, const std::string & why)
{
	const auto source = omniFolder();
	if(!boost::filesystem::is_directory(source))
		return;
	const auto target = game.folder / "omniai";
	for(boost::filesystem::directory_iterator it(source), end; it != end; ++it)
	{
		const auto name = it->path().filename().string();
		if(name.rfind("decisions-", 0) != 0 || it->path().extension() != ".jsonl")
			continue;
		if(!namesThisGame(it->path(), game.gameStartTime, game.map))
			continue;
		boost::filesystem::create_directories(target);
		for(const char * ext : {".jsonl", ".log"})
		{
			auto from = it->path();
			from.replace_extension(ext);
			if(!boost::filesystem::exists(from))
				continue;
			boost::filesystem::copy_file(from, target / from.filename(), boost::filesystem::copy_options::overwrite_existing);
			copied.push_back("omniai/" + from.filename().string() + " " + std::to_string(boost::filesystem::file_size(from)) + " bytes");
		}
	}
}
}
