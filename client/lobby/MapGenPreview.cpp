/*
 * MapGenPreview.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "MapGenPreview.h"

#include "../GameEngine.h"
#include "../gui/WindowHandler.h"
#include "../render/IScreenHandler.h"

#include "../../lib/VCMIDirs.h"
#include "../../lib/modding/AddonCode.h"
#include "../../lib/modding/MapGenerators.h"
#include "../../lib/rmg/CMapGenOptions.h"
#include "../../lib/StartInfo.h"

#include <deque>
#include <fstream>

namespace
{
/// the seed every preview run uses: the picture changes with the settings and nothing else
constexpr int PREVIEW_SEED = 1;
/// a run still going after this has hung, and is ended
constexpr std::chrono::seconds PREVIEW_DEADLINE(60);
/// S246 (MapGen Dev, November 16th: an islands layout fits the sea and tries 16 zone layouts, 84 s for Islands and 88 s for Archipelago on 108x108 on
/// K's PC, against 9 s for Coastal; "I would allow 180 s for those two (and keep 60 s for the others)"): runs that ask for island water get longer
constexpr std::chrono::seconds PREVIEW_DEADLINE_ISLANDS(180);

std::chrono::seconds deadlineFor(const std::vector<std::string> & args)
{
	// --bio.waterShape 5 (Islands) or 6 (Archipelago), or --bio.waterContent 2 (Islands content, which the generator makes as the Islands layout)
	for(size_t i = 0; i + 1 < args.size(); ++i)
		if((args[i] == "--bio.waterShape" && (args[i + 1] == "5" || args[i + 1] == "6")) || (args[i] == "--bio.waterContent" && args[i + 1] == "2"))
			return PREVIEW_DEADLINE_ISLANDS;
	return PREVIEW_DEADLINE;
}
/// how long after the last change a run starts, so a slider dragged across its range runs once
constexpr std::chrono::milliseconds PREVIEW_DEBOUNCE(1000);

/// one run asked for: its key (the generator and its arguments), and whether it is the lobby card's (a newer card request drops an older queued one)
struct Job
{
	std::string key;
	MapGeneratorInfo generator;
	std::vector<std::string> args;
	bool card = false;
};

/// what the worker shares with the main thread (S246: a queue of runs, newest first, each set of arguments once)
struct Pending
{
	std::mutex lock;
	bool running = false;
	std::chrono::steady_clock::time_point askedAt;
	std::deque<Job> jobs;
	std::set<std::string> asked;
};

Pending & pending()
{
	static Pending state;
	return state;
}

/// at most this many runs wait; the oldest is dropped (asked again when it is looked at again)
constexpr size_t MAX_QUEUED = 12;
/// at most this many layouts are kept
constexpr size_t MAX_KEPT = 64;

/// main thread only: the layouts made, by key; the lobby card's newest key; a number that changes with every new layout
std::map<std::string, JsonNode> shownLayouts;
std::string cardKey;
int shownSerial = 0;

const JsonNode & layoutFor(const std::string & key)
{
	static const JsonNode none;
	const auto found = shownLayouts.find(key.empty() ? cardKey : key);
	return found == shownLayouts.end() ? none : found->second;
}

std::optional<MapGeneratorInfo> generatorFor(const CMapGenOptions & options)
{
	if(options.getExternalGenerator().empty())
		return std::nullopt;
	for(const auto & found : MapGenerators::active())
		if(found.modID == options.getExternalGenerator() && found.problem.empty() && found.arguments.count("layout"))
			return found;
	return std::nullopt;
}

boost::filesystem::path previewFolder()
{
	return VCMIDirs::get().userCachePath() / "mapgen-preview";
}

/// one run of the generator for the arguments asked for last; an empty node when it made no layout
JsonNode runOnce(const MapGeneratorInfo & generator, const std::vector<std::string> & args)
{
	const std::string refused = AddonCode::trustProblem(generator.modID, generator.codeFolder);
	if(!refused.empty())
	{
		logGlobal->info("Map preview: %s is not run for a preview, the catalog does not vouch for it: %s", generator.name, refused);
		return JsonNode();
	}
	const auto layoutPath = previewFolder() / "layout.json";
	boost::system::error_code ec;
	boost::filesystem::create_directories(previewFolder(), ec);
	boost::filesystem::remove(layoutPath, ec);
	std::vector<std::string> withLayout = args;
	withLayout.push_back("--layout");
	withLayout.push_back(layoutPath.string());
	const auto logPath = VCMIDirs::get().userLogsPath() / "extmapgen_preview_log.txt";
	const auto started = std::chrono::steady_clock::now();
	const int exitCode = MapGenerators::run(generator, withLayout, logPath, deadlineFor(args), nullptr, false);
	const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
	if(exitCode != 0 || !boost::filesystem::exists(layoutPath))
	{
		logGlobal->info("Map preview: %s made no layout (exit code %d, %d ms): %s", generator.name, exitCode, static_cast<int>(took), MapGenerators::errorLine(logPath));
		return JsonNode();
	}
	std::ifstream file(layoutPath.string(), std::ios::binary);
	const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	JsonNode layout(text.data(), text.size(), layoutPath.filename().string());
	if(!layout["layout"].isVector() || !layout["size"].isStruct())
	{
		logGlobal->warn("Map preview: %s wrote a layout DMB cannot read (%s)", generator.name, layoutPath.string());
		return JsonNode();
	}
	std::vector<std::string> colors;
	for(const auto & start : layout["starts"].Vector())
		colors.push_back(start["color"].String());
	logGlobal->info("Map preview: %s made the layout in %d ms: %dx%d, %d level(s), starts %s", generator.name, static_cast<int>(took),
		static_cast<int>(layout["size"]["w"].Integer()), static_cast<int>(layout["size"]["h"].Integer()), static_cast<int>(layout["layout"].Vector().size()),
		boost::algorithm::join(colors, ","));
	return layout;
}

void work()
{
	auto & state = pending();
	for(;;)
	{
		// one second after the newest request; a request meanwhile starts the second again
		for(;;)
		{
			std::chrono::steady_clock::time_point due;
			{
				std::lock_guard guard(state.lock);
				due = state.askedAt + PREVIEW_DEBOUNCE;
			}
			if(std::chrono::steady_clock::now() >= due)
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		Job job;
		{
			std::lock_guard guard(state.lock);
			if(state.jobs.empty())
			{
				state.running = false;
				return;
			}
			job = state.jobs.front();
			state.jobs.pop_front();
		}

		JsonNode layout;
		try
		{
			layout = runOnce(job.generator, job.args);
		}
		catch(const std::exception & e)
		{
			logGlobal->warn("Map preview: %s: %s", job.generator.name, e.what());
		}
		catch(...)
		{
			logGlobal->warn("Map preview: %s: an unknown error", job.generator.name);
		}

		// kept under its key, an empty one too (a run that failed is not run again for the same settings)
		ENGINE->dispatchMainThread([key = job.key, layout]()
		{
			if(shownLayouts.size() >= MAX_KEPT)
			{
				shownLayouts.clear();
				std::lock_guard guard(pending().lock);
				pending().asked.clear();
			}
			shownLayouts[key] = layout;
			shownSerial++;
			// the lobby is drawn again on the next frame, the pictures with it (a picture of fewer levels leaves nothing of the last behind)
			ENGINE->windows().totalRedraw();
		});
	}
}

}

bool MapGenPreview::applies(const CMapGenOptions & options)
{
	return generatorFor(options).has_value();
}

void MapGenPreview::request(const CMapGenOptions & options, const StartInfo & start)
{
	const std::string key = requestWith(options, start, {});
	if(!key.empty())
		cardKey = key;
}

std::string MapGenPreview::requestWith(const CMapGenOptions & options, const StartInfo & start, const std::map<std::string, std::string> & replaced)
{
	const bool card = replaced.empty();
	const auto generator = generatorFor(options);
	if(!generator)
		return {};
	std::vector<std::string> args;
	try
	{
		boost::filesystem::path outPath;
		args = MapGenerators::gameArguments(*generator, options, start, PREVIEW_SEED, outPath);
	}
	catch(const std::exception & e)
	{
		logGlobal->warn("Map preview: the arguments for %s: %s", generator->name, e.what());
		return {};
	}
	// the generator is told to write no map in the user's Maps folder: a preview is never in the scenario list
	for(size_t i = 0; i + 1 < args.size(); ++i)
		if(args[i] == "--out")
			args[i + 1] = (previewFolder() / "preview.vmap").string();
	for(const auto & [flag, value] : replaced)
	{
		// S246: an empty value takes the flag out (the template menu's open layout, Classic, is the generator with no --template)
		if(value.empty())
		{
			for(size_t i = 0; i + 1 < args.size();)
			{
				if(args[i] == flag)
					args.erase(args.begin() + i, args.begin() + i + 2);
				else
					++i;
			}
			continue;
		}
		bool found = false;
		for(size_t i = 0; i + 1 < args.size(); ++i)
			if(args[i] == flag)
			{
				args[i + 1] = value;
				found = true;
			}
		if(!found)
		{
			args.push_back(flag);
			args.push_back(value);
		}
	}

	const std::string key = generator->modID + "\n" + boost::algorithm::join(args, "\n");
	if(shownLayouts.count(key))
		return key;
	auto & state = pending();
	std::lock_guard guard(state.lock);
	if(!state.asked.insert(key).second)
		return key;
	if(card)
	{
		// the card shows only the newest settings: an older card run still waiting is not needed any more
		for(auto it = state.jobs.begin(); it != state.jobs.end();)
		{
			if(it->card)
			{
				state.asked.erase(it->key);
				it = state.jobs.erase(it);
			}
			else
				++it;
		}
	}
	state.jobs.push_front(Job{key, *generator, args, card});
	while(state.jobs.size() > MAX_QUEUED)
	{
		state.asked.erase(state.jobs.back().key);
		state.jobs.pop_back();
	}
	state.askedAt = std::chrono::steady_clock::now();
	if(!state.running)
	{
		state.running = true;
		std::thread(work).detach();
	}
	return key;
}

const JsonNode * MapGenPreview::landedLayout(const std::string & key)
{
	if(key.empty())
		return nullptr;
	const auto found = shownLayouts.find(key);
	if(found == shownLayouts.end() || !found->second["layout"].isVector() || found->second["layout"].Vector().empty())
		return nullptr;
	return &found->second;
}

namespace
{
const ColorRGBA BACKGROUND(22, 24, 30);
const ColorRGBA ZONE_BORDER(14, 16, 20);
const ColorRGBA WATER(16, 44, 88);
const ColorRGBA CASTLE_OUTLINE(255, 255, 255);
/// the template previews' zone fills (OmniMapGen tools/preview/layout_render.py FILL, TREASURE_SHADES), drawn darkened by 0.82 as there
const std::array<ColorRGBA, 3> TREASURE_SHADES = {ColorRGBA(92, 176, 112), ColorRGBA(54, 128, 78), ColorRGBA(146, 206, 150)};

ColorRGBA darkened(const ColorRGBA & color)
{
	return ColorRGBA(static_cast<uint8_t>(color.r * 0.82), static_cast<uint8_t>(color.g * 0.82), static_cast<uint8_t>(color.b * 0.82));
}

bool isTreasure(const std::string & kind)
{
	return kind == "highLoot" || kind == "lowLoot" || kind == "treasure";
}

ColorRGBA fillOf(const std::string & kind)
{
	if(kind == "player" || kind == "start" || kind == "cpuStart" || kind == "playerStart")
		return ColorRGBA(224, 164, 48);
	if(kind == "town")
		return ColorRGBA(206, 120, 92);
	if(kind == "junction")
		return ColorRGBA(150, 124, 200);
	if(kind == "water")
		return WATER;
	return ColorRGBA(168, 158, 136); // standard: open land
}

/// the game's player colours, as the template previews draw them; player 1 is red
ColorRGBA playerColor(const std::string & name)
{
	static const std::map<std::string, ColorRGBA> colors = {
		{"red", ColorRGBA(255, 0, 0)}, {"blue", ColorRGBA(49, 82, 255)}, {"tan", ColorRGBA(156, 115, 82)}, {"green", ColorRGBA(66, 148, 41)},
		{"orange", ColorRGBA(255, 132, 0)}, {"purple", ColorRGBA(140, 41, 165)}, {"teal", ColorRGBA(8, 156, 165)}, {"pink", ColorRGBA(198, 123, 140)}};
	const auto found = colors.find(name);
	return found == colors.end() ? ColorRGBA(150, 150, 150) : found->second;
}

/// a small castle centred on (cx, cy), h pixels high: three towers with notches, a wall and a gate (layout_render.py castle()), the outline first
void drawCastle(Canvas & canvas, int cx, int cy, int h, const ColorRGBA & fill)
{
	const double w = h * 1.15;
	const int o = std::max(1, static_cast<int>(std::lround(h * 0.12)));
	const double left = cx - w / 2;
	const double right = cx + w / 2;
	const double top = cy - h / 2.0;
	const double bottom = cy + h / 2.0;
	const double tower = w * 0.30;
	const double middle = w * 0.34;
	const std::array<std::array<double, 4>, 4> parts = {{
		{left, top + h * 0.22, left + tower, bottom},
		{right - tower, top + h * 0.22, right, bottom},
		{cx - middle / 2, top, cx + middle / 2, bottom},
		{left, static_cast<double>(cy), right, bottom}}};
	const auto box = [&canvas](double x0, double y0, double x1, double y1, int grow, const ColorRGBA & color)
	{
		const int ix0 = static_cast<int>(std::floor(x0)) - grow;
		const int iy0 = static_cast<int>(std::floor(y0)) - grow;
		const int ix1 = static_cast<int>(std::ceil(x1)) + grow;
		const int iy1 = static_cast<int>(std::ceil(y1)) + grow;
		canvas.drawColor(Rect(ix0, iy0, std::max(1, ix1 - ix0), std::max(1, iy1 - iy0)), color);
	};
	for(const auto & part : parts)
		box(part[0], part[1], part[2], part[3], o, CASTLE_OUTLINE);
	for(const auto & part : parts)
		box(part[0], part[1], part[2], part[3], 0, fill);
	for(size_t i = 0; i < 3; ++i)
	{
		const double notch = (parts[i][2] - parts[i][0]) * 0.34;
		const double mid = (parts[i][0] + parts[i][2]) / 2;
		box(mid - notch / 2, parts[i][1], mid + notch / 2, parts[i][1] + h * 0.13, 0, CASTLE_OUTLINE);
	}
	const double gate = w * 0.17;
	box(cx - gate / 2, bottom - h * 0.30, cx + gate / 2, bottom, 0, CASTLE_OUTLINE);
}

/// one level of the layout drawn into a side x side canvas of real pixels
std::unique_ptr<Canvas> drawLevel(const JsonNode & layout, size_t level, int side)
{
	auto canvas = std::make_unique<Canvas>(Point(side, side), CanvasScalingPolicy::IGNORE);
	canvas->drawColor(Rect(0, 0, side, side), BACKGROUND);

	const JsonNode & data = layout["layout"].Vector()[level];
	const int W = static_cast<int>(data["W"].Integer());
	const int H = static_cast<int>(data["H"].Integer());
	const auto & cells = data["zone"].Vector();
	if(W <= 0 || H <= 0 || cells.size() != static_cast<size_t>(W) * H)
		return canvas;
	std::vector<int> zone(cells.size());
	for(size_t i = 0; i < cells.size(); ++i)
		zone[i] = static_cast<int>(cells[i].Integer());
	std::vector<std::string> kinds;
	for(const auto & kind : data["classes"].Vector())
		kinds.push_back(kind.String());
	const auto kindOf = [&kinds](int id) { return id >= 0 && id < static_cast<int>(kinds.size()) ? kinds[id] : std::string("standard"); };

	// treasure zones that touch take different greens, the biggest first (layout_render.py treasure_shades)
	std::map<int, int> zoneSize;
	std::map<int, std::set<int>> touching;
	for(int y = 0; y < H; ++y)
	{
		for(int x = 0; x < W; ++x)
		{
			const int a = zone[y * W + x];
			zoneSize[a]++;
			if(x + 1 < W && zone[y * W + x + 1] != a)
			{
				touching[a].insert(zone[y * W + x + 1]);
				touching[zone[y * W + x + 1]].insert(a);
			}
			if(y + 1 < H && zone[(y + 1) * W + x] != a)
			{
				touching[a].insert(zone[(y + 1) * W + x]);
				touching[zone[(y + 1) * W + x]].insert(a);
			}
		}
	}
	std::vector<int> treasure;
	for(const auto & entry : zoneSize)
		if(isTreasure(kindOf(entry.first)))
			treasure.push_back(entry.first);
	std::sort(treasure.begin(), treasure.end(), [&zoneSize](int a, int b) { return zoneSize[a] > zoneSize[b]; });
	std::map<int, size_t> shade;
	for(int id : treasure)
	{
		std::set<size_t> used;
		for(int other : touching[id])
			if(shade.count(other))
				used.insert(shade[other]);
		size_t pick = 0;
		while(pick < TREASURE_SHADES.size() && used.count(pick))
			++pick;
		shade[id] = pick < TREASURE_SHADES.size() ? pick : 0;
	}
	const auto colorOf = [&](int id)
	{
		if(id < 0)
			return BACKGROUND;
		if(shade.count(id))
			return darkened(TREASURE_SHADES[shade[id]]);
		return darkened(fillOf(kindOf(id)));
	};

	// water, when the generator gives it: one entry a tile, per level ("waterTiles", MapGen Dev's key, beside the "water" summary of shape and
	// coverage; "water" itself when it is the per-level mask, as in the generator's dump that layout_render.py reads)
	std::vector<bool> water;
	const JsonNode & mask = layout["waterTiles"].isVector() ? layout["waterTiles"] : layout["water"];
	if(mask.isVector() && level < mask.Vector().size())
	{
		const auto & wet = mask.Vector()[level].Vector();
		if(wet.size() == cells.size())
			for(const auto & tile : wet)
				water.push_back(tile.isNumber() ? tile.Integer() != 0 : tile.Bool());
	}

	// the map fitted into the square, kept in proportion
	const double scale = static_cast<double>(side) / std::max(W, H);
	const int width = std::max(1, static_cast<int>(W * scale));
	const int height = std::max(1, static_cast<int>(H * scale));
	const int offX = (side - width) / 2;
	const int offY = (side - height) / 2;
	const int border = std::max(1, side / 160);
	const auto tileAt = [&](int px, int py) { return std::min(H - 1, py * H / height) * W + std::min(W - 1, px * W / width); };

	for(int py = 0; py < height; ++py)
	{
		int runStart = 0;
		ColorRGBA runColor = BACKGROUND;
		for(int px = 0; px <= width; ++px)
		{
			ColorRGBA color = runColor;
			if(px < width)
			{
				const int tile = tileAt(px, py);
				const bool wet = !water.empty() && water[tile];
				const int id = zone[tile];
				bool edge = false;
				if(!wet)
				{
					for(int k = 1; k <= border && !edge; ++k)
					{
						if(px + k < width && zone[tileAt(px + k, py)] != id)
							edge = true;
						if(py + k < height && zone[tileAt(px, py + k)] != id)
							edge = true;
					}
				}
				color = wet ? WATER : (edge ? ZONE_BORDER : colorOf(id));
			}
			if(px == width || px == 0 || color.r != runColor.r || color.g != runColor.g || color.b != runColor.b)
			{
				if(px > runStart)
					canvas->drawColor(Rect(offX + runStart, offY + py, px - runStart, 1), runColor);
				runStart = px;
				runColor = color;
			}
		}
	}

	// the starts: castles in the player's colour, no numbers (K, November 5th: "player starts dont need numbers, only colors corresponding to the player color that gets it")
	// a start on the map's edge keeps its whole castle inside the panel, as the template previews do (layout_render.py spread_castles keeps
	// every castle a margin inside its panel)
	// S246: 5 pixels at least (7 was most of a water menu card's small picture)
	const int castleHeight = std::max(5, side / 11);
	const int castleOutline = std::max(1, static_cast<int>(std::lround(castleHeight * 0.12)));
	const int marginX = static_cast<int>(std::ceil(castleHeight * 1.15 / 2)) + castleOutline + 1;
	const int marginY = static_cast<int>(std::ceil(castleHeight / 2.0)) + castleOutline + 1;
	for(const auto & start : layout["starts"].Vector())
	{
		if(static_cast<size_t>(start["level"].Integer()) != level)
			continue;
		const int cx = std::clamp(offX + static_cast<int>((start["x"].Float() + 0.5) * width / W), marginX, side - marginX);
		const int cy = std::clamp(offY + static_cast<int>((start["y"].Float() + 0.5) * height / H), marginY, side - marginY);
		drawCastle(*canvas, cx, cy, castleHeight, playerColor(start["color"].String()));
	}
	return canvas;
}
}

MapGenPreviewBox::MapGenPreviewBox(const Rect & position, std::string key, bool drawEmpty, int onlyLevel)
	: key(std::move(key))
	, drawEmpty(drawEmpty)
	, onlyLevel(onlyLevel)
{
	pos.x += position.x;
	pos.y += position.y;
	pos.w = position.w;
	pos.h = position.h;
}

void MapGenPreviewBox::redrawPanels()
{
	panels.clear();
	panelAt.clear();
	const int scale = ENGINE->screenHandler().getScalingFactor();
	drawnSerial = shownSerial;
	drawnScale = scale;
	drawnSize = Point(pos.w, pos.h);
	const JsonNode & shownLayout = layoutFor(key);
	if(!shownLayout["layout"].isVector() || shownLayout["layout"].Vector().empty())
		return;
	const size_t levels = shownLayout["layout"].Vector().size();
	if(onlyLevel >= 0)
	{
		// one level, the largest square the box holds, centred
		if(static_cast<size_t>(onlyLevel) >= levels)
			return;
		const int side = std::max(8, std::min(pos.w, pos.h));
		panels.push_back(drawLevel(shownLayout, onlyLevel, side * scale));
		panelAt.emplace_back((pos.w - side) / 2, (pos.h - side) / 2);
		return;
	}
	constexpr int gap = 4;
	const int side = std::max(8, std::min(pos.h, (pos.w - gap * static_cast<int>(levels - 1)) / static_cast<int>(levels)));
	const int used = side * static_cast<int>(levels) + gap * static_cast<int>(levels - 1);
	for(size_t level = 0; level < levels; ++level)
	{
		panels.push_back(drawLevel(shownLayout, level, side * scale));
		panelAt.emplace_back(pos.w - used + static_cast<int>(level) * (side + gap), (pos.h - side) / 2);
	}
}

void MapGenPreviewBox::showAll(Canvas & to)
{
	if(drawnSerial != shownSerial || drawnScale != ENGINE->screenHandler().getScalingFactor() || drawnSize != Point(pos.w, pos.h))
		redrawPanels();
	if(panels.empty())
	{
		if(drawEmpty)
			to.drawColor(Rect(pos.x + pos.w - pos.h, pos.y, pos.h, pos.h), BACKGROUND);
		return;
	}
	// S246: one level in a menu's picture place takes the whole place, so nothing of the module's picture under it shows beside the live one
	if(onlyLevel >= 0)
		to.drawColor(pos, BACKGROUND);
	for(size_t i = 0; i < panels.size(); ++i)
		to.draw(*panels[i], pos.topLeft() + panelAt[i]);
}
