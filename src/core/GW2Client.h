#pragma once
#include "HttpClient.h"
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <json.hpp>

struct GW2Item {
    int id = 0;
    std::string name;
    std::string type;
    std::string rarity;
    std::string chatLink;
    std::string description;
    int level = 0;
    int vendorValue = 0;
    bool found = false;
};

struct GW2Price {
    int itemId = 0;
    int buyPrice = 0;
    int buyQuantity = 0;
    int sellPrice = 0;
    int sellQuantity = 0;
    bool whitelisted = false;
    bool found = false;
};

struct GW2Ingredient {
    int itemId = 0;
    int count = 0;
};

struct GW2Recipe {
    int id = 0;
    int outputItemId = 0;
    int outputCount = 0;
    int minRating = 0;
    std::string type;
    std::string chatLink;
    std::vector<std::string> disciplines;
    std::vector<GW2Ingredient> ingredients;
    bool found = false;
};

struct GW2Waypoint {
    std::string name;
    std::string chatLink;
    int floor = 0;
    double x = 0;
    double y = 0;
    bool hasCoord = false;
};

struct GW2Sector {
    std::string name;
    double x = 0, y = 0;
};

struct GW2MapInfo {
    int id = 0;
    std::string name;
    int minLevel = 0;
    int maxLevel = 0;
    int regionId = 0;
    std::string regionName;
    int continentId = 0;
    std::string continentName;
    double contRect[2][2] = {{0, 0}, {0, 0}};
    bool hasContRect = false;
    double mapRect[2][2] = {{0, 0}, {0, 0}};
    bool hasMapRect = false;
    int defaultFloor = 1;
    std::vector<int> floors;
    int floorUsed = 0;
    std::vector<GW2Waypoint> waypoints;
    std::vector<GW2Sector> sectors;
    bool found = false;
};

struct WikiSearchResult {
    std::string title;
    std::string url;
    bool fulltext = false;   // true when found by full-text fallback, not by title prefix
};

struct WikiPage {
    std::string title;
    std::string wikitext;
    std::string html;
    bool found = false;
};

struct GuideSearchResult {
    int id = 0;
    std::string title;
    std::string selfHref;   // full REST URL for the resource (posts/N or pages/N)
};

struct GuidePage {
    std::string title;
    std::string url;
    std::string modified;   // ISO 8601 e.g. "2026-08-14T14:25:59"
    std::string html;       // content.rendered — article-only HTML
    bool found = false;
};

struct BuildSearchResult {
    std::string title;
    int pageId = 0;
    int size = 0;
    std::string timestamp;
};

class GW2Client {
public:
    GW2Item GetItem(int id);
    std::vector<GW2Item> GetItems(const std::vector<int>& ids);

    GW2Price GetPrice(int id);
    std::vector<GW2Price> GetPrices(const std::vector<int>& ids);

    GW2Recipe GetRecipe(int id);
    std::vector<int> SearchRecipesByOutput(int outputItemId);

    GW2MapInfo GetMap(int id);
    std::vector<GW2MapInfo> GetMaps(const std::vector<int>& ids);
    GW2MapInfo GetMapWithWaypoints(int mapId);

    std::vector<WikiSearchResult> WikiSearch(const std::string& query, int limit = 5);
    std::vector<WikiSearchResult> WikiSearchWithInteractiveMap(const std::string& query, int limit = 5);
    WikiPage WikiGetPage(const std::string& title);
    WikiPage WikiGetPageHtml(const std::string& title);

    std::vector<GuideSearchResult> GuideSearch(const std::string& query, int limit = 5);
    GuidePage GuideGetContent(const std::string& selfHref);

    std::vector<BuildSearchResult> BuildSearch(const std::string& query, int limit = 10);
    WikiPage BuildGetPage(const std::string& title);
    WikiPage BuildGetPageHtml(const std::string& title);

    static std::string FormatPrice(int copper);
    static std::string ExtractItemIdFromWikitext(const std::string& wikitext);

    static constexpr const char* API_HOST = "api.guildwars2.com";
    static constexpr const char* WIKI_HOST = "wiki.guildwars2.com";
    static constexpr const char* GUIDE_HOST = "guildjen.com";
    static constexpr const char* BUILD_HOST = "metabattle.com";

    // Receives "[HTTP] host/path -> WinHttp... code" when a GET gets no response at all.
    using LogFunc = std::function<void(const std::string&)>;
    void SetLogger(LogFunc fn) { m_logger = std::move(fn); }

private:
    std::string BuildIdList(const std::vector<int>& ids);
    std::string UrlEncode(const std::string& str);
    std::vector<WikiSearchResult> WikiFullTextSearch(const std::string& query, int limit);
    std::optional<HttpResponse> Fetch(const char* host, const std::string& path, int timeoutMs = 10000);

    HttpClient m_http;
    LogFunc m_logger;
};
