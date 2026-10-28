/*
 * DmbChatRouting.h, part of Dead Man's Boots
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

/// DMB (the project owner, October 21st: 'We also need a "team" chat mode like a lot of other modern retro competitive strategy games'): the channel of a chat line.
enum class EChatChannel : int8_t
{
	ALL = 0, // every player and watcher, as before
	TEAM = 1, // the sender's team only
	SPECTATORS = 2 // the watchers (connected clients with no slot), and the sender
};

/// A connection, as the chat router sees it: the team of each player colour the connection controls (-1: a player without a team). A connection that controls no colour has
/// no slot, which makes it a spectator (the project owner: a player connected to the lobby and not assigned a slot is a spectator).
struct DmbChatConnection
{
	int id = 0;
	std::vector<int> playerTeams;

	bool isSpectator() const
	{
		return playerTeams.empty();
	}
};

struct DmbChatRoute
{
	/// the channel the line goes out on: what the sender asked for, except that a spectator's TEAM is SPECTATORS (a spectator has no team)
	EChatChannel channel = EChatChannel::ALL;
	std::vector<int> recipients; // connection ids, in the order of the list given, each once
};

/// Who receives a chat line. A pure function of plain values, decided on the server and never by what a client claims: the sender's connection always gets its own line
/// back, so a line to an empty team or to no watchers does not vanish. `senderTeam` is the team of the sender's colour, -1 when the sender is a spectator or has none.
inline DmbChatRoute dmbChatRoute(const std::vector<DmbChatConnection> & connections, int senderConnection, bool senderIsSpectator, int senderTeam, EChatChannel requested)
{
	DmbChatRoute route;
	route.channel = requested;
	if(senderIsSpectator && requested == EChatChannel::TEAM)
		route.channel = EChatChannel::SPECTATORS;

	for(const auto & connection : connections)
	{
		bool receives = connection.id == senderConnection;
		switch(route.channel)
		{
			case EChatChannel::ALL:
				receives = true;
				break;
			case EChatChannel::TEAM:
				receives = receives || (senderTeam >= 0 && std::find(connection.playerTeams.begin(), connection.playerTeams.end(), senderTeam) != connection.playerTeams.end());
				break;
			case EChatChannel::SPECTATORS:
				receives = receives || connection.isSpectator();
				break;
		}
		if(receives && std::find(route.recipients.begin(), route.recipients.end(), connection.id) == route.recipients.end())
			route.recipients.push_back(connection.id);
	}
	return route;
}
