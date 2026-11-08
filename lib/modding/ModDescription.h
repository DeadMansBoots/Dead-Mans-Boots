/*
 * ModDescription.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

VCMI_LIB_NAMESPACE_BEGIN

struct CModVersion;
struct ModVerificationInfo;
class JsonNode;

using TModID = std::string;
using TModList = std::vector<TModID>;
using TModSet = std::set<TModID>;

class DLL_LINKAGE ModDescription : boost::noncopyable
{
	TModID identifier;

	std::unique_ptr<JsonNode> localConfig;
	std::unique_ptr<JsonNode> repositoryConfig;

	TModSet dependencies;
	TModSet softDependencies;
	TModSet conflicts;

	TModSet loadModList(const JsonNode & configNode) const;

public:
	ModDescription(const TModID & fullID, const JsonNode & localConfig, const JsonNode & repositoryConfig);
	~ModDescription();

	const TModID & getID() const;
	TModID getParentID() const;
	TModID getTopParentID() const;

	const TModSet & getDependencies() const;
	const TModSet & getSoftDependencies() const;
	const TModSet & getConflicts() const;

	const std::string & getBaseLanguage() const;
	const std::string & getName() const;

	const JsonNode & getFilesystemConfig() const;
	const JsonNode & getLocalConfig() const;
	const JsonNode & getValue(const std::string & keyName) const;
	const JsonNode & getLocalizedValue(const std::string & keyName) const;
	const JsonNode & getLocalizedDescription() const;
	const JsonNode & getLocalValue(const std::string & keyName) const;
	const JsonNode & getRepositoryValue(const std::string & keyName) const;

	CModVersion getVersion() const;
	ModVerificationInfo getVerificationInfo() const;

	bool isCompatible() const;
	bool isUpdateAvailable() const;
	/// DMB (S084): whether this engine can run the mod a mod.json describes: its "dmb" "api" and its "compatibility" range (the one rule the game, the launcher and the archive check use)
	static bool engineAllows(const JsonNode & modConfig);
	/// DMB (S084): the newest version the repository lists (its mod.json) can run on this engine
	bool isRepositoryCompatible() const;
	/// DMB (S084): the repository has a newer version, but it cannot run on this engine; it is not offered and the installed copy stays
	bool isUpdateBlocked() const;

	bool affectsGameplay() const;
	bool isCompatibility() const;
	bool isTranslation() const;
	bool keepDisabled() const;
	bool isInstalled() const;

	static void mergeModDescriptions(JsonNode & config, const std::string & fullDescription);
};

VCMI_LIB_NAMESPACE_END
