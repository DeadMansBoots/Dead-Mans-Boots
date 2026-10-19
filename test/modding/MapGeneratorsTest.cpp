/*
 * MapGeneratorsTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../../lib/modding/MapGenerators.h"

// DMB: the cmd.exe line of a .cmd or .bat generator
TEST(MapGenerators, batchLineQuotesWhatCmdWouldSplitOrRead)
{
	EXPECT_EQ(MapGenerators::batchCommandLine("C:\\Mods\\gen\\run.cmd", {"--template", "x&calc", "--vcmiroot", "D:\\Games\\R&D\\vcmi", "--teams", ""}),
		"cmd /c \"\"C:\\Mods\\gen\\run.cmd\" --template \"x&calc\" --vcmiroot \"D:\\Games\\R&D\\vcmi\" --teams \"\"\"");
	// plain tokens stay bare (a script's %1 is --w, as before); a drive root's final backslash is then not in front of a quote
	EXPECT_EQ(MapGenerators::batchCommandLine("run.cmd", {"--w", "64", "--vcmiroot", "E:\\", "a,b", "k=v", "x;y"}),
		"cmd /c \"\"run.cmd\" --w 64 --vcmiroot E:\\ \"a,b\" \"k=v\" \"x;y\"\"");
	EXPECT_EQ(MapGenerators::batchCommandLine("C:\\My Mods\\run.bat", {"a b", "(x)|^<y>"}),
		"cmd /c \"\"C:\\My Mods\\run.bat\" \"a b\" \"(x)|^<y>\"\"");
}

TEST(MapGenerators, batchLineRefusesWhatCmdCannotPass)
{
	EXPECT_THROW(MapGenerators::batchCommandLine("run.cmd", {"say \"hi\""}), std::runtime_error);
	EXPECT_THROW(MapGenerators::batchCommandLine("run.cmd", {"%PATH%"}), std::runtime_error);
	EXPECT_THROW(MapGenerators::batchCommandLine("run.cmd", {"a\nb"}), std::runtime_error);
	EXPECT_THROW(MapGenerators::batchCommandLine("C:\\Tools\\Ann%1\\run.cmd", {}), std::runtime_error);
}
