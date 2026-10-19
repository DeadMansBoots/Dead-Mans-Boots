/*
 * AddonCode.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "AddonCode.h"

#include "../CConfigHandler.h"
#include "../VCMIDirs.h"
#include "../filesystem/Filesystem.h"
#include "../json/JsonNode.h"

#include <condition_variable>
#include <map>
#include <mutex>
#include <set>

VCMI_LIB_NAMESPACE_BEGIN

namespace
{
/// SHA-256 (FIPS 180-4). The engine links no cryptography library, and this is all it needs of one.
class Sha256
{
	std::array<uint32_t, 8> state = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	std::array<uint8_t, 64> block = {};
	size_t used = 0;
	uint64_t length = 0;

	static uint32_t rotr(uint32_t x, int n)
	{
		return (x >> n) | (x << (32 - n));
	}

	void compress()
	{
		static const std::array<uint32_t, 64> k = {
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
			0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
			0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
			0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
			0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
			0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
			0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

		std::array<uint32_t, 64> w;
		for(int i = 0; i < 16; ++i)
			w[i] = uint32_t(block[4 * i]) << 24 | uint32_t(block[4 * i + 1]) << 16 | uint32_t(block[4 * i + 2]) << 8 | uint32_t(block[4 * i + 3]);
		for(int i = 16; i < 64; ++i)
		{
			const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
			const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}

		uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4], f = state[5], g = state[6], h = state[7];
		for(int i = 0; i < 64; ++i)
		{
			const uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
			const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
			h = g;
			g = f;
			f = e;
			e = d + t1;
			d = c;
			c = b;
			b = a;
			a = t1 + t2;
		}
		state[0] += a;
		state[1] += b;
		state[2] += c;
		state[3] += d;
		state[4] += e;
		state[5] += f;
		state[6] += g;
		state[7] += h;
	}

public:
	void update(const uint8_t * data, size_t size)
	{
		length += size;
		while(size > 0)
		{
			const size_t take = std::min(size, block.size() - used);
			std::copy(data, data + take, block.begin() + used);
			used += take;
			data += take;
			size -= take;
			if(used == block.size())
			{
				compress();
				used = 0;
			}
		}
	}

	std::string hex()
	{
		const uint64_t bits = length * 8;
		const uint8_t one = 0x80;
		const uint8_t zero = 0;
		update(&one, 1);
		while(used != 56)
			update(&zero, 1);
		std::array<uint8_t, 8> size;
		for(int i = 0; i < 8; ++i)
			size[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
		update(size.data(), size.size());

		static const char digits[] = "0123456789abcdef";
		std::string result;
		for(uint32_t word : state)
			for(int shift = 28; shift >= 0; shift -= 4)
				result += digits[(word >> shift) & 0xf];
		return result;
	}
};
}

std::optional<boost::filesystem::path> AddonCode::modFolder(const std::string & modID)
{
	// the same folder ModManager and CModHandler use for a mod: "MODS/<ID>", a submod under its parent.
	// "initial" is the filesystem that indexes every Mods folder, the game's and the player's
	std::string directory = modID;
	boost::to_upper(directory);
	boost::algorithm::replace_all(directory, ".", "/MODS/");
	const auto folder = CResourceHandler::get("initial")->getResourceName(ResourcePath("MODS/" + directory, EResType::DIRECTORY));
	if(!folder)
		return std::nullopt;
	return boost::filesystem::absolute(*folder);
}

std::string AddonCode::fileHash(const boost::filesystem::path & file)
{
	std::ifstream in(file.c_str(), std::ios::binary);
	if(!in)
		return {};
	Sha256 hash;
	std::vector<char> buffer(1 << 16);
	while(in)
	{
		in.read(buffer.data(), buffer.size());
		hash.update(reinterpret_cast<const uint8_t *>(buffer.data()), static_cast<size_t>(in.gcount()));
	}
	return hash.hex();
}

std::string AddonCode::folderHash(const boost::filesystem::path & folder)
{
	boost::system::error_code ec;
	if(!boost::filesystem::is_directory(folder, ec))
		return {};

	std::vector<std::pair<std::string, std::string>> files; // path relative to the folder, the file's hash
	for(boost::filesystem::recursive_directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
	{
		boost::system::error_code statusError;
		const bool regular = boost::filesystem::is_regular_file(it->path(), statusError);
		if(statusError)
			return {};
		if(!regular)
			continue;
		const std::string hash = fileHash(it->path());
		if(hash.empty())
			return {};
		files.emplace_back(it->path().lexically_relative(folder).generic_string(), hash);
	}
	if(ec)
		return {};
	std::sort(files.begin(), files.end());

	std::string lines;
	for(const auto & [path, hash] : files)
		lines += hash + "  " + path + "\n";
	Sha256 total;
	total.update(reinterpret_cast<const uint8_t *>(lines.data()), lines.size());
	return total.hex();
}

namespace
{
/// what a folder looks like without reading it: each file's path relative to the folder, its size and its write time, sorted by path (the same
/// walk folderHash makes), so replacing any one file with another of the same size and an older time still changes it
std::string folderSignature(const boost::filesystem::path & folder)
{
	boost::system::error_code error;
	std::vector<std::string> files;
	for(boost::filesystem::recursive_directory_iterator it(folder, error), end; !error && it != end; it.increment(error))
	{
		boost::system::error_code fileError;
		if(!boost::filesystem::is_regular_file(it->path(), fileError))
			continue;
		const auto size = boost::filesystem::file_size(it->path(), fileError);
		const auto written = boost::filesystem::last_write_time(it->path(), fileError);
		files.push_back(it->path().lexically_relative(folder).generic_string() + "\n" + std::to_string(size) + "\n" + std::to_string(written));
	}
	std::sort(files.begin(), files.end());
	Sha256 signature;
	for(const auto & file : files)
	{
		signature.update(reinterpret_cast<const uint8_t *>(file.data()), file.size());
		signature.update(reinterpret_cast<const uint8_t *>("\n"), 1);
	}
	return signature.hex();
}

/// the answers "Run it anyway" given in this run, by lowercase mod ID and exact hash; they hold even when runAnywayFile could not be written
std::mutex runAnywayMutex;
std::set<std::pair<std::string, std::string>> runAnywayThisRun;

std::mutex hashCacheMutex;
std::map<std::string, std::pair<std::string, std::string>> hashCache; // folder -> (signature, hash)
/// the folders a caller is hashing now: another caller for the same folder waits for that hash instead of working it out again
std::set<std::string> beingHashed;
std::condition_variable hashFinished;

/// folderHash of a folder that looks as it did the last time it was hashed in this run is not read again (hashing a generator takes about half a second)
std::string cachedFolderHash(const boost::filesystem::path & folder)
{
	const std::string key = folder.string();
	const std::string signature = folderSignature(folder);
	{
		std::unique_lock<std::mutex> lock(hashCacheMutex);
		hashFinished.wait(lock, [&key]() { return beingHashed.count(key) == 0; });
		const auto found = hashCache.find(key);
		if(found != hashCache.end() && found->second.first == signature)
			return found->second.second;
		beingHashed.insert(key);
	}
	const std::string hash = AddonCode::folderHash(folder);
	{
		std::lock_guard<std::mutex> guard(hashCacheMutex);
		if(!hash.empty())
			hashCache[key] = {signature, hash};
		beingHashed.erase(key);
	}
	hashFinished.notify_all();
	return hash;
}
}

boost::filesystem::path AddonCode::pinsFile()
{
	return VCMIDirs::get().userCachePath() / "downloads" / "dmbCodePins.json";
}

boost::filesystem::path AddonCode::testedPinsFile()
{
	return VCMIDirs::get().userConfigPath() / "dmbTestedCode.json";
}

boost::filesystem::path AddonCode::runAnywayFile()
{
	return VCMIDirs::get().userConfigPath() / "dmbRunAnyway.json";
}

void AddonCode::warmFolderHash(const boost::filesystem::path & folder)
{
	cachedFolderHash(folder);
}

std::string AddonCode::rememberRunAnyway(const std::string & modID, const boost::filesystem::path & folder)
{
	const std::string hash = cachedFolderHash(folder);
	if(hash.empty())
	{
		logMod->error("Mod %s: the player chose to run its code anyway, but its code in %s could not be read", modID, folder.string());
		return "its code could not be read";
	}
	const std::string mod = boost::to_lower_copy(modID);
	{
		std::lock_guard<std::mutex> guard(runAnywayMutex);
		runAnywayThisRun.emplace(mod, hash);
	}

	JsonNode all;
	{
		std::ifstream in(runAnywayFile().c_str(), std::ios::binary);
		if(in)
		{
			// read strictly: the lenient parser keeps what it could read of a damaged file, and the rewrite below would then drop the rest without a word.
			// The reading is inside the try too: a read error throws (std::ios_base::failure)
			JsonParsingSettings strict;
			strict.strict = true;
			try
			{
				const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				all = JsonNode(text.data(), text.size(), strict, runAnywayFile().string());
				if(!all.isStruct() && !all.isNull())
					throw std::runtime_error("it does not hold a JSON object");
			}
			catch(const std::exception & e)
			{
				// a damaged file is kept aside as <name>.bad, for the player or a helper to look at, and started again
				in.close();
				const boost::filesystem::path aside = runAnywayFile().string() + ".bad";
				boost::system::error_code renameError;
				boost::filesystem::rename(runAnywayFile(), aside, renameError);
				logMod->warn("%s could not be read (%s); it is kept as %s and started again%s", runAnywayFile().string(), e.what(), aside.string(),
					renameError ? " (it could not be renamed: " + renameError.message() + ")" : std::string());
				all = JsonNode();
			}
		}
	}
	JsonVector & list = all[mod].Vector();
	for(const auto & entry : list)
		if(entry.isString() && boost::to_lower_copy(entry.String()) == hash)
			return {};
	list.push_back(JsonNode(hash));
	const boost::filesystem::path temporary = runAnywayFile().string() + ".tmp";
	// why the file did not keep the answer, for the log and the player
	const auto failed = [&](const std::string & why)
	{
		logMod->error("Mod %s: the player chose to run its code anyway (%s); it holds until the game quits, since %s", modID, hash, why);
		boost::system::error_code ignored;
		boost::filesystem::remove(temporary, ignored);
		return why;
	};
	{
		boost::system::error_code folderError;
		boost::filesystem::create_directories(runAnywayFile().parent_path(), folderError);
		std::ofstream out(temporary.c_str(), std::ios::binary | std::ios::trunc);
		out << all.toString();
		out.close();
		if(!out)
			return failed(temporary.string() + " could not be written");
	}
	boost::system::error_code renameError;
	boost::filesystem::rename(temporary, runAnywayFile(), renameError);
	if(renameError)
		return failed(runAnywayFile().string() + " could not be replaced (" + renameError.message() + ")");
	logMod->info("Mod %s: the player chose to run its code anyway (%s), remembered in %s", modID, hash, runAnywayFile().string());
	return {};
}

std::string AddonCode::trustProblem(const std::string & modID, const boost::filesystem::path & folder, bool * answerable)
{
	if(answerable)
		*answerable = false;
	if(settings["mods"]["allowUnlistedCode"].Bool())
		return {};

	// the code is read first: what cannot be read cannot be run, anyway or otherwise
	const std::string hash = cachedFolderHash(folder);
	if(hash.empty())
		return "its code could not be read";
	if(answerable)
		*answerable = true;

	// a pins file as JSON; nothing when it is not there, and no object when it cannot be read (a read error throws std::ios_base::failure)
	const auto read = [](const boost::filesystem::path & file) -> std::optional<JsonNode>
	{
		std::ifstream in(file.c_str(), std::ios::binary);
		if(!in)
			return std::nullopt;
		try
		{
			const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			return JsonNode(text.data(), text.size(), file.string());
		}
		catch(const std::exception & e)
		{
			logMod->warn("%s could not be read: %s", file.string(), e.what());
			return JsonNode();
		}
	};
	// the hashes a pins file accepts for the mod: one, or a list of them
	const auto hashesFor = [&modID](const std::optional<JsonNode> & pins)
	{
		std::vector<std::string> accepted;
		if(!pins || !pins->isStruct())
			return accepted;
		const JsonNode & pin = (*pins)[boost::to_lower_copy(modID)];
		if(pin.isString())
			accepted.push_back(boost::to_lower_copy(pin.String()));
		else if(pin.isVector())
			for(const auto & entry : pin.Vector())
				if(entry.isString())
					accepted.push_back(boost::to_lower_copy(entry.String()));
		return accepted;
	};

	const auto catalog = read(pinsFile());
	const std::vector<std::string> accepted = hashesFor(catalog);
	// builds a tester runs before the catalog lists them, each approved on this PC by its exact hash
	const std::vector<std::string> tested = hashesFor(read(testedPinsFile()));
	// versions the player answered "Run it anyway" for (K, October 18th), by exact hash
	std::vector<std::string> remembered = hashesFor(read(runAnywayFile()));
	// and those answered in this run, which hold even when the file could not keep them
	{
		std::lock_guard<std::mutex> guard(runAnywayMutex);
		for(const auto & [mod, answered] : runAnywayThisRun)
			if(mod == boost::to_lower_copy(modID))
				remembered.push_back(answered);
	}
	// why the catalog does not vouch for the mod at all
	const auto unlisted = [&catalog]() -> std::string
	{
		if(!catalog)
			return "DMB's mod catalog has not been downloaded yet (the launcher downloads it)";
		if(!catalog->isStruct())
			return "DMB's mod catalog could not be read (the launcher downloads it again)";
		return "it is not in DMB's mod catalog";
	};
	if(vstd::contains(accepted, hash))
		return {};
	if(vstd::contains(tested, hash))
	{
		logMod->info("Mod %s: its code is a build approved for testing on this PC (%s)", modID, testedPinsFile().string());
		return {};
	}
	if(vstd::contains(remembered, hash))
	{
		logMod->info("Mod %s: its code is a version the player chose to run anyway (%s)", modID, runAnywayFile().string());
		return {};
	}
	if(accepted.empty() && tested.empty())
		return unlisted(); // nothing pins the mod, and the player's answers name other code
	// the numbers are for the log, where a mod's author looks; the player gets a notice with a choice
	logMod->warn("Mod %s: its code in %s hashes to %s, and DMB's mod catalog pins %s", modID, folder.string(), hash,
		boost::algorithm::join(accepted, ", "));
	return "its files are not the ones DMB's mod catalog lists";
}

VCMI_LIB_NAMESPACE_END
