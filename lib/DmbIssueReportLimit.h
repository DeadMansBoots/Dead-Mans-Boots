/*
 * DmbIssueReportLimit.h, part of Dead Man's Boots
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include <algorithm>
#include <chrono>
#include <map>
#include <string>
#include <vector>

/// The rate limit of DmbIssueReport, with no dependency on the game so that a test can drive it with a clock of its own.
/// Each kind has its own budget: at least `kindSpacing` between two reports of a kind and at most `perKindPerHour` of them in an hour, so a kind that keeps
/// happening (a slow PC's slow-frame) cannot use up what another kind (a lost connection) needs. A second, larger cap over all kinds stops a process
/// that is failing in many ways at once from sending a flood.
class DmbIssueReportLimit
{
public:
	using Clock = std::chrono::steady_clock;

	DmbIssueReportLimit(std::chrono::minutes kindSpacing = std::chrono::minutes(10), size_t perKindPerHour = 3, size_t allKindsPerHour = 20)
		: kindSpacing(kindSpacing)
		, perKindPerHour(perKindPerHour)
		, allKindsPerHour(allKindsPerHour)
	{
	}

	/// True when a report of this kind may go out at `now`; the report is then counted. Not thread safe: the caller locks.
	bool allow(const std::string & kind, Clock::time_point now)
	{
		const auto hour = std::chrono::hours(1);
		auto & own = byKind[kind];
		const auto expired = [&](Clock::time_point t) { return now - t >= hour; };
		own.erase(std::remove_if(own.begin(), own.end(), expired), own.end());
		all.erase(std::remove_if(all.begin(), all.end(), expired), all.end());

		if(!own.empty() && now - own.back() < kindSpacing)
			return false;
		if(own.size() >= perKindPerHour || all.size() >= allKindsPerHour)
			return false;
		own.push_back(now);
		all.push_back(now);
		return true;
	}

private:
	std::chrono::minutes kindSpacing;
	size_t perKindPerHour;
	size_t allKindsPerHour;
	std::map<std::string, std::vector<Clock::time_point>> byKind;
	std::vector<Clock::time_point> all;
};
