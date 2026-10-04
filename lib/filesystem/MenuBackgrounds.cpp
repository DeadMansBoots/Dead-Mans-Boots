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

#include <deque>
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

/// a text field of a mod's mod.json ("name", "description"), empty when there is none
std::string modTextOf(const boost::filesystem::path & folder, const char * key)
{
	try
	{
		std::ifstream file((folder / "mod.json").c_str(), std::ios::binary);
		std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		if(text.empty())
			return {};
		const JsonNode node(reinterpret_cast<const std::byte *>(text.data()), text.size(), "mod.json");
		return node[key].isString() ? node[key].String() : std::string();
	}
	catch(const std::exception &)
	{
		return {};
	}
}

std::string modNameOf(const boost::filesystem::path & folder)
{
	return modTextOf(folder, "name");
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

/// The Main Menu Themes Pack carries a theme for every release and mod of the game, but a player's list is the menus of what they have
/// (K, October 4th: "why do we have wake of gods? that's not in my mods list"): the four releases whose files the game itself holds
/// (Restoration of Erathia, Armageddon's Blade, Shadow of Death, Complete) always, any other theme only where the mod it is the menu of is
/// installed (Horn of the Abyss, Tides of War); a theme this does not know is left out.
bool packThemeWanted(const std::string & id)
{
	static const std::string pack = "h3-themes.";
	if(!boost::starts_with(id, pack))
		return true;
	std::string theme = id.substr(pack.size());
	const auto dot = theme.find('.');
	const std::string main = theme.substr(0, dot);
	if(main == "roe" || main == "ab" || main == "sod" || main == "complete")
		return true;
	const std::pair<const char *, const char *> needs[] = {{"hota-", "hota"}, {"tow", "tides-of-war"}, {"wog", "wake-of-gods"}, {"hc-", "heroes-chronicles"}};
	for(const auto & [prefix, mod] : needs)
		if(boost::starts_with(main, prefix))
			return !MenuBackgrounds::modFolder(mod).empty();
	return false;
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
	// a pack theme of a mod that adds a menu of its own is left out too: the mod's own entry is that menu
	const std::vector<MenuMod> all = found;
	found.erase(std::remove_if(found.begin(), found.end(), [&all](const MenuMod & entry)
	{
		if(!packThemeWanted(entry.id))
			return true;
		if(!boost::starts_with(entry.id, "h3-themes."))
			return false;
		const std::string theme = entry.id.substr(10);
		const std::pair<const char *, const char *> owners[] = {{"hota-", "hota."}, {"tow", "tides-of-war."}, {"wog", "wake-of-gods."}, {"hc-", "heroes-chronicles."}};
		for(const auto & [prefix, mod] : owners)
			if(boost::starts_with(theme, prefix))
				return std::any_of(all.begin(), all.end(), [mod = std::string(mod)](const MenuMod & other) { return boost::starts_with(other.id, mod); });
		return false;
	}), found.end());
	// two entries with one title (two sub-mods of one mod, a themes pack's) are told apart by what each mod says it is ("Heroes 3 Theme:
	// Armageddon's Blade" is "Armageddon's Blade menu"), and where that is the same too, by the mod's own id
	const std::vector<MenuMod> before = found;
	for(auto & entry : found)
		if(std::count_if(before.begin(), before.end(), [&entry](const MenuMod & other) { return other.title == entry.title; }) > 1)
		{
			std::string described = modTextOf(entry.folder, "description");
			const auto at = described.find("Theme: ");
			if(at != std::string::npos)
				described = described.substr(at + 7);
			boost::trim(described);
			if(!described.empty())
				entry.title = boost::iends_with(described, "menu") ? described : described + " menu";
		}
	for(auto & entry : found)
		if(std::count_if(found.begin(), found.end(), [&entry](const MenuMod & other) { return other.title == entry.title; }) > 1)
			entry.title += " [" + entry.id + "]";
	return found;
}

std::vector<boost::filesystem::path> modBaseFolders()
{
	std::vector<boost::filesystem::path> folders;
	for(const auto & base : VCMIDirs::get().dataPaths())
		folders.push_back(base / "Mods");
	folders.push_back(VCMIDirs::get().userDataPath() / "Mods");
	return folders;
}

std::vector<MenuFile> discoverPictures(const boost::filesystem::path & folder)
{
	std::vector<MenuFile> found;
	boost::system::error_code ec;
	if(folder.empty())
		return found;
	for(boost::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
	{
		std::string extension = boost::to_lower_copy(it->path().extension().string());
		if((extension != ".png" && extension != ".bmp") || !boost::filesystem::is_regular_file(it->path(), ec))
			continue;
		MenuFile file;
		file.path = it->path();
		file.id = "file:" + boost::to_lower_copy(it->path().filename().string());
		file.slug = boost::to_lower_copy(it->path().stem().string());
		for(char & c : file.slug)
			if(!std::isalnum(static_cast<unsigned char>(c)))
				c = '-';
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

std::vector<MenuFile> discoverDropIns()
{
	return discoverPictures(MenuBackgrounds::dropInFolder());
}

std::vector<MenuFile> discoverLoadingScreens()
{
	return discoverPictures(MenuBackgrounds::loadingFolder());
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

boost::filesystem::path MenuBackgrounds::loadingFolder()
{
	static const boost::filesystem::path folder = []()
	{
		boost::system::error_code ec;
		const auto path = VCMIDirs::get().userDataPath() / "LoadingScreens";
		boost::filesystem::create_directories(path, ec);
		return boost::filesystem::is_directory(path, ec) ? path : boost::filesystem::path();
	}();
	return folder;
}

const std::vector<MenuFile> & MenuBackgrounds::loadingScreens()
{
	static const std::vector<MenuFile> found = discoverLoadingScreens();
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

namespace
{
ui32 le32(const std::vector<ui8> & b, size_t at)
{
	return static_cast<ui32>(b[at]) | (static_cast<ui32>(b[at + 1]) << 8) | (static_cast<ui32>(b[at + 2]) << 16) | (static_cast<ui32>(b[at + 3]) << 24);
}

void put32(std::vector<ui8> & b, ui32 v)
{
	for(int i = 0; i < 4; ++i)
		b.push_back(static_cast<ui8>((v >> (8 * i)) & 0xFF));
}

/// The glow baked into the game's menu buttons is a dull pink that matches the picture each release made it for (saturation up to 0.68,
/// pink or orange: red over green, blue not above red); the icons' own reds are saturated and their silvers have no tint.
bool isGlow(const ui8 * palette, ui8 index)
{
	if(index <= 7)
		return false;
	const int r = palette[index * 3];
	const int g = palette[index * 3 + 1];
	const int b = palette[index * 3 + 2];
	const int top = std::max({r, g, b});
	const int low = std::min({r, g, b});
	return top >= 70 && r >= g + 8 && r >= b - 4 && (top - low) * 100 <= 68 * top;
}

/// the glow tints the icon's drop shadow too: a dark red-brown, with the glow's limits on saturation
bool tintedDark(const ui8 * palette, ui8 index)
{
	if(index <= 7)
		return false;
	const int r = palette[index * 3];
	const int g = palette[index * 3 + 1];
	const int b = palette[index * 3 + 2];
	const int top = std::max({r, g, b});
	const int low = std::min({r, g, b});
	return top >= 30 && top < 110 && r >= g + 8 && r >= b - 4 && (top - low) * 100 <= 68 * top;
}

/// the glow is a smooth gradient; where one pixel steps to the next by more than this in a colour channel, the icon's edge begins
int colourStep(const ui8 * palette, ui8 a, ui8 b)
{
	int step = 0;
	for(int c = 0; c < 3; ++c)
		step = std::max(step, std::abs(static_cast<int>(palette[a * 3 + c]) - static_cast<int>(palette[b * 3 + c])));
	return step;
}

/// one frame's pixels (palette indexes, 0 outside) as a w by h block; false for a format this does not read
bool decodeFrame(const std::vector<ui8> & d, size_t at, int w, int h, ui32 format, std::vector<ui8> & pix)
{
	pix.assign(static_cast<size_t>(w) * h, 0);
	const size_t body = at + 32;
	const auto need = [&d](size_t n) { return n <= d.size(); };
	for(int row = 0; row < h; ++row)
	{
		ui8 * line = pix.data() + static_cast<size_t>(row) * w;
		if(format == 0)
		{
			if(!need(body + static_cast<size_t>(row + 1) * w))
				return false;
			std::copy(d.begin() + body + static_cast<size_t>(row) * w, d.begin() + body + static_cast<size_t>(row + 1) * w, line);
		}
		else if(format == 1 || format == 2)
		{
			size_t p;
			if(format == 1)
			{
				if(!need(body + static_cast<size_t>(row + 1) * 4))
					return false;
				p = body + le32(d, body + static_cast<size_t>(row) * 4);
			}
			else
			{
				if(!need(body + static_cast<size_t>(row + 1) * 2))
					return false;
				p = body + (d[body + row * 2] | (d[body + row * 2 + 1] << 8));
			}
			int col = 0;
			while(col < w)
			{
				if(format == 1)
				{
					if(!need(p + 2))
						return false;
					const ui8 code = d[p];
					const int length = d[p + 1] + 1;
					p += 2;
					if(col + length > w)
						return false;
					if(code == 0xFF)
					{
						if(!need(p + length))
							return false;
						std::copy(d.begin() + p, d.begin() + p + length, line + col);
						p += length;
					}
					else
						std::fill(line + col, line + col + length, code);
					col += length;
				}
				else
				{
					if(!need(p + 1))
						return false;
					const int code = d[p] >> 5;
					const int length = (d[p] & 31) + 1;
					p += 1;
					if(col + length > w)
						return false;
					if(code == 7)
					{
						if(!need(p + length))
							return false;
						std::copy(d.begin() + p, d.begin() + p + length, line + col);
						p += length;
					}
					else
						std::fill(line + col, line + col + length, static_cast<ui8>(code));
					col += length;
				}
			}
		}
		else
			return false;
	}
	return true;
}

/// what is reached from outside through glow-coloured pixels, each close in colour to the one before it (the icon's own edge is a step)
std::vector<ui8> findGlow(const ui8 * palette, const std::vector<ui8> & pix, int w, int h)
{
	const auto at = [w](int x, int y) { return static_cast<size_t>(y) * w + x; };
	std::vector<ui8> ring(pix.size(), 0);
	std::deque<std::pair<int, int>> queue;
	const auto visit = [&](int x, int y, int from)
	{
		if(x < 0 || y < 0 || x >= w || y >= h || ring[at(x, y)] || pix[at(x, y)] == 0 || !isGlow(palette, pix[at(x, y)]))
			return;
		if(from >= 0 && colourStep(palette, static_cast<ui8>(from), pix[at(x, y)]) > 20)
			return;
		ring[at(x, y)] = 1;
		queue.emplace_back(x, y);
	};
	for(int y = 0; y < h; ++y)
		for(int x = 0; x < w; ++x)
		{
			if(pix[at(x, y)] == 0)
			{
				visit(x - 1, y, -1);
				visit(x + 1, y, -1);
				visit(x, y - 1, -1);
				visit(x, y + 1, -1);
			}
			else if(x == 0 || y == 0 || x == w - 1 || y == h - 1)
				visit(x, y, -1);
		}
	while(!queue.empty())
	{
		const auto [x, y] = queue.front();
		queue.pop_front();
		const int here = pix[at(x, y)];
		visit(x - 1, y, here);
		visit(x + 1, y, here);
		visit(x, y - 1, here);
		visit(x, y + 1, here);
	}
	return ring;
}

int glowBrightness(const ui8 * palette, const std::vector<ui8> & pix, const std::vector<ui8> & ring)
{
	long total = 0;
	long count = 0;
	for(size_t i = 0; i < pix.size(); ++i)
		if(ring[i])
		{
			total += (palette[pix[i] * 3] + palette[pix[i] * 3 + 1] + palette[pix[i] * 3 + 2]) / 3;
			++count;
		}
	return count ? static_cast<int>(total / count) : 0;
}

/// The glow also fills pockets the flood from outside cannot reach (between and inside the letters, walled in by their dark edge). Those
/// have the colours the glow outside uses most, shaded dark by the letters; so a patch of at least 12 touching pixels in such a colour,
/// in a frame, whose colour is dark on average (the top of a silver banner or the gem of a sword is pale), is glow too.
void addEnclosedGlow(const ui8 * palette, const std::vector<ui8> & pix, std::vector<ui8> & ring, int w, int h,
	const std::vector<long> & inGlow, const std::vector<long> & used)
{
	const auto at = [w](int x, int y) { return static_cast<size_t>(y) * w + x; };
	std::vector<ui8> candidate(pix.size(), 0);
	for(size_t i = 0; i < pix.size(); ++i)
	{
		const ui8 v = pix[i];
		candidate[i] = v > 7 && !ring[i] && isGlow(palette, v) && inGlow[v] > 0 && inGlow[v] * 2 >= used[v];
	}
	std::vector<ui8> seen(pix.size(), 0);
	for(int y0 = 0; y0 < h; ++y0)
		for(int x0 = 0; x0 < w; ++x0)
		{
			if(!candidate[at(x0, y0)] || seen[at(x0, y0)])
				continue;
			std::vector<size_t> patch;
			std::deque<std::pair<int, int>> open;
			open.emplace_back(x0, y0);
			seen[at(x0, y0)] = 1;
			long tops = 0;
			while(!open.empty())
			{
				const auto [x, y] = open.front();
				open.pop_front();
				patch.push_back(at(x, y));
				const ui8 v = pix[at(x, y)];
				tops += std::max({palette[v * 3], palette[v * 3 + 1], palette[v * 3 + 2]});
				for(int dx = -1; dx <= 1; ++dx)
					for(int dy = -1; dy <= 1; ++dy)
					{
						const int nx = x + dx;
						const int ny = y + dy;
						if(nx >= 0 && ny >= 0 && nx < w && ny < h && candidate[at(nx, ny)] && !seen[at(nx, ny)])
						{
							seen[at(nx, ny)] = 1;
							open.emplace_back(nx, ny);
						}
					}
			}
			if(patch.size() >= 12 && tops <= 155 * static_cast<long>(patch.size()))
				for(const size_t i : patch)
					ring[i] = 1;
		}
}

/// the glow taken away: the ring becomes transparent, except against the icon: one pixel of the darkest colour the icon itself uses, or,
/// in a highlight frame (its glow is paler than the button's normal one), two pixels of the brightest. The game's shadow indexes cannot
/// be used: the scaled-up copy of an image ignores them. What the glow was holding up is left floating: a few pixels apart from the icon
/// (a dark fragment at the edge of a frame, the smudge of a candle flame, single specks) are taken away first, so nothing is drawn round
/// them; a bright small piece joined to the icon (a gold finial) stays.
void removeGlow(const ui8 * palette, std::vector<ui8> & pix, const std::vector<ui8> & ring, int w, int h, bool highlight)
{
	const auto at = [w](int x, int y) { return static_cast<size_t>(y) * w + x; };
	std::vector<ui8> notIcon(ring);
	std::vector<ui8> seen(pix.size(), 0);
	for(int y0 = 0; y0 < h; ++y0)
		for(int x0 = 0; x0 < w; ++x0)
		{
			if(pix[at(x0, y0)] <= 7 || ring[at(x0, y0)] || seen[at(x0, y0)])
				continue;
			std::vector<size_t> piece;
			std::deque<std::pair<int, int>> open;
			open.emplace_back(x0, y0);
			seen[at(x0, y0)] = 1;
			long brightness = 0;
			while(!open.empty())
			{
				const auto [x, y] = open.front();
				open.pop_front();
				piece.push_back(at(x, y));
				const ui8 v = pix[at(x, y)];
				brightness += (palette[v * 3] + palette[v * 3 + 1] + palette[v * 3 + 2]) / 3;
				for(int dx = -1; dx <= 1; ++dx)
					for(int dy = -1; dy <= 1; ++dy)
					{
						const int nx = x + dx;
						const int ny = y + dy;
						if(nx >= 0 && ny >= 0 && nx < w && ny < h && pix[at(nx, ny)] > 7 && !ring[at(nx, ny)] && !seen[at(nx, ny)])
						{
							seen[at(nx, ny)] = 1;
							open.emplace_back(nx, ny);
						}
					}
			}
			const long mean = brightness / static_cast<long>(piece.size());
			if(piece.size() <= 3 || (piece.size() <= 80 && mean < 120))
				for(const size_t i : piece)
					notIcon[i] = 1;
		}

	// how far each pixel that is not the icon is from the icon
	std::deque<std::pair<int, int>> queue;
	std::vector<int> dist(pix.size(), -1);
	for(int y = 0; y < h; ++y)
		for(int x = 0; x < w; ++x)
			if(pix[at(x, y)] != 0 && !notIcon[at(x, y)])
			{
				dist[at(x, y)] = 0;
				queue.emplace_back(x, y);
			}
	while(!queue.empty())
	{
		const auto [x, y] = queue.front();
		queue.pop_front();
		const int next = dist[at(x, y)] + 1;
		const std::pair<int, int> around[4] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
		for(const auto & [nx, ny] : around)
			if(nx >= 0 && ny >= 0 && nx < w && ny < h && dist[at(nx, ny)] < 0 && (notIcon[at(nx, ny)] || pix[at(nx, ny)] == 0))
			{
				dist[at(nx, ny)] = next;
				queue.emplace_back(nx, ny);
			}
	}
	ui8 outline = 0;
	int best = highlight ? -1 : 100000;
	for(size_t i = 0; i < pix.size(); ++i)
	{
		const ui8 v = pix[i];
		if(v <= 7 || notIcon[i] || isGlow(palette, v))
			continue;
		const int sum = palette[v * 3] + palette[v * 3 + 1] + palette[v * 3 + 2];
		if(highlight ? sum > best : sum < best)
		{
			best = sum;
			outline = v;
		}
	}
	const int reach = highlight ? 2 : 1;
	for(int y = 0; y < h; ++y)
		for(int x = 0; x < w; ++x)
			if(notIcon[at(x, y)])
				pix[at(x, y)] = (dist[at(x, y)] >= 1 && dist[at(x, y)] <= reach && outline != 0) ? outline : 0;

	// the icon's drop shadow, tinted by the glow, within two pixels of what is now transparent takes the outline colour
	if(outline != 0)
	{
		const std::vector<ui8> cleared(pix);
		for(int y = 0; y < h; ++y)
			for(int x = 0; x < w; ++x)
			{
				if(!tintedDark(palette, cleared[at(x, y)]))
					continue;
				bool beside = false;
				for(int dy = -2; dy <= 2 && !beside; ++dy)
					for(int dx = -2; dx <= 2 && !beside; ++dx)
					{
						const int nx = x + dx;
						const int ny = y + dy;
						beside = nx >= 0 && ny >= 0 && nx < w && ny < h && cleared[at(nx, ny)] == 0;
					}
				if(beside)
					pix[at(x, y)] = outline;
			}
	}
}
}

boost::filesystem::path MenuBackgrounds::modFolder(const std::string & id)
{
	std::vector<std::string> parts;
	boost::split(parts, id, boost::is_any_of("."));
	for(const auto & base : modBaseFolders())
	{
		boost::filesystem::path current = base;
		for(size_t i = 0; i < parts.size() && !current.empty(); ++i)
		{
			current = child(current, boost::to_lower_copy(parts[i]));
			if(!current.empty() && i + 1 < parts.size())
				current = child(current, "mods");
		}
		boost::system::error_code ec;
		if(!current.empty() && boost::filesystem::exists(current / "mod.json", ec))
			return current;
	}
	return {};
}

std::vector<std::string> MenuBackgrounds::modDepends(const boost::filesystem::path & folder)
{
	std::vector<std::string> ids;
	if(folder.empty())
		return ids;
	try
	{
		std::ifstream file((folder / "mod.json").c_str(), std::ios::binary);
		std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		if(text.empty())
			return ids;
		const JsonNode node(reinterpret_cast<const std::byte *>(text.data()), text.size(), "mod.json");
		for(const auto & entry : node["depends"].Vector())
			if(entry.isString())
				ids.push_back(boost::to_lower_copy(entry.String()));
	}
	catch(const std::exception &)
	{
	}
	return ids;
}

std::vector<ui8> MenuBackgrounds::neutralSprite(const MenuEra & era, const std::string & name)
{
	const std::vector<ui8> d = sprite(era, name);
	if(d.size() < 16 + 768)
		return {};
	const ui32 blocks = le32(d, 12);
	std::vector<ui8> out(d.begin(), d.begin() + 16 + 768);
	struct Block
	{
		std::vector<ui8> header; // 16 bytes
		std::vector<ui8> names;
		std::vector<ui32> offsets;
	};
	std::vector<Block> parsed;
	size_t pos = 16 + 768;
	for(ui32 b = 0; b < blocks; ++b)
	{
		if(pos + 16 > d.size())
			return {};
		Block block;
		block.header.assign(d.begin() + pos, d.begin() + pos + 16);
		const ui32 n = le32(d, pos + 4);
		pos += 16;
		if(n > 1000 || pos + 13 * n + 4 * n > d.size())
			return {};
		block.names.assign(d.begin() + pos, d.begin() + pos + 13 * n);
		pos += 13 * n;
		for(ui32 i = 0; i < n; ++i)
			block.offsets.push_back(le32(d, pos + 4 * i));
		pos += 4 * n;
		parsed.push_back(std::move(block));
	}
	// the new file: the same header and tables, every frame stored plain (format 0) with the glow turned to shadow
	std::vector<size_t> offsetAt;
	for(const auto & block : parsed)
	{
		out.insert(out.end(), block.header.begin(), block.header.end());
		out.insert(out.end(), block.names.begin(), block.names.end());
		offsetAt.push_back(out.size());
		for(size_t i = 0; i < block.offsets.size(); ++i)
			put32(out, 0);
	}
	const ui8 * palette = d.data() + 16;
	for(size_t b = 0; b < parsed.size(); ++b)
	{
		struct Frame
		{
			ui32 format, fullW, fullH, left, top;
			int w, h;
			std::vector<ui8> pix, ring;
			int brightness;
		};
		std::vector<Frame> frames;
		for(size_t i = 0; i < parsed[b].offsets.size(); ++i)
		{
			const size_t at = parsed[b].offsets[i];
			if(at + 32 > d.size())
				return {};
			Frame f;
			f.format = le32(d, at + 4);
			f.fullW = le32(d, at + 8);
			f.fullH = le32(d, at + 12);
			f.w = static_cast<int>(le32(d, at + 16));
			f.h = static_cast<int>(le32(d, at + 20));
			f.left = le32(d, at + 24);
			f.top = le32(d, at + 28);
			if(f.w <= 0 || f.h <= 0 || f.w > 4096 || f.h > 4096 || (f.format == 1 && f.w > static_cast<int>(f.fullW) && f.h > static_cast<int>(f.fullH)))
				return {};
			if(!decodeFrame(d, at, f.w, f.h, f.format, f.pix))
				return {};
			f.ring = findGlow(palette, f.pix, f.w, f.h);
			f.brightness = glowBrightness(palette, f.pix, f.ring);
			frames.push_back(std::move(f));
		}
		std::vector<long> inGlow(256, 0);
		std::vector<long> used(256, 0);
		for(const auto & f : frames)
			for(size_t k = 0; k < f.pix.size(); ++k)
				if(f.pix[k] > 7)
				{
					++used[f.pix[k]];
					inGlow[f.pix[k]] += f.ring[k] ? 1 : 0;
				}
		for(auto & f : frames)
			addEnclosedGlow(palette, f.pix, f.ring, f.w, f.h, inGlow, used);
		int palest = 1000;
		for(const auto & f : frames)
			palest = std::min(palest, f.brightness);
		for(size_t i = 0; i < frames.size(); ++i)
		{
			Frame & f = frames[i];
			// a frame whose glow is much brighter than the button's other frames is its highlight
			removeGlow(palette, f.pix, f.ring, f.w, f.h, f.brightness > palest + 25);
			const ui32 here = static_cast<ui32>(out.size());
			for(int k = 0; k < 4; ++k)
				out[offsetAt[b] + i * 4 + k] = static_cast<ui8>((here >> (8 * k)) & 0xFF);
			put32(out, static_cast<ui32>(f.pix.size()));
			put32(out, 0);
			put32(out, f.fullW);
			put32(out, f.fullH);
			put32(out, static_cast<ui32>(f.w));
			put32(out, static_cast<ui32>(f.h));
			put32(out, f.left);
			put32(out, f.top);
			out.insert(out.end(), f.pix.begin(), f.pix.end());
		}
	}
	return out;
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
