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

#include "CArchiveLoader.h"
#include "CInputStream.h"

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

std::vector<ui8> readAll(const CArchiveLoader & loader)
{
	std::vector<ui8> bytes;
	auto stream = loader.load(ResourcePath(PICTURE));
	if(!stream)
		return bytes;
	bytes.resize(static_cast<size_t>(stream->getSize()));
	if(!bytes.empty() && stream->read(bytes.data(), bytes.size()) != static_cast<si64>(bytes.size()))
		bytes.clear();
	return bytes;
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
			found.push_back(std::move(era));
		}
	}

	const auto rank = [](const MenuEra & era)
	{
		for(size_t i = 0; i < std::size(KNOWN); ++i)
			if(era.id == KNOWN[i])
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

const std::vector<MenuEra> & MenuBackgrounds::eras()
{
	static const std::vector<MenuEra> found = discover();
	return found;
}

std::vector<ui8> MenuBackgrounds::pcx(const MenuEra & era)
{
	try
	{
		return readAll(CArchiveLoader("DATA/", era.archive));
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
