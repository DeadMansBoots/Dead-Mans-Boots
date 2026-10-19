/*
 * TimerStringTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../../client/lobby/TimerString.h"

// DMB: the lobby's timer fields, as the player edits them
TEST(TimerString, readsTheTimesThePlayerMeans)
{
	EXPECT_EQ(TimerString::parse(""), 0);
	EXPECT_EQ(TimerString::parse("90"), 90);
	EXPECT_EQ(TimerString::parse("2:00"), 120);
	EXPECT_EQ(TimerString::parse("12:30"), 750);
	EXPECT_EQ(TimerString::parse("2:005"), 20 * 60 + 5);
	EXPECT_EQ(TimerString::parse("2:305"), 23 * 60 + 5);
	EXPECT_EQ(TimerString::parse("12:095"), 1209 * 60 + 5);
	EXPECT_EQ(TimerString::parse("129:0"), 129 * 60);
	EXPECT_EQ(TimerString::parse("3000:00"), 24 * 60 * 60);
	EXPECT_EQ(TimerString::parse("2:0"), 20);
	EXPECT_EQ(TimerString::parse("0:2"), 2);
	EXPECT_EQ(TimerString::parse(":05"), 5);
	EXPECT_EQ(TimerString::parse(":90"), 9);
	EXPECT_EQ(TimerString::parse("5:"), 300);
}

TEST(TimerString, refusesWhatIsNotATime)
{
	// the minutes deleted with one digit of the seconds: no minute digit to borrow
	EXPECT_EQ(TimerString::parse(":5"), 5);
	EXPECT_EQ(TimerString::parse("a"), std::nullopt);
	EXPECT_EQ(TimerString::parse("1:x0"), std::nullopt);
	EXPECT_EQ(TimerString::parse("x:00"), std::nullopt);
	EXPECT_EQ(TimerString::parse("-5"), std::nullopt);
	EXPECT_EQ(TimerString::parse("1:-5"), std::nullopt);
	EXPECT_EQ(TimerString::parse("1:2345"), std::nullopt);
	EXPECT_EQ(TimerString::parse("99999999999"), std::nullopt);
	EXPECT_EQ(TimerString::parse("99999999999:00"), std::nullopt);
	EXPECT_EQ(TimerString::parse("5000000"), std::nullopt);
	EXPECT_EQ(TimerString::parse(std::to_string(TimerString::MAXIMUM)), TimerString::MAXIMUM);
	EXPECT_EQ(TimerString::parse(std::to_string(TimerString::MAXIMUM + 1)), std::nullopt);
}
