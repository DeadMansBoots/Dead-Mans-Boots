/*
 * RenderHandler.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "RenderHandler.h"

#include "SDLImage.h"
#include "ScalableImage.h"
#include "FontChain.h"

#include "../GameEngine.h"

#include "../render/AssetGenerator.h"
#include "../../lib/filesystem/MenuBackgrounds.h"
#include "../render/CAnimation.h"
#include "../render/CanvasImage.h"
#include "../render/CDefFile.h"
#include "../render/IImageLoader.h"
#include <SDL_surface.h>
#include <SDL_pixels.h>
#include "../render/Colors.h"
#include "../render/ColorFilter.h"
#include "../render/IScreenHandler.h"
#include "../render/hdEdition/HdImageLoader.h"

#include "../../lib/AsyncRunner.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/CThreadHelper.h"
#include "../../lib/ExceptionsCommon.h"
#include "../../lib/VCMIDirs.h"
#include "../../lib/constants/StringConstants.h"
#include "../../lib/entities/building/CBuilding.h"
#include "../../lib/entities/faction/CTown.h"
#include "../../lib/entities/faction/CTownHandler.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/json/JsonUtils.h"

#include <vcmi/ArtifactService.h>
#include <vcmi/CreatureService.h>
#include <vcmi/Entity.h>
#include <vcmi/FactionService.h>
#include <vcmi/HeroTypeService.h>
#include <vcmi/Services.h>
#include <vcmi/SkillService.h>
#include <vcmi/spells/Service.h>
#include <vcmi/ResourceTypeService.h>

RenderHandler::RenderHandler()
	:assetGenerator(std::make_unique<AssetGenerator>())
{
}

RenderHandler::~RenderHandler() = default;

std::shared_ptr<CDefFile> RenderHandler::getAnimationFile(const AnimationPath & path)
{
	AnimationPath actualPath = boost::starts_with(path.getName(), "SPRITES") ? path : path.addPrefix("SPRITES/");

	{
		std::lock_guard<std::mutex> lock(animationCacheMutex);
		auto it = animationFiles.find(actualPath);

		if (it != animationFiles.end())
		{
			auto locked = it->second.lock();
			if (locked)
				return locked;
		}
	}

	// DMB: "SPRITES/DMBM-<release>-<name>", a main menu button out of that release's own sprite archive (MenuBackgrounds), so a release's
	// picture is drawn with the buttons made for it
	std::shared_ptr<CDefFile> result;
	// "SPRITES/DMBN-<release>-<name>" is the same button with its baked-in glow turned to a soft shadow, for a picture it was not made for
	const bool neutral = boost::istarts_with(actualPath.getName(), "SPRITES/DMBN-");
	if(neutral || boost::istarts_with(actualPath.getName(), "SPRITES/DMBM-"))
	{
		const std::string rest = actualPath.getName().substr(std::string("SPRITES/DMBM-").size());
		const auto dash = rest.find('-');
		if(dash == std::string::npos)
			return nullptr;
		const std::string releaseId = boost::to_lower_copy(rest.substr(0, dash));
		for(const auto & era : MenuBackgrounds::eras())
		{
			if(era.id != releaseId)
				continue;
			const auto bytes = neutral ? MenuBackgrounds::neutralSprite(era, rest.substr(dash + 1)) : MenuBackgrounds::sprite(era, rest.substr(dash + 1));
			if(bytes.empty())
				return nullptr;
			auto copy = std::make_unique<ui8[]>(bytes.size());
			std::copy(bytes.begin(), bytes.end(), copy.get());
			result = std::make_shared<CDefFile>(actualPath, std::move(copy));
		}
		if(!result)
			return nullptr;
	}
	else
	{
		if (!CResourceHandler::get()->existsResource(actualPath))
			return nullptr;
		result = std::make_shared<CDefFile>(actualPath);
	}

	auto entries = result->getEntries();
	
	{
		std::lock_guard<std::mutex> lock(animationCacheMutex);
		for(const auto& entry : entries)
			for(size_t i = 0; i < entry.second; ++i)
				animationSpriteDefs[actualPath][entry.first][i] = {result->getName(i, entry.first), result->getFrameInfo(i, entry.first)};

		animationFiles[actualPath] = result;
	}
	
	return result;
}

std::pair<std::string, CDefFile::SSpriteDef> RenderHandler::getAnimationSpriteDef(const AnimationPath & path, int frame, int group)
{
	AnimationPath actualPath = boost::starts_with(path.getName(), "SPRITES") ? path : path.addPrefix("SPRITES/");

	return animationSpriteDefs[actualPath][group][frame];
}

ImagePath RenderHandler::getAnimationFrameName(const AnimationPath & path, int frame, int group)
{
	auto info = getAnimationSpriteDef(path, frame, group);

	auto frameName = info.first;
	boost::iterator_range<std::string::iterator> sub = boost::find_first(frameName, ".");
	if(!sub.empty())
		frameName = std::string(frameName.begin(), sub.begin());
	boost::to_upper(frameName);

	return ImagePath::builtin(frameName);
}

void RenderHandler::initFromJson(AnimationLayoutMap & source, const JsonNode & config, EImageBlitMode mode) const
{
	std::string basepath;
	basepath = config["basepath"].String();

	JsonNode base;
	base["margins"] = config["margins"];
	base["width"] = config["width"];
	base["height"] = config["height"];

	for(const JsonNode & group : config["sequences"].Vector())
	{
		size_t groupID = group["group"].Integer();//TODO: string-to-value conversion("moving" -> MOVING)
		source[groupID].clear();

		for(const JsonNode & frame : group["frames"].Vector())
		{
			JsonNode toAdd = frame;
			JsonUtils::inherit(toAdd, base);
			toAdd["file"].String() = basepath + frame.String();
			if(group["generateShadow"].isNumber())
				toAdd["generateShadow"].Integer() = group["generateShadow"].Integer();
			if(group["generateOverlay"].isNumber())
				toAdd["generateOverlay"].Integer() = group["generateOverlay"].Integer();
			source[groupID].emplace_back(toAdd, mode);
		}
	}

	for(const JsonNode & node : config["images"].Vector())
	{
		size_t group = node["group"].Integer();
		size_t frame = node["frame"].Integer();

		if (source[group].size() <= frame)
			source[group].resize(frame+1);

		JsonNode toAdd = node;
		JsonUtils::inherit(toAdd, base);

		if (toAdd.Struct().count("file"))
			toAdd["file"].String() = basepath + node["file"].String();

		if (toAdd.Struct().count("defFile"))
			toAdd["defFile"].String() = basepath + node["defFile"].String();

		source[group][frame] = ImageLocator(toAdd, mode);
	}
}

RenderHandler::AnimationLayoutMap & RenderHandler::getAnimationLayout(const AnimationPath & path, int scalingFactor, EImageBlitMode mode)
{
	static constexpr std::array scaledSpritesPath = {
		"", // 0x
		"SPRITES/",
		"SPRITES2X/",
		"SPRITES3X/",
		"SPRITES4X/",
	};

	std::string pathString = path.getName();

	if (boost::starts_with(pathString, "SPRITES/"))
		pathString = pathString.substr(std::string("SPRITES/").length());

	AnimationPath actualPath = AnimationPath::builtin(scaledSpritesPath.at(scalingFactor) + pathString);

	auto it = animationLayouts.find(actualPath);

	if (it != animationLayouts.end() && (settings["video"]["useHdTextures"].Bool() || scalingFactor == 1))
		return it->second;

	AnimationLayoutMap result;

	auto defFile = getAnimationFile(actualPath);
	if(defFile)
	{
		const std::map<size_t, size_t> defEntries = defFile->getEntries();

		for (const auto & defEntry : defEntries)
			result[defEntry.first].resize(defEntry.second);
	}

	auto jsonResource = actualPath.toType<EResType::JSON>();
	auto configList = CResourceHandler::get()->getResourcesWithName(jsonResource);

	for(auto & loader : configList)
	{
		auto stream = loader->load(jsonResource);
		std::unique_ptr<ui8[]> textData(new ui8[stream->getSize()]);
		stream->read(textData.get(), stream->getSize());

		const JsonNode config(reinterpret_cast<const std::byte*>(textData.get()), stream->getSize(), path.getOriginalName());

		initFromJson(result, config, mode);
	}

	animationLayouts[actualPath] = result;
	return animationLayouts[actualPath];
}

int RenderHandler::getScalingFactor() const
{
	return ENGINE->screenHandler().getScalingFactor();
}

ImageLocator RenderHandler::getLocatorForAnimationFrame(const AnimationPath & path, int frame, int group, int scaling, EImageBlitMode mode)
{
	const auto & layout = getAnimationLayout(path, scaling, mode);
	if (!layout.count(group))
		return ImageLocator();

	if (frame >= layout.at(group).size())
		return ImageLocator();

	const auto & locator = layout.at(group).at(frame);
	if (locator.image || locator.defFile)
		return locator;

	return ImageLocator(path, frame, group, mode);
}

std::shared_ptr<ScalableImageShared> RenderHandler::loadImageImpl(const ImageLocator & locator)
{
	auto it = imageFiles.find(locator);
	if (it != imageFiles.end())
	{
		auto locked = it->second.lock();
		if (locked)
			return locked;
	}

	auto sdlImage = loadImageFromFileUncached(locator);
	auto scaledImage = std::make_shared<ScalableImageShared>(locator, sdlImage);

	storeCachedImage(locator, scaledImage);
	return scaledImage;
}

namespace
{
/// S377, S461, S498: a DEF frame read as its palette indices (the cut needs the indices, not colours)
class DmbIndexLoader final : public IImageLoader
{
public:
	Point size;
	Point margins;
	Point fullSize;
	std::vector<ui8> pixels;
	std::array<SDL_Color, 256> palette{};
	size_t position = 0;
	size_t lineStart = 0;

	void init(Point SpriteSize, Point Margins, Point FullSize, SDL_Color * pal) override
	{
		size = SpriteSize;
		margins = Margins;
		fullSize = FullSize;
		pixels.assign(static_cast<size_t>(size.x) * size.y, 0);
		if(pal)
			std::copy(pal, pal + palette.size(), palette.begin());
	}
	void load(size_t n, const ui8 * data) override
	{
		for(size_t i = 0; i < n && position < pixels.size(); ++i)
			pixels[position++] = data[i];
	}
	void load(size_t n, ui8 color) override
	{
		for(size_t i = 0; i < n && position < pixels.size(); ++i)
			pixels[position++] = color;
	}
	void endLine() override
	{
		lineStart += size.x;
		position = lineStart;
	}
};

/// S377 (K, November 19th: "for these, the buttons backgrounds should match the main background"), S461 and S498 (K, November 24th, on the
/// cut: "No, nothing is wrongly cut now"): the five difficulty chess pieces (GSPBUT3 to GSPBUT7, frame 0) cut from their blue tile, made here
/// from the player's own art each time (Heroes III art is never shipped). The rule is the accepted one (skill heroes3-art-extraction, tool
/// ! LLM Files\Tools\art_extract\s461_pieces.py): the tile is the palette indices read where only the tile is (inside the 2-pixel frame
/// band, outside each piece's box, the five frames pooled) plus the tile's darker shades (blue, no lighter in red, green or blue than that
/// texture); the frame band goes and a flood from it takes only tile indices. Every other pixel keeps its index, the player-colour range
/// (224 to 255) included, so a player's colour can be cast over the piece later
std::array<std::shared_ptr<ISharedImage>, 5> cutChessPieces(const std::function<std::shared_ptr<CDefFile>(const AnimationPath &)> & defFile)
{
	constexpr int BAND = 2;
	constexpr int PLAYER_FIRST = 224;
	std::array<DmbIndexLoader, 5> frames;
	for(int tier = 0; tier < 5; ++tier)
	{
		const auto def = defFile(AnimationPath::builtin("GSPBUT" + std::to_string(3 + tier) + ".DEF"));
		if(!def || !def->hasFrame(0, 0))
			return {};
		def->loadFrame(0, 0, frames[tier]);
	}
	const auto & palette = frames[0].palette;
	const auto tileBlue = [&palette](int index)
	{
		const SDL_Color & c = palette[index];
		return index < PLAYER_FIRST && c.b > c.r + 10 && c.b >= c.g;
	};
	const auto flood = [](const DmbIndexLoader & frame, const std::function<bool(int)> & through)
	{
		const int w = frame.size.x;
		const int h = frame.size.y;
		std::vector<char> tile(static_cast<size_t>(w) * h, 0);
		std::vector<Point> todo;
		for(int y = 0; y < h; ++y)
			for(int x = 0; x < w; ++x)
				if(x < BAND || y < BAND || x >= w - BAND || y >= h - BAND)
				{
					tile[y * w + x] = 1;
					todo.emplace_back(x, y);
				}
		while(!todo.empty())
		{
			const Point p = todo.back();
			todo.pop_back();
			for(const Point & d : {Point(1, 0), Point(-1, 0), Point(0, 1), Point(0, -1)})
			{
				const Point n = p + d;
				if(n.x < 0 || n.y < 0 || n.x >= w || n.y >= h || tile[n.y * w + n.x] || !through(frame.pixels[n.y * w + n.x]))
					continue;
				tile[n.y * w + n.x] = 1;
				todo.push_back(n);
			}
		}
		return tile;
	};

	// the tile's own indices, read outside each piece's rough box (the first pass floods by colour only to find the box)
	std::set<int> tileIndices;
	for(const auto & frame : frames)
	{
		const int w = frame.size.x;
		const int h = frame.size.y;
		const auto rough = flood(frame, tileBlue);
		int x0 = w;
		int y0 = h;
		int x1 = -1;
		int y1 = -1;
		for(int y = 0; y < h; ++y)
			for(int x = 0; x < w; ++x)
				if(!rough[y * w + x])
				{
					x0 = std::min(x0, x);
					y0 = std::min(y0, y);
					x1 = std::max(x1, x);
					y1 = std::max(y1, y);
				}
		for(int y = BAND; y < h - BAND; ++y)
			for(int x = BAND; x < w - BAND; ++x)
				if(!(x >= x0 - 1 && x <= x1 + 1 && y >= y0 - 1 && y <= y1 + 1))
					tileIndices.insert(frame.pixels[y * w + x]);
	}
	// and its darker shades: blue, no lighter than the texture in any channel
	int ceilR = 0;
	int ceilG = 0;
	int ceilB = 0;
	for(int index : tileIndices)
	{
		ceilR = std::max<int>(ceilR, palette[index].r);
		ceilG = std::max<int>(ceilG, palette[index].g);
		ceilB = std::max<int>(ceilB, palette[index].b);
	}
	for(int index = 0; index < PLAYER_FIRST; ++index)
		if(tileBlue(index) && palette[index].r <= ceilR && palette[index].g <= ceilG && palette[index].b <= ceilB)
			tileIndices.insert(index);

	std::array<std::shared_ptr<ISharedImage>, 5> pieces;
	for(int tier = 0; tier < 5; ++tier)
	{
		const auto & frame = frames[tier];
		const auto tile = flood(frame, [&tileIndices](int index) { return tileIndices.count(index) != 0; });
		SDL_Surface * surface = SDL_CreateRGBSurface(0, frame.fullSize.x, frame.fullSize.y, 8, 0, 0, 0, 0);
		if(!surface)
			return {};
		SDL_Palette * surfacePalette = SDL_AllocPalette(256);
		SDL_SetPaletteColors(surfacePalette, frame.palette.data(), 0, 256);
		SDL_SetSurfacePalette(surface, surfacePalette);
		SDL_FreePalette(surfacePalette);
		SDL_FillRect(surface, nullptr, 0);
		int kept = 0;
		for(int y = 0; y < frame.size.y; ++y)
			for(int x = 0; x < frame.size.x; ++x)
				if(!tile[y * frame.size.x + x])
				{
					static_cast<ui8 *>(surface->pixels)[(y + frame.margins.y) * surface->pitch + x + frame.margins.x] = frame.pixels[y * frame.size.x + x];
					++kept;
				}
		SDL_SetColorKey(surface, SDL_TRUE, 0);
		pieces[tier] = std::make_shared<SDLImageShared>(surface);
		SDL_FreeSurface(surface); // the image keeps its own reference
		logGlobal->debug("S498: chess piece %d cut at run time: %d pixels kept of %dx%d", tier, kept, frame.size.x, frame.size.y);
	}
	return pieces;
}
}

std::shared_ptr<ISharedImage> RenderHandler::loadImageFromFileUncached(const ImageLocator & locator)
{
	if(locator.image)
	{
		auto imagePath = *locator.image;
		auto imagePathSprites = imagePath.addPrefix("SPRITES/");
		auto imagePathData = imagePath.addPrefix("DATA/");

		if(CResourceHandler::get()->existsResource(imagePathSprites))
			return std::make_shared<SDLImageShared>(imagePathSprites);

		if(CResourceHandler::get()->existsResource(imagePathData))
			return std::make_shared<SDLImageShared>(imagePathData);

		if(CResourceHandler::get()->existsResource(imagePath))
			return std::make_shared<SDLImageShared>(imagePath);

		// S498: "DmbChessPiece0.png" to "DmbChessPiece4.png", the difficulty pieces cut from their tile (cutChessPieces)
		for(int tier = 0; tier < 5; ++tier)
			if(imagePath == ImagePath::builtin("DmbChessPiece" + std::to_string(tier) + ".png"))
			{
				const auto pieces = cutChessPieces([this](const AnimationPath & path) { return getAnimationFile(path); });
				if(pieces[tier])
					return pieces[tier];
			}

		auto generated = assetGenerator->generateImage(imagePath);
		if (generated)
		{
			generated->setAsyncUpscale(false); // do not async upscale base image for generated images -> fixes #6201
			return generated;
		}

		logGlobal->error("Failed to load image '%s'", locator.image->getOriginalName().c_str());
		return std::make_shared<SDLImageShared>(ImagePath::builtin("DEFAULT"));
	}

	if(locator.defFile)
	{
		auto defFile = getAnimationFile(*locator.defFile);
		if(defFile && defFile->hasFrame(locator.defFrame, locator.defGroup))
		{
			auto img = std::make_shared<SDLImageShared>(defFile.get(), locator.defFrame, locator.defGroup);

			auto pathForDefFrame = getAnimationFrameName(*locator.defFile, locator.defFrame, locator.defGroup);
			if(hdImageLoader->exists(pathForDefFrame))
				img->setAsyncUpscale(false); // avoids flickering graphics when hd textures are enabled

			return img;
		}
		else
		{
			logGlobal->error("Frame %d in group %d not found in file: %s", 
				locator.defFrame, locator.defGroup, locator.defFile->getName().c_str());
			return std::make_shared<SDLImageShared>(ImagePath::builtin("DEFAULT"));
		}
	}

	throw std::runtime_error("Invalid image locator received!");
}

void RenderHandler::storeCachedImage(const ImageLocator & locator, std::shared_ptr<ScalableImageShared> image)
{
	imageFiles[locator] = image;
}

std::shared_ptr<SDLImageShared> RenderHandler::loadScaledImage(const ImageLocator & locator)
{
	static constexpr std::array scaledDataPath = {
		"", // 0x
		"DATA/",
		"DATA2X/",
		"DATA3X/",
		"DATA4X/",
	};

	static constexpr std::array scaledSpritesPath = {
		"", // 0x
		"SPRITES/",
		"SPRITES2X/",
		"SPRITES3X/",
		"SPRITES4X/",
	};

	ImagePath pathToLoad;
	Point defMargins(0, 0);
	Point defFullSize(0, 0);

	if(locator.defFile)
	{
		auto remappedLocator = getLocatorForAnimationFrame(*locator.defFile, locator.defFrame, locator.defGroup, locator.scalingFactor, locator.layer);
		// we expect that .def's are only used for 1x data, upscaled assets should use standalone images
		if (!remappedLocator.image)
		{
			if(!settings["video"]["useHdTextures"].Bool() || locator.scalingFactor == 1)
				return nullptr;

			if(!CResourceHandler::get("core")->existsResource(*locator.defFile)) // HD mod supports only core
				return nullptr;

			auto info = getAnimationSpriteDef(*locator.defFile, locator.defFrame, locator.defGroup);
			defMargins = Point(info.second.leftMargin, info.second.topMargin);
			defFullSize = Point(info.second.fullWidth, info.second.fullHeight);

			auto pathForDefFrame = getAnimationFrameName(*locator.defFile, locator.defFrame, locator.defGroup);
			if(hdImageLoader->exists(pathForDefFrame))
				pathToLoad = pathForDefFrame;
		}
		else
			pathToLoad = *remappedLocator.image;
	}

	if(locator.image)
		pathToLoad = *locator.image;

	if (pathToLoad.empty())
		return nullptr;

	std::string imagePathString = pathToLoad.getName();
	auto imagePathOriginal = ImagePath::builtin(imagePathString);

	bool generateShadow = locator.generateShadow && (*locator.generateShadow) != SharedImageLocator::ShadowMode::SHADOW_NONE;
	bool generateOverlay = locator.generateOverlay && (*locator.generateOverlay) != SharedImageLocator::OverlayMode::OVERLAY_NONE;
	bool isShadow = locator.layer == EImageBlitMode::ONLY_SHADOW_HIDE_SELECTION || locator.layer == EImageBlitMode::ONLY_SHADOW_HIDE_FLAG_COLOR;
	bool isOverlay = locator.layer == EImageBlitMode::ONLY_FLAG_COLOR || locator.layer == EImageBlitMode::ONLY_SELECTION;
	bool optimizeImage = !(isShadow && generateShadow) && !(isOverlay && generateOverlay); // images needs to expanded
	bool overlay = isOverlay && !generateOverlay;
	bool shadow = isShadow && !generateShadow;

	if(overlay)
		imagePathString += "-OVERLAY";
	if(shadow)
		imagePathString += "-SHADOW";
	if(locator.playerColored.isValidPlayer())
		imagePathString += "-" + boost::to_upper_copy(GameConstants::PLAYER_COLOR_NAMES[locator.playerColored.getNum()]);
	if(locator.playerColored == PlayerColor::NEUTRAL)
		imagePathString += "-NEUTRAL";

	auto imagePath = ImagePath::builtin(imagePathString);
	auto imagePathSprites = ImagePath::builtin(imagePathString).addPrefix(scaledSpritesPath.at(locator.scalingFactor));
	auto imagePathData = ImagePath::builtin(imagePathString).addPrefix(scaledDataPath.at(locator.scalingFactor));

	std::shared_ptr<SDLImageShared> img = nullptr;

	if(!img && CResourceHandler::get()->existsResource(imagePathSprites) && (settings["video"]["useHdTextures"].Bool() || locator.scalingFactor == 1))
		img = std::make_shared<SDLImageShared>(imagePathSprites, optimizeImage);
	if(!img && CResourceHandler::get()->existsResource(imagePathData) && (settings["video"]["useHdTextures"].Bool() || locator.scalingFactor == 1))
		img = std::make_shared<SDLImageShared>(imagePathData, optimizeImage);
	if(!img && hdImageLoader->exists(imagePathOriginal) && settings["video"]["useHdTextures"].Bool() && locator.scalingFactor > 1)
	{
		if((!isOverlay || !isShadow) || overlay || shadow)
			img = hdImageLoader->getImage(imagePathOriginal, defFullSize, defMargins, shadow, overlay);
	}
	if(!img && CResourceHandler::get()->existsResource(imagePath))
		img = std::make_shared<SDLImageShared>(imagePath, optimizeImage);
	if(!img && locator.scalingFactor == 1)
		img = std::dynamic_pointer_cast<SDLImageShared>(assetGenerator->generateImage(imagePath));

	if(img)
	{
		// TODO: Performance improvement - Run algorithm on optimized ("trimmed") images
		// Not implemented yet because different frame image sizes seems to cause wobbeling shadow -> needs a way around this
		if(isShadow && generateShadow)
			img = img->drawShadow((*locator.generateShadow) == SharedImageLocator::ShadowMode::SHADOW_SHEAR);
		if(isOverlay && generateOverlay && (*locator.generateOverlay) == SharedImageLocator::OverlayMode::OVERLAY_OUTLINE)
			img = img->drawOutline(Colors::WHITE, 1);

		if(locator.scalingFactor == 1)
			img->setAsyncUpscale(false); // no base image, needs to be done in sync
	}

	return img;
}

std::shared_ptr<IImage> RenderHandler::loadImage(const ImageLocator & locator)
{
	ImageLocator adjustedLocator = locator;

	if(locator.image)
	{
		std::vector<std::string> splitted;
		boost::split(splitted, (*locator.image).getOriginalName(), boost::is_any_of(":"));
		if(splitted.size() == 3)
		{
			// allows image from def file with following filename (first group, then frame): "deffile.def:0:5"
			adjustedLocator.defFile = AnimationPath::builtin(splitted[0]);
			adjustedLocator.defGroup = std::stoi(splitted[1]);
			adjustedLocator.defFrame = std::stoi(splitted[2]);
			adjustedLocator.image = std::nullopt;
		}
	}

	std::shared_ptr<ScalableImageInstance> result;

	if (adjustedLocator.scalingFactor == 0)
	{
		auto scaledLocator = adjustedLocator;
		scaledLocator.scalingFactor = getScalingFactor();

		result = loadImageImpl(scaledLocator)->createImageReference();
	}
	else
		result = loadImageImpl(adjustedLocator)->createImageReference();

	if (locator.horizontalFlip)
		result->horizontalFlip();
	if (locator.verticalFlip)
		result->verticalFlip();

	return result;
}

std::shared_ptr<IImage> RenderHandler::loadImage(const AnimationPath & path, int frame, int group, EImageBlitMode mode)
{
	ImageLocator locator = getLocatorForAnimationFrame(path, frame, group, 1, mode);
	if (!locator.empty())
		return loadImage(locator);
	else
	{
		logGlobal->error("Failed to load non-existing image");
		return loadImage(ImageLocator(ImagePath::builtin("DEFAULT"), mode));
	}
}

std::shared_ptr<IImage> RenderHandler::loadImage(const ImagePath & path, EImageBlitMode mode)
{
	auto name = path.getOriginalName();
	
	std::vector<std::string> splitted;
	boost::split(splitted, name, boost::is_any_of(":"));
	if(splitted.size() == 3)
	{
		// allows image from def file with following filename (first group, then frame): "deffile.def:0:5"
		ImageLocator locator = getLocatorForAnimationFrame(AnimationPath::builtin(splitted[0]), std::stoi(splitted[2]), std::stoi(splitted[1]), 1, mode);
		return loadImage(locator);
	}

	ImageLocator locator(path, mode);
	return loadImage(locator);
}

std::shared_ptr<CanvasImage> RenderHandler::createImage(const Point & size, CanvasScalingPolicy scalingPolicy)
{
	return std::make_shared<CanvasImage>(size, scalingPolicy);
}

std::shared_ptr<CAnimation> RenderHandler::loadAnimation(const AnimationPath & path, EImageBlitMode mode)
{
	return std::make_shared<CAnimation>(path, getAnimationLayout(path, 1, mode), mode);
}

void RenderHandler::addImageListEntries(const EntityService * service)
{
	service->forEachBase([this](const Entity * entity, bool & stop)
	{
		entity->registerIcons([this](size_t index, size_t group, const std::string & listName, const std::string & imageName)
		{
			if (imageName.empty())
				return;

			auto & layout = getAnimationLayout(AnimationPath::builtin("SPRITES/" + listName), 1, EImageBlitMode::COLORKEY);

			JsonNode entry;
			entry["file"].String() = imageName;

			if (index >= layout[group].size())
				layout[group].resize(index + 1);

			layout[group][index] = ImageLocator(entry, EImageBlitMode::SIMPLE);
		});
	});
}

static void detectOverlappingBuildings(RenderHandler * renderHandler, const Faction * factionBase)
{
	if (!factionBase->hasTown())
		return;

	auto faction = dynamic_cast<const CFaction*>(factionBase);

	for (const auto & left : faction->town->clientInfo.structures)
	{
		for (const auto & right : faction->town->clientInfo.structures)
		{
			if (left->identifier <= right->identifier)
				continue; // only a<->b comparison is needed, not a<->a or b<->a

			if (left->building && right->building && left->building->getBase() == right->building->getBase())
			{
				if (left->pos.z != right->pos.z)
					logMod->warn("Town %s: Upgrades of same building have different z-index: '%s' and '%s'", faction->getJsonKey(), left->identifier, right->identifier);

				continue; // upgrades of the same buildings are expected to overlap
			}

			if (left->pos.z != right->pos.z)
				continue; // buildings already have different z-index and have well-defined overlap logic

			auto leftImage = renderHandler->loadImage(left->defName, 0, 0, EImageBlitMode::COLORKEY);
			auto rightImage = renderHandler->loadImage(right->defName, 0, 0, EImageBlitMode::COLORKEY);

			Rect leftRect( left->pos.x, left->pos.y, leftImage->width(), leftImage->height());
			Rect rightRect( right->pos.x, right->pos.y, rightImage->width(), rightImage->height());

			Rect intersection = leftRect.intersect(rightRect);

			Point intersectionPosition;
			bool intersectionFound = false;

			for (int y = 0; y < intersection.h && !intersectionFound; ++y)
			{
				for (int x = 0; x < intersection.w && !intersectionFound; ++x)
				{
					Point leftPoint = Point(x,y) - leftRect.topLeft() + intersection.topLeft();
					Point rightPoint = Point(x,y) - rightRect.topLeft() + intersection.topLeft();

					if (!leftImage->isTransparent(leftPoint) && !rightImage->isTransparent(rightPoint))
					{
						intersectionFound = true;
						intersectionPosition = intersection.topLeft() + Point(x,y);
					}
				}
			}

			if (intersectionFound)
				logMod->warn("Town %s: Detected overlapping buildings '%s' and '%s' at (%d, %d) with same z-index!", faction->getJsonKey(), left->identifier, right->identifier, intersectionPosition.x, intersectionPosition.y);
		}
	}
};

void RenderHandler::onLibraryLoadingFinished(const Services * services)
{
	hdImageLoader = std::make_unique<HdImageLoader>(); // needs to initialize after class construction because we need loaded screenHandler for getScalingFactor()

	assert(animationLayouts.empty());
	assetGenerator->initialize();
	updateGeneratedAssets();

	addImageListEntries(services->creatures());
	addImageListEntries(services->heroTypes());
	addImageListEntries(services->artifacts());
	addImageListEntries(services->factions());
	addImageListEntries(services->spells());
	addImageListEntries(services->skills());
	addImageListEntries(services->resources());

	if (settings["mods"]["validation"].String() == "full")
	{
		services->factions()->forEach([this](const Faction * factionBase, bool & stop)
		{
			detectOverlappingBuildings(this, factionBase);
		});
	}
}

std::shared_ptr<const IFont> RenderHandler::loadFont(EFonts font)
{
	if (fonts.count(font))
		return fonts.at(font);

	const int8_t index = static_cast<int8_t>(font);
	logGlobal->debug("Loading font %d", static_cast<int>(index));

	auto configList = CResourceHandler::get()->getResourcesWithName(JsonPath::builtin("config/fonts.json"));
	std::shared_ptr<FontChain> loadedFont = std::make_shared<FontChain>();
	std::string bitmapPath;

	for(auto & loader : configList)
	{
		auto stream = loader->load(JsonPath::builtin("config/fonts.json"));
		std::unique_ptr<ui8[]> textData(new ui8[stream->getSize()]);
		stream->read(textData.get(), stream->getSize());
		const JsonNode config(reinterpret_cast<const std::byte*>(textData.get()), stream->getSize(), "config/fonts.json");
		const JsonVector & bmpConf = config["bitmap"].Vector();
		const JsonNode   & ttfConf = config["trueType"];

		bitmapPath = bmpConf[index].String();
		if (!ttfConf[bitmapPath].isNull())
			loadedFont->addTrueTypeFont(ttfConf[bitmapPath], !config["lowPriority"].Bool());
	}
	// bitmap font may be corrupt or unreadable; fall back to already-added TrueType fonts rather than aborting
	try
	{
		loadedFont->addBitmapFont(bitmapPath);
	}
	catch (const std::exception & e)
	{
		logGlobal->error("Failed to load bitmap font '%s': %s", bitmapPath, e.what());
	}

	fonts[font] = loadedFont;
	return loadedFont;
}

void RenderHandler::exportGeneratedAssets()
{
	for (const auto & entry : assetGenerator->generateAllImages())
		entry.second->exportBitmap(VCMIDirs::get().userDataPath() / "Generated" / (entry.first.getOriginalName() + ".png"), nullptr);
}

std::shared_ptr<AssetGenerator> RenderHandler::getAssetGenerator()
{
	return assetGenerator;
}

void RenderHandler::updateGeneratedAssets()
{
	for(const auto & [key, value] : assetGenerator->generateAllAnimations())
		animationLayouts[key] = value;
}
