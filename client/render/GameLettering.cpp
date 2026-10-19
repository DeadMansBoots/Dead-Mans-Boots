/*
 * GameLettering.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "GameLettering.h"

#include "CAnimation.h"
#include "Canvas.h"
#include "CanvasImage.h"
#include "IImage.h"
#include "IRenderHandler.h"
#include "IScreenHandler.h"
#include "../GameEngine.h"

#include "../../lib/GameLibrary.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/texts/TextOperations.h"
#include "../../lib/vcmi_endian.h"

#include <SDL_ttf.h>

namespace
{
// the colours of the words the game carves into its gold buttons (RANSHOW, around its S)
const ColorRGBA CARVED_LETTER(0, 0, 0);
const ColorRGBA CARVED_EDGE(104, 88, 49);
const ColorRGBA CARVED_LIGHT(232, 230, 222);
constexpr double DARK = 0.45; // matches GameButtonLettering's own: a letter's own pixels, at least this black
constexpr int LETTER_GAP = 1;
constexpr int WORD_GAP = 6;
constexpr int CLOSEST = 1;
constexpr int FARTHEST = 5;
}

GameLettering::Letters GameLettering::letters(EFonts font, const std::string & text, bool trim)
{
	// the font's file, by its place in config/fonts.json's list of the game's bitmap fonts, as the
	// renderer loads it (RenderHandler::loadFont)
	const JsonNode config(JsonPath::builtin("config/fonts.json"));
	const auto & names = config["bitmap"].Vector();
	if(static_cast<size_t>(font) >= names.size())
		return {};
	const ResourcePath resource("data/" + names[font].String(), EResType::BMP_FONT);
	if(!CResourceHandler::get()->existsResource(resource))
		return {};
	const auto data = CResourceHandler::get()->load(resource)->readAll();
	const ui8 * bytes = data.first.get();
	const size_t size = data.second;

	// the layout CBitmapFont reads: the height at 5; each glyph's widths before, of and after its pixels
	// at 32; its pixels' offsets after those; the pixels, one byte each, 2 and up a letter's own
	constexpr size_t glyphs = 256;
	constexpr size_t widthsAt = 32;
	constexpr size_t offsetsAt = widthsAt + glyphs * 12;
	constexpr size_t pixelsAt = offsetsAt + glyphs * 4;
	if(size < pixelsAt)
		return {};
	Letters result;
	result.height = bytes[5];

	const std::string encoded = TextOperations::fromUnicode(text, LIBRARY->modh->findResourceEncoding(resource));
	struct Placed
	{
		int x, width;
		const ui8 * pixels;
	};
	std::vector<Placed> placed;
	// DMB: the spacings come from a font file a mod can supply: each is held to 255 either way, far past any Heroes III font's, and
	// the line is added up in 64 bits and held to MAX_LETTERING_WIDTH, where an int sum could overflow and the cells allocated below
	// could run to gigabytes; a font outside these is refused like any other unreadable one
	constexpr int GLYPH_RANGE = 255;
	constexpr int64_t MAX_LETTERING_WIDTH = 65536;
	int64_t x = 0;
	for(const char character : encoded)
	{
		const size_t c = static_cast<ui8>(character);
		const int before = static_cast<int32_t>(read_le_u32(bytes + widthsAt + c * 12));
		const int width = static_cast<int32_t>(read_le_u32(bytes + widthsAt + c * 12 + 4));
		const int after = static_cast<int32_t>(read_le_u32(bytes + widthsAt + c * 12 + 8));
		const size_t offset = pixelsAt + read_le_u32(bytes + offsetsAt + c * 4);
		// compared as a range: a spacing of INT_MIN has no absolute value in an int (std::abs of it is undefined)
		const auto inRange = [](int value) { return value >= -GLYPH_RANGE && value <= GLYPH_RANGE; };
		if(width < 0 || width > GLYPH_RANGE || !inRange(before) || !inRange(after)
			|| offset + static_cast<size_t>(width) * result.height > size)
			return {};
		placed.push_back({ static_cast<int>(x + before), width, bytes + offset });
		x += before + width + after;
		if(std::abs(x) > MAX_LETTERING_WIDTH)
			return {};
	}
	result.width = static_cast<int>(std::max<int64_t>(0, x));
	result.pixels.assign(result.height, std::vector<bool>(result.width, false));
	for(const auto & glyph : placed)
		for(int row = 0; row < result.height; ++row)
			for(int col = 0; col < glyph.width; ++col)
				if(glyph.pixels[row * glyph.width + col] >= 2 && glyph.x + col >= 0 && glyph.x + col < result.width)
					result.pixels[row][glyph.x + col] = true;

	if(!trim)
		return result;
	int left = result.width, right = -1, top = result.height, bottom = -1;
	for(int row = 0; row < result.height; ++row)
		for(int col = 0; col < result.width; ++col)
			if(result.pixels[row][col])
			{
				left = std::min(left, col);
				right = std::max(right, col);
				top = std::min(top, row);
				bottom = std::max(bottom, row);
			}
	if(right < 0)
		return {};
	Letters trimmed;
	trimmed.width = right - left + 1;
	trimmed.height = bottom - top + 1;
	for(int row = top; row <= bottom; ++row)
		trimmed.pixels.emplace_back(result.pixels[row].begin() + left, result.pixels[row].begin() + right + 1);
	return trimmed;
}

std::shared_ptr<IImage> GameLettering::carved(EFonts font, const std::string & text)
{
	const Letters box = letters(font, text, false);
	if(box.width <= 0 || box.height <= 0)
		return nullptr;
	const Point size(box.width + 2, box.height + 2);
	auto image = ENGINE->renderHandler().createImage(size, CanvasScalingPolicy::IGNORE);
	Canvas canvas = image->getCanvas();
	const auto letter = [&box](int col, int row)
	{
		return row >= 0 && row < box.height && col >= 0 && col < box.width && box.pixels[row][col];
	};
	for(int row = -1; row <= box.height; ++row)
		for(int col = -1; col <= box.width; ++col)
		{
			const Point at(col + 1, row + 1);
			if(letter(col, row))
				canvas.drawPoint(at, CARVED_LETTER);
			else if(letter(col - 1, row) || letter(col, row - 1) || letter(col - 1, row - 1))
				canvas.drawPoint(at, CARVED_LIGHT);
			else if(letter(col + 1, row))
				canvas.drawPoint(at, CARVED_EDGE);
		}
	// the game's own art is drawn at the screen's scale the same way
	if(ENGINE->screenHandler().getScalingFactor() > 1)
		image->scaleTo(size, EScalingAlgorithm::XBRZ_ALPHA);
	return image;
}

namespace
{
/// DMB's own button face (Data/DMBButtonFace.ttf in the vcmi mod: an open-licence heavy serif cut and renamed by DMB/release/make_button_face.py, licence
/// text beside it), the same face the launcher draws its buttons' words in (launcher/theme.cpp). K, October 12th 2026, on the game's letters taken
/// from the player's files and patched: "WHAT PART OF BUILD OUR OWN FONT SET DID YOU NOT UNDERSTAND". Loaded once for the screen's scale; its
/// capitals are as tall as the game's own on a 40 pixel bar.
struct ButtonFace
{
	std::pair<std::unique_ptr<ui8[]>, ui64> data;
	std::unique_ptr<TTF_Font, decltype(&TTF_CloseFont)> font{nullptr, TTF_CloseFont};
	int factor = 0;
	int ascent = 0;
	int capHeight = 0; ///< pixels, at the screen's scale
};

constexpr int CAPITALS = 22; ///< rows tall, as the letters of the game's gold buttons on its 40 pixel bars

const ButtonFace * buttonFace()
{
	static ButtonFace face;
	const int factor = std::max(1, ENGINE->screenHandler().getScalingFactor());
	if(face.font && face.factor == factor)
		return &face;
	face = ButtonFace();
	const ResourcePath path("Data/DMBButtonFace.ttf", EResType::TTF_FONT);
	if(!CResourceHandler::get()->existsResource(path))
		return nullptr;
	if(!TTF_WasInit() && TTF_Init() == -1)
		return nullptr;
	face.data = CResourceHandler::get()->load(path)->readAll();
	// the type size whose capitals are CAPITALS * factor pixels tall, from the capital H of a large probe
	constexpr int probe = 200;
	std::unique_ptr<TTF_Font, decltype(&TTF_CloseFont)> big(TTF_OpenFontRW(SDL_RWFromConstMem(face.data.first.get(), face.data.second), 1, probe), TTF_CloseFont);
	int minX = 0, maxX = 0, minY = 0, maxY = 0, advance = 0;
	if(!big || TTF_GlyphMetrics32(big.get(), 'H', &minX, &maxX, &minY, &maxY, &advance) != 0 || maxY <= 0)
		return nullptr;
	const int size = std::max(4, static_cast<int>(std::lround(static_cast<double>(CAPITALS * factor) * probe / maxY)));
	face.font.reset(TTF_OpenFontRW(SDL_RWFromConstMem(face.data.first.get(), face.data.second), 1, size));
	if(!face.font)
		return nullptr;
	TTF_SetFontHinting(face.font.get(), TTF_HINTING_NONE);
	face.factor = factor;
	face.ascent = TTF_FontAscent(face.font.get());
	if(TTF_GlyphMetrics32(face.font.get(), 'H', &minX, &maxX, &minY, &maxY, &advance) != 0)
	{
		// DMB: nothing half made stays cached: the test above took a face with its font set as ready, capital height 0
		face = ButtonFace();
		return nullptr;
	}
	face.capHeight = maxY;
	return &face;
}

std::string utf8(uint32_t c)
{
	std::string out;
	if(c < 0x80)
		out += static_cast<char>(c);
	else if(c < 0x800)
	{
		out += static_cast<char>(0xC0 | (c >> 6));
		out += static_cast<char>(0x80 | (c & 0x3F));
	}
	else
	{
		out += static_cast<char>(0xE0 | (c >> 12));
		out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (c & 0x3F));
	}
	return out;
}

/// the capital of a Unicode letter: the Latin, Latin-1 and Latin Extended-A letters the face has (it has no Cyrillic: a Cyrillic word is carved from the game's bitmap letters, or plain in the launcher)
uint32_t capital(uint32_t c)
{
	if(c >= 'a' && c <= 'z')
		return c - 32;
	if(c >= 0xE0 && c <= 0xFE && c != 0xF7)
		return c - 32;
	if(c == 0xFF)
		return 0x178;
	if(c >= 0x100 && c <= 0x17F && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x17F)
	{
		const bool oddIsLower = (c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E);
		const bool lower = oddIsLower ? (c % 2 == 0) : (c % 2 == 1);
		return lower ? c - 1 : c;
	}
	if(c >= 0x430 && c <= 0x44F)
		return c - 32;
	if(c >= 0x450 && c <= 0x45F)
		return c - 80;
	return c;
}
}

GameLettering::ButtonWords GameLettering::buttonWords(const std::string & text)
{
	ButtonWords result;
	const ButtonFace * face = buttonFace();
	if(!face)
		return result;
	// capitals, as the game's buttons write their words
	std::string capitals;
	const std::string trimmed = boost::algorithm::trim_copy(text);
	for(size_t at = 0; at < trimmed.size();)
	{
		const size_t length = TextOperations::getUnicodeCharacterSize(trimmed[at]);
		const uint32_t c = capital(TextOperations::getUnicodeCodepoint(trimmed.data() + at, trimmed.size() - at));
		at += std::max<size_t>(1, length);
		if(c == ' ' || (c > ' ' && TTF_GlyphIsProvided32(face->font.get(), c) != 0))
			capitals += utf8(c);
		else
			return result;
	}
	if(capitals.empty())
		return result;

	std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> rendered(TTF_RenderUTF8_Blended(face->font.get(), capitals.c_str(), SDL_Color{255, 255, 255, 255}), SDL_FreeSurface);
	if(!rendered)
		return result;
	std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> argb(SDL_ConvertSurfaceFormat(rendered.get(), SDL_PIXELFORMAT_ARGB8888, 0), SDL_FreeSurface);
	if(!argb)
		return result;
	const int w = argb->w;
	const int h = argb->h;
	std::vector<uint8_t> coverage(static_cast<size_t>(w) * h, 0);
	SDL_LockSurface(argb.get());
	for(int y = 0; y < h; ++y)
	{
		const auto * line = reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(argb->pixels) + static_cast<size_t>(y) * argb->pitch);
		for(int x = 0; x < w; ++x)
			coverage[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(line[x] >> 24);
	}
	SDL_UnlockSurface(argb.get());

	int inkLeft = w, inkRight = -1, inkTop = h, inkBottom = -1;
	for(int y = 0; y < h; ++y)
		for(int x = 0; x < w; ++x)
			if(coverage[static_cast<size_t>(y) * w + x] > 0)
			{
				inkLeft = std::min(inkLeft, x);
				inkRight = std::max(inkRight, x);
				inkTop = std::min(inkTop, y);
				inkBottom = std::max(inkBottom, y);
			}
	if(inkRight < 0)
		return result;

	// the carved light is a game pixel to the lower left of every letter; margins keep it inside the picture, and the picture starts a
	// whole game pixel above the capitals' top so the letters land on the game's own pixel grid
	const int f = face->factor;
	const int margin = 2 * f;
	const int capTop = face->ascent - face->capHeight;
	const int up = std::max(0, (capTop - inkTop + f - 1) / f * f);
	const int left = inkLeft - margin;
	const int top = capTop - up - margin;
	const int imageW = inkRight - inkLeft + 1 + 2 * margin;
	const int imageH = std::max(inkBottom, capTop + CAPITALS * f - 1) - top + 1 + margin;
	const auto at = [&](int x, int y) -> double
	{
		return x < 0 || y < 0 || x >= w || y >= h ? 0.0 : coverage[static_cast<size_t>(y) * w + x] / 255.0;
	};
	auto image = ENGINE->renderHandler().createImage(Point(imageW, imageH), CanvasScalingPolicy::IGNORE);
	Canvas canvas = image->getCanvas();
	for(int row = 0; row < imageH; ++row)
		for(int col = 0; col < imageW; ++col)
		{
			const double letter = at(left + col, top + row);
			const double light = at(left + col + f, top + row - f);
			const double opacity = letter + light * (1 - letter);
			if(opacity <= 0)
				continue;
			const double white = light * (1 - letter) / opacity;
			const auto value = static_cast<uint8_t>(std::lround(255.0 * white));
			canvas.drawColor(Rect(col, row, 1, 1), ColorRGBA(value, value, value, static_cast<uint8_t>(std::lround(opacity * 255))));
		}
	result.image = image;
	// the letters' black in game pixels: the words' width, and the capitals' height
	result.letters = Rect(margin / f, (margin + up) / f, (inkRight - inkLeft + f) / f, CAPITALS);
	return result;
}
