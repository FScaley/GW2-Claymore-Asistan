#include "GW2Client.h"
#include <sstream>
#include <algorithm>
#include <cctype>

using json = nlohmann::json;

std::string GW2Client::BuildIdList(const std::vector<int>& ids) {
    std::string result;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i > 0) result += ",";
        result += std::to_string(ids[i]);
    }
    return result;
}

std::string GW2Client::UrlEncode(const std::string& str) {
    std::string result;
    for (unsigned char c : str) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            result += c;
        else if (c == ' ')
            result += '+';
        else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            result += buf;
        }
    }
    return result;
}

std::string GW2Client::FormatPrice(int copper) {
    if (copper <= 0) return "0c";
    int gold = copper / 10000;
    int silver = (copper % 10000) / 100;
    int cop = copper % 100;
    std::string result;
    if (gold > 0) result += std::to_string(gold) + "g ";
    if (silver > 0 || gold > 0) result += std::to_string(silver) + "s ";
    result += std::to_string(cop) + "c";
    return result;
}

std::string GW2Client::ExtractItemIdFromWikitext(const std::string& wikitext) {
    auto pos = wikitext.find("| id");
    if (pos == std::string::npos) pos = wikitext.find("|id");
    if (pos == std::string::npos) return "";

    auto eq = wikitext.find('=', pos);
    if (eq == std::string::npos) return "";

    size_t start = eq + 1;
    while (start < wikitext.size() && (wikitext[start] == ' ' || wikitext[start] == '\t'))
        ++start;

    std::string num;
    while (start < wikitext.size() && std::isdigit(wikitext[start]))
        num += wikitext[start++];

    return num;
}

GW2Item GW2Client::GetItem(int id) {
    auto items = GetItems({id});
    return items.empty() ? GW2Item{} : items[0];
}

std::vector<GW2Item> GW2Client::GetItems(const std::vector<int>& ids) {
    std::vector<GW2Item> result;
    if (ids.empty()) return result;

    std::string path = "/v2/items?ids=" + BuildIdList(ids);
    auto resp = m_http.Get(API_HOST, path);
    if (!resp || resp->statusCode != 200) return result;

    try {
        auto arr = json::parse(resp->body);
        for (auto& j : arr) {
            GW2Item item;
            item.found = true;
            item.id = j.value("id", 0);
            item.name = j.value("name", "");
            item.type = j.value("type", "");
            item.rarity = j.value("rarity", "");
            item.chatLink = j.value("chat_link", "");
            item.description = j.value("description", "");
            item.level = j.value("level", 0);
            item.vendorValue = j.value("vendor_value", 0);
            result.push_back(std::move(item));
        }
    } catch (...) {}

    return result;
}

GW2Price GW2Client::GetPrice(int id) {
    auto prices = GetPrices({id});
    return prices.empty() ? GW2Price{} : prices[0];
}

std::vector<GW2Price> GW2Client::GetPrices(const std::vector<int>& ids) {
    std::vector<GW2Price> result;
    if (ids.empty()) return result;

    std::string path = "/v2/commerce/prices?ids=" + BuildIdList(ids);
    auto resp = m_http.Get(API_HOST, path);
    if (!resp || resp->statusCode != 200) return result;

    try {
        auto arr = json::parse(resp->body);
        for (auto& j : arr) {
            GW2Price p;
            p.found = true;
            p.itemId = j.value("id", 0);
            p.whitelisted = j.value("whitelisted", false);
            if (j.contains("buys")) {
                p.buyPrice = j["buys"].value("unit_price", 0);
                p.buyQuantity = j["buys"].value("quantity", 0);
            }
            if (j.contains("sells")) {
                p.sellPrice = j["sells"].value("unit_price", 0);
                p.sellQuantity = j["sells"].value("quantity", 0);
            }
            result.push_back(p);
        }
    } catch (...) {}

    return result;
}

GW2Recipe GW2Client::GetRecipe(int id) {
    std::string path = "/v2/recipes?ids=" + std::to_string(id);
    auto resp = m_http.Get(API_HOST, path);
    if (!resp || resp->statusCode != 200) return {};

    try {
        auto arr = json::parse(resp->body);
        if (arr.empty()) return {};
        auto& j = arr[0];

        GW2Recipe r;
        r.found = true;
        r.id = j.value("id", 0);
        r.outputItemId = j.value("output_item_id", 0);
        r.outputCount = j.value("output_item_count", 1);
        r.minRating = j.value("min_rating", 0);
        r.type = j.value("type", "");
        r.chatLink = j.value("chat_link", "");

        if (j.contains("disciplines") && j["disciplines"].is_array()) {
            for (auto& d : j["disciplines"])
                if (d.is_string()) r.disciplines.push_back(d.get<std::string>());
        }
        if (j.contains("ingredients") && j["ingredients"].is_array()) {
            for (auto& ing : j["ingredients"]) {
                GW2Ingredient gi;
                gi.itemId = ing.value("item_id", 0);
                gi.count = ing.value("count", 0);
                r.ingredients.push_back(gi);
            }
        }
        return r;
    } catch (...) {}
    return {};
}

GW2MapInfo GW2Client::GetMap(int id) {
    std::string path = "/v2/maps?ids=" + std::to_string(id);
    auto resp = m_http.Get(API_HOST, path);
    if (!resp || resp->statusCode != 200) return {};

    try {
        auto arr = json::parse(resp->body);
        if (arr.empty()) return {};
        auto& j = arr[0];

        GW2MapInfo m;
        m.found = true;
        m.id = j.value("id", 0);
        m.name = j.value("name", "");
        m.minLevel = j.value("min_level", 0);
        m.maxLevel = j.value("max_level", 0);
        m.regionId = j.value("region_id", 0);
        m.regionName = j.value("region_name", "");
        m.continentId = j.value("continent_id", 0);
        m.continentName = j.value("continent_name", "");
        return m;
    } catch (...) {}
    return {};
}

GW2MapInfo GW2Client::GetMapWithWaypoints(int mapId) {
    auto mapInfo = GetMap(mapId);
    if (!mapInfo.found || mapInfo.continentId == 0 || mapInfo.regionId == 0)
        return mapInfo;

    std::string path = "/v2/continents/" + std::to_string(mapInfo.continentId)
                     + "/floors/1/regions/" + std::to_string(mapInfo.regionId)
                     + "/maps/" + std::to_string(mapId);
    auto resp = m_http.Get(API_HOST, path);
    if (!resp || resp->statusCode != 200) return mapInfo;

    try {
        auto j = json::parse(resp->body);
        if (j.contains("points_of_interest") && j["points_of_interest"].is_object()) {
            for (auto& [key, poi] : j["points_of_interest"].items()) {
                std::string poiType = poi.value("type", "");
                if (poiType != "waypoint") continue;

                GW2Waypoint wp;
                wp.name = poi.value("name", "");
                wp.chatLink = poi.value("chat_link", "");
                wp.floor = poi.value("floor", 0);
                if (!wp.name.empty() && !wp.chatLink.empty())
                    mapInfo.waypoints.push_back(std::move(wp));
            }
        }
    } catch (...) {}

    return mapInfo;
}

std::vector<int> GW2Client::SearchRecipesByOutput(int outputItemId) {
    std::vector<int> result;
    std::string path = "/v2/recipes/search?output=" + std::to_string(outputItemId);
    auto resp = m_http.Get(API_HOST, path);
    if (!resp || resp->statusCode != 200) return result;

    try {
        auto arr = json::parse(resp->body);
        for (auto& id : arr)
            if (id.is_number_integer()) result.push_back(id.get<int>());
    } catch (...) {}

    return result;
}

std::vector<WikiSearchResult> GW2Client::WikiSearch(const std::string& query, int limit) {
    std::vector<WikiSearchResult> result;
    std::string path = "/api.php?action=opensearch&search=" + UrlEncode(query)
                     + "&limit=" + std::to_string(limit) + "&format=json";
    auto resp = m_http.Get(WIKI_HOST, path);
    if (!resp || resp->statusCode != 200) return result;

    try {
        auto arr = json::parse(resp->body);
        if (arr.size() >= 4 && arr[1].is_array() && arr[3].is_array()) {
            for (size_t i = 0; i < arr[1].size(); ++i) {
                WikiSearchResult r;
                r.title = arr[1][i].get<std::string>();
                if (i < arr[3].size()) r.url = arr[3][i].get<std::string>();
                result.push_back(std::move(r));
            }
        }
    } catch (...) {}

    return result;
}

WikiPage GW2Client::WikiGetPage(const std::string& title) {
    std::string path = "/api.php?action=parse&page=" + UrlEncode(title)
                     + "&prop=wikitext&format=json";
    auto resp = m_http.Get(WIKI_HOST, path);
    if (!resp || resp->statusCode != 200) return {};

    try {
        auto j = json::parse(resp->body);
        if (!j.contains("parse")) return {};
        WikiPage page;
        page.found = true;
        page.title = j["parse"].value("title", title);
        if (j["parse"].contains("wikitext") && j["parse"]["wikitext"].contains("*"))
            page.wikitext = j["parse"]["wikitext"]["*"].get<std::string>();
        return page;
    } catch (...) {}
    return {};
}
