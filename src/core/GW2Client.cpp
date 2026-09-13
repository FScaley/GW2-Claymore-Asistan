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

// Every GET goes through here so a transport failure (DNS, TLS, firewall - anything that yields
// no HTTP response) reaches the Nexus log with its WinHTTP code instead of silently turning into
// "not found". Non-200 statuses are the callers' business and stay quiet (404 is routine).
std::optional<HttpResponse> GW2Client::Fetch(const char* host, const std::string& path, int timeoutMs) {
    auto resp = m_http.Get(host, path, timeoutMs);
    if (!resp && m_logger) {
        std::string shown = path.size() > 120 ? path.substr(0, 120) + "..." : path;
        m_logger("[HTTP] " + std::string(host) + shown + " -> " + m_http.LastFailureText());
    }
    return resp;
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
    auto resp = Fetch(API_HOST, path);
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
    auto resp = Fetch(API_HOST, path);
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
    auto resp = Fetch(API_HOST, path);
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
    auto resp = Fetch(API_HOST, path);
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
        if (j.contains("continent_rect") && j["continent_rect"].is_array() && j["continent_rect"].size() == 2) {
            auto& r = j["continent_rect"];
            if (r[0].is_array() && r[1].is_array() && r[0].size() == 2 && r[1].size() == 2) {
                m.contRect[0][0] = r[0][0].get<double>();
                m.contRect[0][1] = r[0][1].get<double>();
                m.contRect[1][0] = r[1][0].get<double>();
                m.contRect[1][1] = r[1][1].get<double>();
                m.hasContRect = true;
            }
        }
        m.defaultFloor = j.value("default_floor", 1);
        if (j.contains("floors") && j["floors"].is_array())
            for (auto& f : j["floors"])
                if (f.is_number_integer()) m.floors.push_back(f.get<int>());
        return m;
    } catch (...) {}
    return {};
}

GW2MapInfo GW2Client::GetMapWithWaypoints(int mapId) {
    auto mapInfo = GetMap(mapId);
    if (!mapInfo.found || mapInfo.continentId == 0 || mapInfo.regionId == 0)
        return mapInfo;

    std::vector<int> floorOrder;
    floorOrder.push_back(mapInfo.defaultFloor);
    for (int f : mapInfo.floors)
        if (f != mapInfo.defaultFloor) floorOrder.push_back(f);

    for (int floor : floorOrder) {
        std::string path = "/v2/continents/" + std::to_string(mapInfo.continentId)
                         + "/floors/" + std::to_string(floor)
                         + "/regions/" + std::to_string(mapInfo.regionId)
                         + "/maps/" + std::to_string(mapId);
        auto resp = Fetch(API_HOST, path);
        if (!resp || resp->statusCode != 200) continue;

        try {
            auto j = json::parse(resp->body);
            if (!j.contains("points_of_interest") || !j["points_of_interest"].is_object()) continue;
            for (auto& [key, poi] : j["points_of_interest"].items()) {
                if (poi.value("type", "") != "waypoint") continue;

                GW2Waypoint wp;
                wp.name = poi.value("name", "");
                wp.chatLink = poi.value("chat_link", "");
                wp.floor = poi.value("floor", floor);
                if (poi.contains("coord") && poi["coord"].is_array() && poi["coord"].size() == 2) {
                    wp.x = poi["coord"][0].get<double>();
                    wp.y = poi["coord"][1].get<double>();
                    wp.hasCoord = true;
                }
                if (!wp.name.empty() && !wp.chatLink.empty())
                    mapInfo.waypoints.push_back(std::move(wp));
            }
        } catch (...) {
            continue;
        }

        if (!mapInfo.waypoints.empty()) {
            mapInfo.floorUsed = floor;
            break;
        }
    }

    return mapInfo;
}

std::vector<int> GW2Client::SearchRecipesByOutput(int outputItemId) {
    std::vector<int> result;
    std::string path = "/v2/recipes/search?output=" + std::to_string(outputItemId);
    auto resp = Fetch(API_HOST, path);
    if (!resp || resp->statusCode != 200) return result;

    try {
        auto arr = json::parse(resp->body);
        for (auto& id : arr)
            if (id.is_number_integer()) result.push_back(id.get<int>());
    } catch (...) {}

    return result;
}

namespace {

// Lowercase alphanumeric words of 3+ chars, minus filler that models like to append
// ("Guild Wars 2", "GW2", "wiki"). Used for both the query and candidate titles.
std::vector<std::string> SearchWords(const std::string& s) {
    static const char* const PADDING[] = {"gw2", "guild", "wars", "wiki"};
    std::vector<std::string> words;
    std::string cur;
    auto flush = [&]() {
        if (cur.size() >= 3) {
            bool pad = false;
            for (auto* p : PADDING) if (cur == p) { pad = true; break; }
            if (!pad) words.push_back(cur);
        }
        cur.clear();
    };
    for (unsigned char c : s) {
        if (std::isalnum(c)) cur += static_cast<char>(std::tolower(c));
        else flush();
    }
    flush();
    return words;
}

// Prefix-tolerant equality: "tokens" ~ "token", "hearts" ~ "heart".
bool WordsMatch(const std::string& a, const std::string& b) {
    return a.compare(0, b.size(), b) == 0 || b.compare(0, a.size(), a) == 0;
}

// Shared-word count plus the fraction of the title covered, so a short exact title
// ("Renown Heart") outranks a long partial one ("Shard of Janthir Syntri") on a tie.
// 0 when the title shares no word with the query - then it is a guess, not a match.
double TitleScore(const std::vector<std::string>& queryWords, const std::string& title) {
    auto titleWords = SearchWords(title);
    if (titleWords.empty() || queryWords.empty()) return 0.0;
    int shared = 0;
    for (auto& tw : titleWords)
        for (auto& qw : queryWords)
            if (WordsMatch(tw, qw)) { ++shared; break; }
    if (shared == 0) return 0.0;
    return shared + static_cast<double>(shared) / titleWords.size();
}

} // namespace

// Tier 1: opensearch = title PREFIX match. Exact page names hit; a plural or one extra
// word ("Janthir Syntri Renown Tokens", "Gorrik Kourna location") returns nothing.
// Tier 2 (only when tier 1 succeeded with zero hits): full-text search, re-ranked by
// word overlap with the query and capped at 3 candidates. Results carry fulltext=true.
std::vector<WikiSearchResult> GW2Client::WikiSearch(const std::string& query, int limit) {
    std::vector<WikiSearchResult> result;
    std::string path = "/api.php?action=opensearch&search=" + UrlEncode(query)
                     + "&limit=" + std::to_string(limit) + "&format=json";
    auto resp = Fetch(WIKI_HOST, path);
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

    if (result.empty()) return WikiFullTextSearch(query, limit);
    return result;
}

std::vector<WikiSearchResult> GW2Client::WikiFullTextSearch(const std::string& query, int limit) {
    std::vector<WikiSearchResult> result;
    auto queryWords = SearchWords(query);
    if (queryWords.empty()) return result;

    std::string path = "/api.php?action=query&list=search&srsearch=" + UrlEncode(query)
                     + "&srlimit=5&srnamespace=0&format=json";
    auto resp = Fetch(WIKI_HOST, path);
    if (!resp || resp->statusCode != 200) return result;

    struct Scored { WikiSearchResult r; double score; };
    std::vector<Scored> scored;
    try {
        auto j = json::parse(resp->body);
        if (!j.contains("query") || !j["query"].contains("search")) return result;
        for (auto& hit : j["query"]["search"]) {
            std::string title = hit.value("title", "");
            if (title.empty()) continue;
            double score = TitleScore(queryWords, title);
            if (score <= 0.0) continue;
            WikiSearchResult r;
            r.title = title;
            std::string slug = title;
            std::replace(slug.begin(), slug.end(), ' ', '_');
            r.url = "https://wiki.guildwars2.com/wiki/" + UrlEncode(slug);
            r.fulltext = true;
            scored.push_back({std::move(r), score});
        }
    } catch (...) { return result; }

    std::stable_sort(scored.begin(), scored.end(),
                     [](const Scored& a, const Scored& b) { return a.score > b.score; });
    int cap = std::min(limit, 3);
    for (auto& s : scored) {
        if (static_cast<int>(result.size()) >= cap) break;
        result.push_back(std::move(s.r));
    }
    return result;
}

WikiPage GW2Client::WikiGetPage(const std::string& title) {
    std::string path = "/api.php?action=parse&page=" + UrlEncode(title)
                     + "&prop=wikitext&format=json";
    auto resp = Fetch(WIKI_HOST, path);
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

WikiPage GW2Client::WikiGetPageHtml(const std::string& title) {
    std::string path = "/api.php?action=parse&page=" + UrlEncode(title)
                     + "&prop=text&disabletoc=1&disableeditsection=1&format=json";
    auto resp = Fetch(WIKI_HOST, path, 25000);
    if (!resp || resp->statusCode != 200) return {};

    try {
        auto j = json::parse(resp->body);
        if (!j.contains("parse")) return {};
        WikiPage page;
        page.found = true;
        page.title = j["parse"].value("title", title);
        if (j["parse"].contains("text") && j["parse"]["text"].contains("*"))
            page.html = j["parse"]["text"]["*"].get<std::string>();
        return page;
    } catch (...) {}
    return {};
}
