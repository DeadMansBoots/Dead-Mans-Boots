/*
 * TimerString.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <charconv>
#include <optional>
#include <string>

namespace TimerString
{
/// the most a timer field may hold, in seconds: 24 hours of minutes and the seconds after them
constexpr int MAXIMUM = 24 * 60 * 60 + 59;

/// digits only, and few enough that the value fits an int; nothing for anything else
inline std::optional<int> digits(const std::string & text)
{
	if(text.empty() || text.size() > 9)
		return std::nullopt;
	unsigned value = 0; // unsigned: from_chars takes no sign then
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
	if(error != std::errc() || end != text.data() + text.size())
		return std::nullopt;
	return static_cast<int>(value);
}

/// DMB: a timer field's text in seconds, as the player meant it while editing; nothing when the text is not a time,
/// so the field shows the timer's current value again and nothing is sent
/// normal: 2:00, 12:30
/// adding symbol: 2:005 -> 20:05, 2:305 -> 23:05,
/// adding symbol (>60 seconds): 12:095 -> 1209:05
/// removing symbol: 129:0 -> 129:00 (the 90 seconds carry a digit back), 2:0 -> 0:20, 0:2 -> 0:02
inline std::optional<int> parse(const std::string & str)
{
	auto sc = str.find(':');
	if(sc == std::string::npos)
	{
		if(str.empty())
			return 0;
		const auto seconds = digits(str);
		if(!seconds || *seconds > MAXIMUM)
			return std::nullopt;
		return seconds;
	}

	auto l = str.substr(0, sc);
	auto r = str.substr(sc + 1, std::string::npos);
	if(r.length() > 3)
		return std::nullopt;
	if(r.length() == 3) //symbol added
	{
		l.push_back(r.front());
		r.erase(r.begin());
	}
	else if(r.length() == 1 && !l.empty()) //symbol removed
	{
		r.insert(r.begin(), l.back());
		l.pop_back();
	}
	else if(r.empty())
		r = "0";

	const auto sec = digits(r);
	if(!sec)
		return std::nullopt;
	if(*sec >= 60)
	{
		if(l.empty()) //9:00 -> 0:09
			return *sec / 10;

		l.push_back(r.front()); //0:090 -> 9:00
		r.erase(r.begin());
	}
	else if(l.empty())
		return sec;

	const auto minutes = digits(l);
	const auto seconds = digits(r);
	if(!minutes || !seconds)
		return std::nullopt;
	return std::min(24 * 60, *minutes) * 60 + *seconds;
}
}
