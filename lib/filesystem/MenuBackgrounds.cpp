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

/// The glow baked into the game's menu buttons is a dull pink that matches the picture each release made it for (saturation about
/// 0.5 to 0.63, not dark); the icons' own reds are saturated or dark.
bool isGlow(const ui8 * palette, ui8 index)
{
	if(index <= 7)
		return false;
	const int r = palette[index * 3];
	const int g = palette[index * 3 + 1];
	const int b = palette[index * 3 + 2];
	const int top = std::max({r, g, b});
	const int low = std::min({r, g, b});
	return top >= 70 && r >= g + 12 && g * 100 <= 80 * r && (top - low) * 100 <= 68 * top;
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

/// the glow taken away: what is reached from outside through glow-coloured pixels becomes transparent, except one pixel against the icon,
/// which takes the darkest colour the icon itself uses (the game's shadow indexes cannot be used: the scaled-up copy of an image ignores them)
void removeGlow(const ui8 * palette, std::vector<ui8> & pix, int w, int h)
{
	const auto at = [w](int x, int y) { return static_cast<size_t>(y) * w + x; };
	std::vector<ui8> ring(pix.size(), 0);
	std::deque<std::pair<int, int>> queue;
	const auto visit = [&](int x, int y)
	{
		if(x < 0 || y < 0 || x >= w || y >= h || ring[at(x, y)] || pix[at(x, y)] == 0 || !isGlow(palette, pix[at(x, y)]))
			return;
		ring[at(x, y)] = 1;
		queue.emplace_back(x, y);
	};
	// the outside is the picture's edge and every transparent pixel
	for(int y = 0; y < h; ++y)
		for(int x = 0; x < w; ++x)
		{
			if(pix[at(x, y)] == 0)
			{
				visit(x - 1, y);
				visit(x + 1, y);
				visit(x, y - 1);
				visit(x, y + 1);
			}
			else if(x == 0 || y == 0 || x == w - 1 || y == h - 1)
				visit(x, y);
		}
	while(!queue.empty())
	{
		const auto [x, y] = queue.front();
		queue.pop_front();
		visit(x - 1, y);
		visit(x + 1, y);
		visit(x, y - 1);
		visit(x, y + 1);
	}
	// how far each glow pixel is from the icon
	std::vector<int> dist(pix.size(), -1);
	for(int y = 0; y < h; ++y)
		for(int x = 0; x < w; ++x)
			if(pix[at(x, y)] != 0 && !ring[at(x, y)])
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
			if(nx >= 0 && ny >= 0 && nx < w && ny < h && dist[at(nx, ny)] < 0 && (ring[at(nx, ny)] || pix[at(nx, ny)] == 0))
			{
				dist[at(nx, ny)] = next;
				queue.emplace_back(nx, ny);
			}
	}
	ui8 outline = 0;
	int darkest = 100000;
	for(size_t i = 0; i < pix.size(); ++i)
	{
		const ui8 v = pix[i];
		if(v <= 7 || ring[i] || isGlow(palette, v))
			continue;
		const int sum = palette[v * 3] + palette[v * 3 + 1] + palette[v * 3 + 2];
		if(sum < darkest)
		{
			darkest = sum;
			outline = v;
		}
	}
	for(int y = 0; y < h; ++y)
		for(int x = 0; x < w; ++x)
			if(ring[at(x, y)])
				pix[at(x, y)] = (dist[at(x, y)] == 1 && outline != 0) ? outline : 0;

	// what the glow was holding up is left floating: a few pixels apart from the icon (a dark fragment at the edge of one frame, the smudge
	// of a candle flame, single specks) are taken away; a bright small piece (a gold finial) stays
	std::vector<ui8> seen(pix.size(), 0);
	for(int y0 = 0; y0 < h; ++y0)
		for(int x0 = 0; x0 < w; ++x0)
		{
			if(pix[at(x0, y0)] <= 7 || seen[at(x0, y0)])
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
						if(nx >= 0 && ny >= 0 && nx < w && ny < h && pix[at(nx, ny)] > 7 && !seen[at(nx, ny)])
						{
							seen[at(nx, ny)] = 1;
							open.emplace_back(nx, ny);
						}
					}
			}
			const long mean = brightness / static_cast<long>(piece.size());
			if(piece.size() <= 3 || (piece.size() <= 80 && mean < 120))
				for(const size_t i : piece)
					pix[i] = 0;
		}
}
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
		for(size_t i = 0; i < parsed[b].offsets.size(); ++i)
		{
			const size_t at = parsed[b].offsets[i];
			if(at + 32 > d.size())
				return {};
			const ui32 format = le32(d, at + 4);
			const ui32 fullW = le32(d, at + 8);
			const ui32 fullH = le32(d, at + 12);
			const int w = static_cast<int>(le32(d, at + 16));
			const int h = static_cast<int>(le32(d, at + 20));
			if(w <= 0 || h <= 0 || w > 4096 || h > 4096 || (format == 1 && w > static_cast<int>(fullW) && h > static_cast<int>(fullH)))
				return {};
			std::vector<ui8> pix;
			if(!decodeFrame(d, at, w, h, format, pix))
				return {};
			removeGlow(palette, pix, w, h);
			const ui32 here = static_cast<ui32>(out.size());
			for(int k = 0; k < 4; ++k)
				out[offsetAt[b] + i * 4 + k] = static_cast<ui8>((here >> (8 * k)) & 0xFF);
			put32(out, static_cast<ui32>(pix.size()));
			put32(out, 0);
			put32(out, fullW);
			put32(out, fullH);
			put32(out, static_cast<ui32>(w));
			put32(out, static_cast<ui32>(h));
			put32(out, le32(d, at + 24));
			put32(out, le32(d, at + 28));
			out.insert(out.end(), pix.begin(), pix.end());
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
