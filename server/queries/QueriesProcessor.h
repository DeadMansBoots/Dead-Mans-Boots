/*
 * QueriesProcessor.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../lib/GameConstants.h"

class CQuery;
using QueryPtr = std::shared_ptr<CQuery>;

class QueriesProcessor
{
private:
	void addQuery(PlayerColor player, QueryPtr query);
	void popQuery(PlayerColor player, QueryPtr query);

	std::map<PlayerColor, std::vector<QueryPtr>> queries; //player => stack of queries

public:
	void addQuery(QueryPtr query);
	void popQuery(const CQuery &query);
	void popQuery(QueryPtr query);
	void popIfTop(const CQuery &query); //removes this query if it is at the top (otherwise, do nothing)
	void popIfTop(QueryPtr query); //removes this query if it is at the top (otherwise, do nothing)

	QueryPtr topQuery(PlayerColor player);

	std::vector<std::shared_ptr<const CQuery>> allQueries() const;
	std::vector<QueryPtr> allQueries();
	QueryPtr getQuery(QueryID queryID);
	/// DMB: takes a query out of a player's stack wherever it is in it (popQuery takes it only from the top)
	void removeQuery(PlayerColor player, QueryPtr query);
	//void removeQuery
};
