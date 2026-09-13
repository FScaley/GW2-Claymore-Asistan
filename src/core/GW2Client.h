#pragma once
#include "HttpClient.h"
#include <string>
#include <vector>
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
    std::vector<GW2Waypoint> waypoints;
    bool found = false;
};

struct WikiSearchResult {
    std::string title;
    std::string url;
};

struct WikiPage {
    std::string title;
    std::string wikitext;
    bool found = false;
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
    GW2MapInfo GetMapWithWaypoints(int mapId);

    std::vector<WikiSearchResult> WikiSearch(const std::string& query, int limit = 5);
    WikiPage WikiGetPage(const std::string& title);

    static std::string FormatPrice(int copper);
    static std::string ExtractItemIdFromWikitext(const std::string& wikitext);

    static constexpr const char* API_HOST = "api.guildwars2.com";
    static constexpr const char* WIKI_HOST = "wiki.guildwars2.com";

private:
    std::string BuildIdList(const std::vector<int>& ids);
    std::string UrlEncode(const std::string& str);

    HttpClient m_http;
};
