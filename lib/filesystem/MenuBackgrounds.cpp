/*
 * MenuBackgrounds.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "MenuBackgrounds.h"

#include <fstream>

#include "CArchiveLoader.h"
#include "CInputStream.h"
#include "CZipLoader.h"
#include "ResourcePath.h"

#include "../json/JsonNode.h"

#include "../VCMIDirs.h"

VCMI_LIB_NAMESPACE_BEGIN

namespace
{
const std::string PICTURE = "DATA/GAMSELBK.PCX";

/// the order the releases came in; an archive not named here comes after them, by name
const char * const KNOWN[] = { "h3bitmap", "h3ab_bmp", "hota" };

std::string titleOf(const std::string & id)
{
	if(id == "h3bitmap")
		return "Heroes III Complete";
	if(id == "h3ab_bmp")
		return "Armageddon's Blade";
	if(id == "hota")
		return "Horn of the Abyss";
	return id;
}

std::vector<ui8> readAll(const CArchiveLoader & loader, const std::string & entry)
{
	std::vector<ui8> bytes;
	auto stream = loader.load(ResourcePath("DATA/" + entry));
	if(!stream)
		return bytes;
	bytes.resize(static_cast<size_t>(stream->getSize()));
	if(!bytes.empty() && stream->read(bytes.data(), bytes.size()) != static_cast<si64>(bytes.size()))
		bytes.clear();
	return bytes;
}

std::string lowerName(const boost::filesystem::path & path)
{
	return boost::to_lower_copy(path.filename().string());
}

/// a child of the folder by its name in any case ("mods" in one mod, "Mods" in another)
boost::filesystem::path child(const boost::filesystem::path & folder, const std::string & name)
{
	boost::system::error_code ec;
	for(boost::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
		if(lowerName(it->path()) == name)
			return it->path();
	return {};
}

std::string modNameOf(const boost::filesystem::path & folder)
{
	try
	{
		std::ifstream file((folder / "mod.json").c_str(), std::ios::binary);
		std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		if(text.empty())
			return {};
		const JsonNode node(reinterpret_cast<const std::byte *>(text.data()), text.size(), "mod.json");
		return node["name"].isString() ? node["name"].String() : std::string();
	}
	catch(const std::exception &)
	{
		return {};
	}
}

bool holdsMenu(const boost::filesystem::path & folder)
{
	boost::system::error_code ec;
	const auto content = child(folder, "content");
	if(!content.empty() && boost::filesystem::is_directory(content, ec))
	{
		const auto config = child(content, "config");
		if(!config.empty() && !child(config, "mainmenu.json").empty())
			return true;
	}
	const auto zip = child(folder, "content.zip");
	if(zip.empty())
		return false;
	try
	{
		return CZipLoader("", zip).existsResource(ResourcePath("config/mainmenu", EResType::JSON));
	}
	catch(const std::exception &)
	{
		return false;
	}
}

void scanMods(const boost::filesystem::path & folder, const std::string & parentId, const std::string & topName, std::vector<MenuMod> & found)
{
	boost::system::error_code ec;
	if(!boost::filesystem::is_directory(folder, ec))
		return;
	std::vector<boost::filesystem::path> mods;
	for(boost::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
		if(boost::filesystem::is_directory(it->path(), ec) && boost::filesystem::exists(it->path() / "mod.json", ec))
			mods.push_back(it->path());
	std::sort(mods.begin(), mods.end());
	for(const auto & mod : mods)
	{
		const std::string id = (parentId.empty() ? "" : parentId + ".") + lowerName(mod);
		std::string name = modNameOf(mod);
		if(name.empty())
			name = mod.filename().string();
		const std::string top = topName.empty() ? name : topName;
		if(holdsMenu(mod) && std::none_of(found.begin(), found.end(), [&id](const MenuMod & m) { return m.id == id; }))
		{
			MenuMod entry;
			entry.id = id;
			entry.folder = mod;
			if(topName.empty())
				entry.title = boost::iends_with(name, "menu") ? name : name + " menu";
			else
				entry.title = top + " menu" + (boost::icontains(name, "legacy") ? " (legacy)" : "");
			found.push_back(std::move(entry));
		}
		const auto sub = child(mod, "mods");
		if(!sub.empty())
			scanMods(sub, id, top, found);
	}
}

std::vector<MenuMod> discoverMods()
{
	std::vector<boost::filesystem::path> folders;
	for(const auto & base : VCMIDirs::get().dataPaths())
		folders.push_back(base / "Mods");
	folders.push_back(VCMIDirs::get().userDataPath() / "Mods");
	std::vector<MenuMod> found;
	for(const auto & folder : folders)
		scanMods(folder, "", "", found);
	// two entries with one title (two sub-mods of one mod) are told apart by the mod's own id
	for(auto & entry : found)
		if(std::count_if(found.begin(), found.end(), [&entry](const MenuMod & other) { return other.title == entry.title; }) > 1)
			entry.title += " [" + entry.id + "]";
	return found;
}

std::vector<MenuFile> discoverDropIns()
{
	std::vector<MenuFile> found;
	boost::system::error_code ec;
	const auto folder = MenuBackgrounds::dropInFolder();
	if(folder.empty())
		return found;
	for(boost::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
	{
		std::string extension = boost::to_lower_copy(it->path().extension().string());
		if((extension != ".png" && extension != ".jpg" && extension != ".jpeg" && extension != ".bmp") || !boost::filesystem::is_regular_file(it->path(), ec))
			continue;
		MenuFile file;
		file.path = it->path();
		file.id = "file:" + boost::to_lower_copy(it->path().filename().string());
		std::string title = it->path().stem().string();
		std::replace(title.begin(), title.end(), '_', ' ');
		boost::trim(title);
		if(!title.empty())
			title[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(title[0])));
		file.title = title.empty() ? file.id : title;
		found.push_back(std::move(file));
	}
	std::sort(found.begin(), found.end(), [](const MenuFile & a, const MenuFile & b) { return a.id < b.id; });
	return found;
}

std::vector<MenuEra> discover()
{
	std::vector<boost::filesystem::path> folders;
	for(const auto & base : VCMIDirs::get().dataPaths())
		folders.push_back(base / "Data");
	folders.push_back(VCMIDirs::get().userDataPath() / "Data");

	std::vector<MenuEra> found;
	std::set<std::string> seen;
	for(const auto & folder : folders)
	{
		boost::system::error_code ec;
		if(!boost::filesystem::is_directory(folder, ec))
			continue;
		for(boost::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
		{
			std::string extension = it->path().extension().string();
			boost::to_lower(extension);
			if(extension != ".lod")
				continue;
			MenuEra era;
			era.id = boost::to_lower_copy(it->path().stem().string());
			era.archive = it->path();
			if(!seen.insert(era.id).second)
				continue;
			try
			{
				CArchiveLoader loader("DATA/", era.archive);
				if(!loader.existsResource(ResourcePath(PICTURE)))
					continue;
			}
			catch(const std::exception &)
			{
				continue; // an archive that cannot be read is not an era
			}
			era.title = titleOf(era.id);
			// the archive's other two title pictures (GamSelB0, GamSelB1), where it holds them, are entries of their own
			std::vector<MenuEra> pictures = {era};
			try
			{
				CArchiveLoader loader("DATA/", era.archive);
				for(int n = 0; n < 2; ++n)
				{
					const std::string entry = "GAMSELB" + std::to_string(n) + ".PCX";
					if(!loader.existsResource(ResourcePath("DATA/" + entry)))
						continue;
					MenuEra other = era;
					other.id = era.id + "_b" + std::to_string(n); // no dash: the button names split at the first one
					other.entry = entry;
					other.title = era.title + ", picture " + std::to_string(n + 2);
					pictures.push_back(std::move(other));
				}
			}
			catch(const std::exception &)
			{
			}
			// the sprite archive that goes with it: the same name with "bitmap" or "_bmp" turned to "sprite" or "_spr"
			std::vector<std::string> companions;
			companions.push_back(boost::replace_first_copy(era.id, "bitmap", "sprite"));
			companions.push_back(boost::replace_first_copy(era.id, "_bmp", "_spr"));
			for(const auto & stem : companions)
			{
				if(stem == era.id)
					continue;
				for(const char * ext : {".lod", ".LOD"})
				{
					const auto path = it->path().parent_path() / (stem + ext);
					boost::system::error_code exists;
					if(!boost::filesystem::exists(path, exists))
						continue;
					try
					{
						CArchiveLoader loader("DATA/", path);
						if(loader.existsResource(ResourcePath("DATA/MMENUNG.DEF")))
						{
							era.sprites = path;
							break;
						}
					}
					catch(const std::exception &)
					{
					}
				}
				if(!era.sprites.empty())
					break;
			}
			for(auto & picture : pictures)
			{
				picture.sprites = era.sprites;
				found.push_back(std::move(picture));
			}
		}
	}

	// a picture ranks with its archive ("h3bitmap_b0" with "h3bitmap")
	const auto archiveOf = [](const MenuEra & era)
	{
		const bool other = era.id.size() > 3 && era.id[era.id.size() - 3] == '_' && era.id[era.id.size() - 2] == 'b' && std::isdigit(static_cast<unsigned char>(era.id.back()));
		return other ? era.id.substr(0, era.id.size() - 3) : era.id;
	};
	const auto rank = [&archiveOf](const MenuEra & era)
	{
		for(size_t i = 0; i < std::size(KNOWN); ++i)
			if(archiveOf(era) == KNOWN[i])
				return i;
		return std::size(KNOWN);
	};
	std::stable_sort(found.begin(), found.end(), [&rank](const MenuEra & a, const MenuEra & b)
	{
		return rank(a) != rank(b) ? rank(a) < rank(b) : a.id < b.id;
	});

	// a picture another archive holds byte for byte is listed once
	std::vector<MenuEra> distinct;
	std::vector<std::vector<ui8>> pictures;
	for(auto & era : found)
	{
		auto bytes = MenuBackgrounds::pcx(era);
		if(bytes.empty() || std::find(pictures.begin(), pictures.end(), bytes) != pictures.end())
			continue;
		pictures.push_back(std::move(bytes));
		distinct.push_back(std::move(era));
	}
	return distinct;
}
}

boost::filesystem::path MenuBackgrounds::dropInFolder()
{
	static const boost::filesystem::path folder = []()
	{
		boost::system::error_code ec;
		const auto path = VCMIDirs::get().userDataPath() / "MenuBackgrounds";
		boost::filesystem::create_directories(path, ec);
		return boost::filesystem::is_directory(path, ec) ? path : boost::filesystem::path();
	}();
	return folder;
}

const std::vector<MenuFile> & MenuBackgrounds::dropIns()
{
	static const std::vector<MenuFile> found = discoverDropIns();
	return found;
}

const std::vector<MenuMod> & MenuBackgrounds::menuMods()
{
	static const std::vector<MenuMod> found = discoverMods();
	return found;
}

const std::vector<MenuEra> & MenuBackgrounds::eras()
{
	static const std::vector<MenuEra> found = discover();
	return found;
}

std::vector<ui8> MenuBackgrounds::pcx(const MenuEra & era)
{
	try
	{
		return readAll(CArchiveLoader("DATA/", era.archive), era.entry);
	}
	catch(const std::exception &)
	{
		return {};
	}
}

std::vector<ui8> MenuBackgrounds::sprite(const MenuEra & era, const std::string & name)
{
	std::vector<ui8> bytes;
	if(era.sprites.empty())
		return bytes;
	try
	{
		CArchiveLoader loader("DATA/", era.sprites);
		const ResourcePath wanted("DATA/" + name + ".DEF");
		if(!loader.existsResource(wanted))
			return bytes;
		auto stream = loader.load(wanted);
		bytes.resize(static_cast<size_t>(stream->getSize()));
		if(!bytes.empty() && stream->read(bytes.data(), bytes.size()) != static_cast<si64>(bytes.size()))
			bytes.clear();
	}
	catch(const std::exception &)
	{
		bytes.clear();
	}
	return bytes;
}

bool MenuBackgrounds::decode(const std::vector<ui8> & pcx, int & width, int & height, std::vector<ui8> & rgb)
{
	if(pcx.size() < 12)
		return false;
	const auto word = [&pcx](size_t at)
	{
		return static_cast<ui32>(pcx[at]) | (static_cast<ui32>(pcx[at + 1]) << 8) | (static_cast<ui32>(pcx[at + 2]) << 16) | (static_cast<ui32>(pcx[at + 3]) << 24);
	};
	const ui32 size = word(0);
	const ui32 w = word(4);
	const ui32 h = word(8);
	if(w == 0 || h == 0 || w > 8192 || h > 8192)
		return false;
	const size_t pixels = static_cast<size_t>(w) * h;
	width = static_cast<int>(w);
	height = static_cast<int>(h);
	rgb.assign(pixels * 3, 0);
	if(size == pixels && pcx.size() >= 12 + pixels + 768)
	{
		const ui8 * palette = pcx.data() + 12 + pixels;
		for(size_t i = 0; i < pixels; ++i)
		{
			const ui8 index = pcx[12 + i];
			rgb[i * 3 + 0] = palette[index * 3 + 0];
			rgb[i * 3 + 1] = palette[index * 3 + 1];
			rgb[i * 3 + 2] = palette[index * 3 + 2];
		}
		return true;
	}
	if(size == pixels * 3 && pcx.size() >= 12 + pixels * 3)
	{
		for(size_t i = 0; i < pixels; ++i)
		{
			rgb[i * 3 + 0] = pcx[12 + i * 3 + 2]; // blue, green, red in the file
			rgb[i * 3 + 1] = pcx[12 + i * 3 + 1];
			rgb[i * 3 + 2] = pcx[12 + i * 3 + 0];
		}
		return true;
	}
	return false;
}

VCMI_LIB_NAMESPACE_END
