#pragma once
#include "GW2Client.h"
#include "ItemIndex.h"
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <unordered_map>
#include <json.hpp>

struct EntityCoord {
    std::string name;
    int mapId = 0;
    std::string mapName;
    double cx = 0, cy = 0;
    enum CoordSource { None, Exact, Sector } source = None;
    bool hasCoord = false;
    bool done = false;
    int bitIndex = -1;
    std::vector<std::string> areas;
};

struct MapRects {
    double contRect[4] = {};
    double mapRect[4] = {};
};

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
    void SetGw2ApiKey(const std::string& key) { m_gw2ApiKey = key; }

    using LogFunc = std::function<void(const std::string&)>;
    void SetLogger(LogFunc fn) { m_logger = std::move(fn); }

    FunctionResult Handle(const FunctionCall& call, CancelCheck shouldCancel = nullptr);

    static nlohmann::json GetToolDefinitions();

    static constexpr size_t WIKI_TEXT_BUDGET = 12 * 1024;
    static constexpr size_t WIKI_SUBPAGE_CAP = 5;
    static constexpr size_t GUIDE_TEXT_BUDGET = 12 * 1024;
    static constexpr size_t BUILD_TEXT_BUDGET = 12 * 1024;
    static constexpr size_t BUILD_FETCH_CAP = 5;
    static constexpr size_t LOCATION_MAP_CAP = 6;

private:
    std::string HandleItemInfo(const nlohmann::json& args, const CancelCheck& cancel);
    std::string HandleRecipe(const nlohmann::json& args, const CancelCheck& cancel);
    std::string HandleMap(const nlohmann::json& args, const CancelCheck& cancel);
    std::string HandleWiki(const nlohmann::json& args, const CancelCheck& cancel);
    std::string HandleGuide(const nlohmann::json& args, const CancelCheck& cancel);
    std::string HandleBuild(const nlohmann::json& args, const CancelCheck& cancel);
    std::string HandleAccountAchievement(const nlohmann::json& args, const CancelCheck& cancel);
    std::string HandleAccountWallet(const nlohmann::json& args, const CancelCheck& cancel);
    std::string HandleAccountInventory(const nlohmann::json& args, const CancelCheck& cancel);
    std::string HandleAccountCharacters(const nlohmann::json& args, const CancelCheck& cancel);
    std::string HandleAccountUnlocks(const nlohmann::json& args, const CancelCheck& cancel);

    int ResolveItemId(const std::string& name);
    int ResolveMapId(const std::string& name, int depth = 0);
    nlohmann::json ExtractCollectionItems(const std::string& html);

    static bool Cancelled(const CancelCheck& c) { return c && c(); }

    struct GuideCache {
        std::string title;
        std::string url;
        std::string modified;
        std::string text;   // HtmlToText + h1→h2 normalized
    };

    GW2Client* m_gw2;
    ItemIndex* m_index;
    std::unordered_map<int, GuideCache> m_guideCache;
    std::function<void(const std::string&)> m_logger;
    std::string m_gw2ApiKey;

    // Entity coord side-channel: written by HandleWiki (worker thread only), drained by Worker.
    std::vector<EntityCoord> m_entityCoords;
    std::map<int, MapRects> m_mapRects;

public:
    struct EntityData {
        std::vector<EntityCoord> coords;
        std::map<int, MapRects> rects;
    };
    EntityData TakeEntityData();
};
