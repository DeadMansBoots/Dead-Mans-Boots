/*
 * DmbChatRoutingTest.cpp, part of Dead Man's Boots
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"

#include "../../lib/DmbChatRouting.h"

namespace
{
// connection 1: red (team 0) and an AI (team 0), connection 2: blue (team 0), connection 3: green (team 1), connection 4: hot seat with orange (team 1) and purple (team 2),
// connection 5: no slot (a spectator), connection 6: no slot (a spectator)
std::vector<DmbChatConnection> sample()
{
	return {{1, {0, 0}}, {2, {0}}, {3, {1}}, {4, {1, 2}}, {5, {}}, {6, {}}};
}
}

// everyone gets an ALL line, watchers included
TEST(DmbChatRouting, allReachesEveryConnection)
{
	const auto route = dmbChatRoute(sample(), 2, false, 0, EChatChannel::ALL);
	EXPECT_EQ(route.channel, EChatChannel::ALL);
	EXPECT_EQ(route.recipients, (std::vector<int>{1, 2, 3, 4, 5, 6}));
}

// a team line reaches the connections that control a colour of the team, and the sender's own, and no watcher
TEST(DmbChatRouting, teamReachesOnlyTeammates)
{
	const auto route = dmbChatRoute(sample(), 2, false, 0, EChatChannel::TEAM);
	EXPECT_EQ(route.channel, EChatChannel::TEAM);
	EXPECT_EQ(route.recipients, (std::vector<int>{1, 2}));
}

// a hot seat connection receives when any of its colours is on the team
TEST(DmbChatRouting, hotSeatConnectionReceivesForAnyColour)
{
	const auto route = dmbChatRoute(sample(), 3, false, 1, EChatChannel::TEAM);
	EXPECT_EQ(route.recipients, (std::vector<int>{3, 4}));
	const auto other = dmbChatRoute(sample(), 3, false, 2, EChatChannel::TEAM);
	EXPECT_EQ(other.recipients, (std::vector<int>{3, 4}));
}

// a player alone on a team still gets the line back
TEST(DmbChatRouting, lonePlayerGetsOwnTeamLine)
{
	const std::vector<DmbChatConnection> connections{{1, {0}}, {2, {1}}};
	const auto route = dmbChatRoute(connections, 1, false, 0, EChatChannel::TEAM);
	EXPECT_EQ(route.recipients, (std::vector<int>{1}));
}

// a team line from a colour with no team reaches the sender only
TEST(DmbChatRouting, noTeamMeansOnlyTheSender)
{
	const auto route = dmbChatRoute(sample(), 2, false, -1, EChatChannel::TEAM);
	EXPECT_EQ(route.recipients, (std::vector<int>{2}));
}

// a line to the spectators reaches the connections with no slot and the sender, never the other players
TEST(DmbChatRouting, spectatorLineReachesWatchersAndSender)
{
	const auto route = dmbChatRoute(sample(), 2, false, 0, EChatChannel::SPECTATORS);
	EXPECT_EQ(route.channel, EChatChannel::SPECTATORS);
	EXPECT_EQ(route.recipients, (std::vector<int>{2, 5, 6}));
}

// a spectator has no team: its TEAM line becomes a line to the spectators, decided by the server
TEST(DmbChatRouting, spectatorTeamLineBecomesSpectatorLine)
{
	const auto route = dmbChatRoute(sample(), 5, true, -1, EChatChannel::TEAM);
	EXPECT_EQ(route.channel, EChatChannel::SPECTATORS);
	EXPECT_EQ(route.recipients, (std::vector<int>{5, 6}));
}

// a spectator may write to everyone
TEST(DmbChatRouting, spectatorMayWriteToAll)
{
	const auto route = dmbChatRoute(sample(), 5, true, -1, EChatChannel::ALL);
	EXPECT_EQ(route.recipients, (std::vector<int>{1, 2, 3, 4, 5, 6}));
}

// a sender whose connection is not in the list still reaches the others of its channel and nothing is duplicated
TEST(DmbChatRouting, eachRecipientOnce)
{
	const auto route = dmbChatRoute(sample(), 9, false, 0, EChatChannel::TEAM);
	EXPECT_EQ(route.recipients, (std::vector<int>{1, 2}));
}
