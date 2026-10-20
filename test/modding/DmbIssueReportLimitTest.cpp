/*
 * DmbIssueReportLimitTest.cpp, part of Dead Man's Boots
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"

#include "../../lib/DmbIssueReportLimit.h"

namespace
{
using Clock = DmbIssueReportLimit::Clock;
const Clock::time_point start = Clock::time_point(std::chrono::hours(100));
}

// a kind is held to 10 minutes between its reports
TEST(DmbIssueReportLimit, sameKindIsSpaced)
{
	DmbIssueReportLimit limit;
	EXPECT_TRUE(limit.allow("slow-frame", start));
	EXPECT_FALSE(limit.allow("slow-frame", start + std::chrono::minutes(9)));
	EXPECT_TRUE(limit.allow("slow-frame", start + std::chrono::minutes(10)));
}

// a kind that keeps happening runs out of its own budget (three an hour) and no other kind's
TEST(DmbIssueReportLimit, chattyKindCannotUseUpAnotherKindsBudget)
{
	DmbIssueReportLimit limit;
	int sent = 0;
	for(int i = 0; i < 6; ++i)
		sent += limit.allow("slow-frame", start + std::chrono::minutes(10 * i)) ? 1 : 0;
	EXPECT_EQ(sent, 3); // minutes 0, 10, 20 inside the hour; minutes 30 to 50 refused
	// the connection-lost report that comes after still goes out
	EXPECT_TRUE(limit.allow("connection-lost", start + std::chrono::minutes(55)));
	EXPECT_TRUE(limit.allow("request-refused", start + std::chrono::minutes(55)));
}

// the budget refills as the hour passes
TEST(DmbIssueReportLimit, budgetRefillsAfterAnHour)
{
	DmbIssueReportLimit limit;
	EXPECT_TRUE(limit.allow("a", start));
	EXPECT_TRUE(limit.allow("a", start + std::chrono::minutes(10)));
	EXPECT_TRUE(limit.allow("a", start + std::chrono::minutes(20)));
	EXPECT_FALSE(limit.allow("a", start + std::chrono::minutes(59)));
	EXPECT_TRUE(limit.allow("a", start + std::chrono::minutes(61))); // the first one is over an hour old
}

// many kinds at once are held to the overall cap
TEST(DmbIssueReportLimit, allKindsShareALargerCap)
{
	DmbIssueReportLimit limit(std::chrono::minutes(10), 3, 4);
	int sent = 0;
	for(int i = 0; i < 10; ++i)
		sent += limit.allow("kind" + std::to_string(i), start) ? 1 : 0;
	EXPECT_EQ(sent, 4);
}
