#pragma once
#include "GW2Client.h"
#include "ItemIndex.h"
#include <string>
#include <json.hpp>

struct FunctionCall {
    std::string id;
    std::string name;
    nlohmann::json arguments;
};

struct FunctionResult {
    std::string callId;
    std::string name;
    std::string resultText;
};

class FunctionHandler {
public:
    FunctionHandler(GW2Client* gw2, ItemIndex* index);

    FunctionResult Handle(const FunctionCall& call);

    static nlohmann::json GetToolDefinitions();

private:
    std::string HandleItemInfo(const nlohmann::json& args);
    std::string HandleRecipe(const nlohmann::json& args);
    std::string HandleMap(const nlohmann::json& args);
    std::string HandleWiki(const nlohmann::json& args);

    int ResolveItemId(const std::string& name);
    int ResolveMapId(const std::string& name);
    std::string TruncateWikitext(const std::string& wikitext, size_t maxBytes = 4096);

    GW2Client* m_gw2;
    ItemIndex* m_index;
};
