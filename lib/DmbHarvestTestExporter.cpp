// TestExporter.cpp: the 93.8 proof exporter against DMB's lib/DmbHarvest.h (OmniAI Dev, 2026-11-10). For the proof only, never released:
// put this file in lib/ and add it to lib/CMakeLists.txt. It registers itself (DMB_HARVEST_EXPORTER at the end); nothing in the framework
// changes. It counts each topic and each seat's requests, and writes test.counts-summary.txt at game end.
#include "StdInc.h"
#include "DmbHarvest.h"

VCMI_LIB_NAMESPACE_BEGIN

class TestExporter final : public HarvestExporter
{
	std::map<std::string, int> topics;
	std::map<std::string, int> requests;

public:
	std::string id() const override { return "test.counts"; }
	std::string name() const override { return "Test: counts"; }
	std::string description() const override { return "Counts each kind of event and each seat's orders; a proof that an exporter needs no framework change."; }
	bool onByDefault() const override { return false; }
	bool wants(const std::string & topic) const override { return true; }

	void gameStarted(HarvestGame & game) override { topics.clear(); requests.clear(); }
	void eventHappened(const HarvestEvent & event, HarvestGame & game) override
	{
		topics[event.topic]++;
		const auto at = event.body.find("\"player\":\"");
		if(event.topic == "request" && at != std::string::npos)
			requests[event.body.substr(at + 10, event.body.find('"', at + 10) - at - 10)]++;
	}
	void gameEnded(const std::string & why, HarvestGame & game) override
	{
		std::string text = "ended: " + why + "\n";
		for(const auto & t : topics)
			text += "topic " + t.first + " " + std::to_string(t.second) + "\n";
		for(const auto & r : requests)
			text += "requests " + r.first + " " + std::to_string(r.second) + "\n";
		game.appendFile(id(), "summary.txt", text);
	}
};

DMB_HARVEST_EXPORTER(TestExporter)

VCMI_LIB_NAMESPACE_END
