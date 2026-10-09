/*
 * PlayRecorder.h, part of VCMI engine (Dead Man's Boots)
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../lib/json/JsonNode.h"
#include "../lib/constants/EntityIdentifiers.h"

VCMI_LIB_NAMESPACE_BEGIN
struct CPackForServer;
struct CPackForClient;
VCMI_LIB_NAMESPACE_END

/// The play recorder (K, October 8th 2026: "if i do something in game, you all should know about it, even if its changing a system setting
/// or moving a item into a slot, then out of it"). One JSON line per event in
/// Documents\My Games\Dead Man's Boots\PlayLogs\<date>_<time>_<map>.jsonl: every request the player's client sends (moves, builds, recruits,
/// stack and artifact moves, trades, spells, battle commands, end of turn), what the game answered (the packs applied: fights with both
/// armies and losses, objects visited, resources, levels), windows opened and closed, settings changed with old and new value, and a snapshot
/// of the player's seat at the start of each of their turns. Never changes the game: every call is wrapped, a failed write is dropped.
/// Bounded: one file stops at 100 MB, and only the newest 40 files (1 GB together) are kept.
class PlayRecorder
{
public:
	static PlayRecorder & get();

	/// Opens this game's file (the first call of a game; later calls do nothing until stopGame)
	void startGame(PlayerColor seat);
	void stopGame(const std::string & why);
	bool isOpen() const;

	/// A request the player's client sends to the server (called before it is sent)
	void request(const CPackForServer & pack, PlayerColor player);
	/// A pack from the server, before the client applies it
	void clientPack(CPackForClient & pack);
	void windowOpened(const std::string & name);
	void windowClosed(const std::string & name);
	void settingChanged(const std::string & file, const std::vector<std::string> & path, const JsonNode & before, const JsonNode & after);
	/// The player's seat as OmniAI's TRUTH lines describe a seat (resources, income, towns, buildings, heroes, army, experience), plus heroes in full
	void snapshot(PlayerColor seat, const std::string & reason);
	/// Any other event: kind and a JSON object body without braces (key/value pairs), e.g. R"("what":"selected hero")"
	void event(const std::string & kind, const std::string & body);

private:
	PlayRecorder() = default;
	void write(const std::string & kind, const std::string & body);
	void pruneOldFiles(const std::string & folder);

	struct Impl;
	std::shared_ptr<Impl> impl;
};
