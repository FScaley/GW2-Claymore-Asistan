#pragma once
#include "GW2Client.h"
#include "ItemIndex.h"
#include <string>
#include <functional>
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
    using CancelCheck = std::function<bool()>;

    FunctionHandler(GW2Client* gw2, ItemIndex* index);

    FunctionResult Handle(const FunctionCall& call, CancelCheck shouldCancel = nullptr);

    static nlohmann::json GetToolDefinitions();

    static constexpr size_t WIKI_TEXT_BUDGET = 12 * 1024;
    static constexpr size_t WIKI_SUBPAGE_CAP = 5;

private:
    std::string HandleItemInfo(const nlohmann::json& args, const CancelCheck& cancel);
    std::string HandleRecipe(const nlohmann::json& args, const CancelCheck& cancel);
    std::string HandleMap(const nlohmann::json& args, const CancelCheck& cancel);
    std::string HandleWiki(const nlohmann::json& args, const CancelCheck& cancel);

    int ResolveItemId(const std::string& name);
    int ResolveMapId(const std::string& name);
    nlohmann::json ExtractCollectionItems(const std::string& html);

    static bool Cancelled(const CancelCheck& c) { return c && c(); }

    GW2Client* m_gw2;
    ItemIndex* m_index;
};
