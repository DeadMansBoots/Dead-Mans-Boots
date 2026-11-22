/*
 * MetalTint.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "json/JsonNode.h"
#include "filesystem/ResourcePath.h"

#include <array>
#include <map>
#include <string>

/// DMB (S382, K, November 19th: "do all the metal buttons in DMB in this grain style, but remember it needs to handle any color the user wants
/// with the same affects"): one colour rule for the metal of every DMB button, the launcher's and the game's. A pixel of art keeps its lightness
/// (so the bevel, the shading and BEGIN's grain stay) and takes the colour: a hero colour through the game's own range for that player
/// (gameConfig.json settings.interface.playerColoredBackground, the numbers the game stretches its brown leather with), softened a tenth of the
/// way back to grey; silver and black as greys. "gold" (and anything unknown) is the art as it is. The launcher's tintPixel was this code.
namespace MetalTint
{
	/// the colour names a metal can take
	inline const std::array<std::string, 11> & names()
	{
		static const std::array<std::string, 11> list = {"gold", "red", "blue", "tan", "green", "orange", "purple", "teal", "pink", "silver", "black"};
		return list;
	}

	using ColourRange = std::array<float, 6>; // min r, g, b, max r, g, b

	/// the game's player colours: from its own config when it can be read, else the numbers it ships with
	inline const std::map<std::string, ColourRange> & heroRanges()
	{
		static const std::map<std::string, ColourRange> ranges = []()
		{
			std::map<std::string, ColourRange> shipped = {
				{"red",    {0.25f, 0.00f, 0.00f, 1.25f, 0.00f, 0.00f}},
				{"blue",   {0.00f, 0.00f, 0.00f, 0.45f, 1.20f, 4.50f}},
				{"tan",    {0.40f, 0.27f, 0.23f, 1.10f, 1.20f, 1.15f}},
				{"green",  {-0.27f, 0.10f, -0.27f, 0.70f, 1.70f, 0.70f}},
				{"orange", {0.47f, 0.17f, -0.27f, 1.60f, 1.20f, 0.70f}},
				{"purple", {0.12f, -0.10f, 0.25f, 1.15f, 1.20f, 2.20f}},
				{"teal",   {-0.13f, 0.23f, 0.23f, 0.90f, 1.20f, 2.20f}},
				{"pink",   {0.44f, 0.15f, 0.25f, 1.00f, 1.00f, 1.75f}},
			};
			try
			{
				const JsonNode config(JsonPath::builtin("config/gameConfig.json"));
				const JsonNode & filters = config["settings"]["interface"]["playerColoredBackground"];
				for(auto & [name, range] : shipped)
				{
					const auto & numbers = filters[name].Vector();
					if(numbers.size() == range.size())
						for(size_t i = 0; i < range.size(); ++i)
							range[i] = static_cast<float>(numbers[i].Float());
				}
			}
			catch(const std::exception &)
			{
				// the numbers above stand
			}
			return shipped;
		}();
		return ranges;
	}

	/// whether `colour` changes the art (gold and unknown names leave it as it is)
	inline bool tints(const std::string & colour)
	{
		return colour == "silver" || colour == "black" || heroRanges().count(colour) != 0;
	}

	/// one pixel of art of `lightness` (0 to 255) as `colour`, r, g, b; `weight` darkens (the launcher's dark look)
	inline std::array<int, 3> pixel(int lightness, const std::string & colour, double weight)
	{
		const double l = std::clamp(lightness, 0, 255) / 255.0;
		if(colour == "silver" || colour == "black")
		{
			// the cool tint is added after the weight, so a lifted silver does not turn blue
			const double scale = colour == "silver" ? 0.86 : 0.30;
			const std::array<double, 3> tint = colour == "silver" ? std::array<double, 3>{0, 3, 8} : std::array<double, 3>{0, 0, 2};
			std::array<int, 3> out;
			for(int c = 0; c < 3; ++c)
				out[c] = std::clamp(static_cast<int>(std::lround((l * scale * weight + tint[c] / 255.0) * 255.0)), 0, 255);
			return out;
		}
		const auto found = heroRanges().find(colour);
		if(found == heroRanges().end())
			return {lightness, lightness, lightness};
		const ColourRange & range = found->second;
		// the lightest art (a button's highlight) a little below the colour's own top, or a pale colour such as tan would turn it white;
		// softened only a tenth of the way back to grey, so the colour reads
		const double lit = l * 0.85;
		std::array<int, 3> out;
		for(int c = 0; c < 3; ++c)
		{
			const double v = (lit * (range[3 + c] - range[c]) + range[c]) * 0.9 + lit * 0.1;
			out[c] = std::clamp(static_cast<int>(std::lround(v * weight * 255.0)), 0, 255);
		}
		return out;
	}
}
