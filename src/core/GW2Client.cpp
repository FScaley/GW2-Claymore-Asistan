#include "GW2Client.h"
#include "WikiText.h"
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
        if (j.contains("map_rect") && j["map_rect"].is_array() && j["map_rect"].size() == 2) {
            auto& r = j["map_rect"];
            if (r[0].is_array() && r[1].is_array() && r[0].size() == 2 && r[1].size() == 2) {
                m.mapRect[0][0] = r[0][0].get<double>();
                m.mapRect[0][1] = r[0][1].get<double>();
                m.mapRect[1][0] = r[1][0].get<double>();
                m.mapRect[1][1] = r[1][1].get<double>();
                m.hasMapRect = true;
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

std::vector<GW2MapInfo> GW2Client::GetMaps(const std::vector<int>& ids) {
    std::vector<GW2MapInfo> result;
    if (ids.empty()) return result;
    std::string path = "/v2/maps?ids=" + BuildIdList(ids);
    auto resp = Fetch(API_HOST, path);
    if (!resp || resp->statusCode != 200) return result;
    try {
        auto arr = json::parse(resp->body);
        for (auto& j : arr) {
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
                    m.contRect[0][0] = r[0][0].get<double>(); m.contRect[0][1] = r[0][1].get<double>();
                    m.contRect[1][0] = r[1][0].get<double>(); m.contRect[1][1] = r[1][1].get<double>();
                    m.hasContRect = true;
                }
            }
            if (j.contains("map_rect") && j["map_rect"].is_array() && j["map_rect"].size() == 2) {
                auto& r = j["map_rect"];
                if (r[0].is_array() && r[1].is_array() && r[0].size() == 2 && r[1].size() == 2) {
                    m.mapRect[0][0] = r[0][0].get<double>(); m.mapRect[0][1] = r[0][1].get<double>();
                    m.mapRect[1][0] = r[1][0].get<double>(); m.mapRect[1][1] = r[1][1].get<double>();
                    m.hasMapRect = true;
                }
            }
            m.defaultFloor = j.value("default_floor", 1);
            if (j.contains("floors") && j["floors"].is_array())
                for (auto& f : j["floors"])
                    if (f.is_number_integer()) m.floors.push_back(f.get<int>());
            result.push_back(std::move(m));
        }
    } catch (...) {}
    return result;
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
            if (j.contains("sectors") && j["sectors"].is_object()) {
                for (auto& [key, sec] : j["sectors"].items()) {
                    GW2Sector s;
                    s.name = sec.value("name", "");
                    if (sec.contains("coord") && sec["coord"].is_array() && sec["coord"].size() == 2) {
                        s.x = sec["coord"][0].get<double>();
                        s.y = sec["coord"][1].get<double>();
                    }
                    if (!s.name.empty())
                        mapInfo.sectors.push_back(std::move(s));
                }
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

std::vector<WikiSearchResult> GW2Client::WikiSearchWithInteractiveMap(const std::string& query, int limit) {
    std::vector<WikiSearchResult> result;
    auto queryWords = SearchWords(query);
    if (queryWords.empty()) return result;

    std::string searchTerms = "insource:\"interactive map\"";
    for (auto& w : queryWords) searchTerms += " " + w;
    std::string path = "/api.php?action=query&list=search&srsearch=" + UrlEncode(searchTerms)
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

std::vector<GuideSearchResult> GW2Client::GuideSearch(const std::string& query, int limit) {
    std::string path = "/wp-json/wp/v2/search?search=" + UrlEncode(query)
                     + "&per_page=" + std::to_string(limit);
    auto resp = Fetch(GUIDE_HOST, path, 25000);
    if (!resp || resp->statusCode != 200) return {};

    std::vector<GuideSearchResult> results;
    try {
        auto arr = json::parse(resp->body);
        if (!arr.is_array()) return {};
        for (auto& item : arr) {
            GuideSearchResult r;
            r.id = item.value("id", 0);
            r.title = WikiText::DecodeEntities(item.value("title", ""));
            if (item.contains("_links") && item["_links"].contains("self")
                && item["_links"]["self"].is_array() && !item["_links"]["self"].empty()) {
                r.selfHref = item["_links"]["self"][0].value("href", "");
            }
            if (r.id > 0 && !r.title.empty())
                results.push_back(std::move(r));
        }
    } catch (...) {}

    if (results.size() > 1) {
        auto queryWords = SearchWords(query);
        if (!queryWords.empty()) {
            auto SharedCount = [&](const std::string& title) -> int {
                auto titleWords = SearchWords(title);
                int shared = 0;
                for (auto& tw : titleWords)
                    for (auto& qw : queryWords)
                        if (WordsMatch(tw, qw)) { ++shared; break; }
                return shared;
            };
            int topShared = SharedCount(results[0].title);
            bool anyBetter = false;
            for (size_t i = 1; i < results.size(); ++i) {
                if (SharedCount(results[i].title) > topShared) { anyBetter = true; break; }
            }
            if (anyBetter) {
                std::stable_sort(results.begin(), results.end(),
                    [&](const GuideSearchResult& a, const GuideSearchResult& b) {
                        return SharedCount(a.title) > SharedCount(b.title);
                    });
            }
        }
    }

    return results;
}

std::vector<BuildSearchResult> GW2Client::BuildSearch(const std::string& query, int limit) {
    std::string path = "/wiki/api.php?action=query&list=search&srnamespace=3000&srsearch="
                     + UrlEncode(query) + "&srlimit=" + std::to_string(limit) + "&format=json";
    auto resp = Fetch(BUILD_HOST, path, 25000);
    if (!resp || resp->statusCode != 200) return {};

    std::vector<BuildSearchResult> results;
    try {
        auto j = json::parse(resp->body);
        if (!j.contains("query") || !j["query"].contains("search")) return {};
        for (auto& hit : j["query"]["search"]) {
            BuildSearchResult r;
            r.title = hit.value("title", "");
            r.pageId = hit.value("pageid", 0);
            r.size = hit.value("size", 0);
            r.timestamp = hit.value("timestamp", "");
            if (!r.title.empty() && r.size > 100)
                results.push_back(std::move(r));
        }
    } catch (...) {}
    return results;
}

WikiPage GW2Client::BuildGetPage(const std::string& title) {
    std::string path = "/wiki/api.php?action=parse&page=" + UrlEncode(title)
                     + "&prop=wikitext&format=json";
    auto resp = Fetch(BUILD_HOST, path);
    if (!resp || resp->statusCode != 200) return {};

    try {
        auto j = json::parse(resp->body);
        if (!j.contains("parse")) return {};
        WikiPage page;
        page.found = true;
        page.title = j["parse"].value("title", title);
        if (j["parse"].contains("wikitext")) {
            if (j["parse"]["wikitext"].is_string())
                page.wikitext = j["parse"]["wikitext"].get<std::string>();
            else if (j["parse"]["wikitext"].contains("*"))
                page.wikitext = j["parse"]["wikitext"]["*"].get<std::string>();
        }
        return page;
    } catch (...) {}
    return {};
}

WikiPage GW2Client::BuildGetPageHtml(const std::string& title) {
    std::string path = "/wiki/api.php?action=parse&page=" + UrlEncode(title)
                     + "&prop=text&disabletoc=1&disableeditsection=1&format=json";
    auto resp = Fetch(BUILD_HOST, path, 25000);
    if (!resp || resp->statusCode != 200) return {};

    try {
        auto j = json::parse(resp->body);
        if (!j.contains("parse")) return {};
        WikiPage page;
        page.found = true;
        page.title = j["parse"].value("title", title);
        if (j["parse"].contains("text")) {
            if (j["parse"]["text"].is_string())
                page.html = j["parse"]["text"].get<std::string>();
            else if (j["parse"]["text"].contains("*"))
                page.html = j["parse"]["text"]["*"].get<std::string>();
        }
        return page;
    } catch (...) {}
    return {};
}

GuidePage GW2Client::GuideGetContent(const std::string& selfHref) {
    if (selfHref.empty()) return {};
    auto schemeEnd = selfHref.find("://");
    if (schemeEnd == std::string::npos) return {};
    auto hostStart = schemeEnd + 3;
    auto pathStart = selfHref.find('/', hostStart);
    if (pathStart == std::string::npos) return {};
    std::string host = selfHref.substr(hostStart, pathStart - hostStart);
    if (host != GUIDE_HOST) return {};
    std::string path = selfHref.substr(pathStart) + "?_fields=title,link,modified,content";

    auto resp = Fetch(host.c_str(), path, 25000);
    if (!resp || resp->statusCode != 200) return {};

    try {
        auto j = json::parse(resp->body);
        GuidePage page;
        page.found = true;
        if (j.contains("title") && j["title"].is_object())
            page.title = WikiText::DecodeEntities(j["title"].value("rendered", ""));
        else
            page.title = WikiText::DecodeEntities(j.value("title", ""));
        page.url = j.value("link", "");
        page.modified = j.value("modified", "");
        if (j.contains("content") && j["content"].is_object())
            page.html = j["content"].value("rendered", "");
        return page;
    } catch (...) {}
    return {};
}

AchievementInfo GW2Client::GetAchievement(int id) {
    std::string path = "/v2/achievements/" + std::to_string(id);
    auto resp = Fetch(API_HOST, path);
    if (!resp || resp->statusCode != 200) return {};
    try {
        auto j = json::parse(resp->body);
        AchievementInfo a;
        a.found = true;
        a.id = j.value("id", 0);
        a.name = j.value("name", "");
        if (j.contains("bits") && j["bits"].is_array()) {
            for (auto& b : j["bits"]) {
                AchievementBit bit;
                bit.type = b.value("type", "");
                bit.id = b.value("id", 0);
                bit.text = b.value("text", "");
                a.bits.push_back(std::move(bit));
            }
        }
        return a;
    } catch (...) {}
    return {};
}

AccountAchievement GW2Client::GetAccountAchievement(int id, const std::string& apiKey) {
    if (apiKey.empty()) return {};
    std::string path = "/v2/account/achievements?id=" + std::to_string(id)
                     + "&access_token=" + UrlEncode(apiKey);
    auto resp = Fetch(API_HOST, path);
    if (!resp || resp->statusCode != 200) return {};
    try {
        auto j = json::parse(resp->body);
        AccountAchievement a;
        a.found = true;
        a.id = j.value("id", 0);
        a.done = j.value("done", false);
        a.current = j.value("current", 0);
        a.max = j.value("max", 0);
        if (j.contains("bits") && j["bits"].is_array()) {
            for (auto& b : j["bits"])
                if (b.is_number_integer()) a.bits.push_back(b.get<int>());
        }
        return a;
    } catch (...) {}
    return {};
}

TokenInfo GW2Client::GetTokenInfo(const std::string& apiKey) {
    if (apiKey.empty()) return {};
    std::string path = "/v2/tokeninfo?access_token=" + UrlEncode(apiKey);
    auto resp = Fetch(API_HOST, path);
    if (!resp || resp->statusCode != 200) return {};
    try {
        auto j = json::parse(resp->body);
        TokenInfo t;
        t.found = true;
        t.name = j.value("name", "");
        if (j.contains("permissions") && j["permissions"].is_array())
            for (auto& p : j["permissions"])
                if (p.is_string()) t.permissions.push_back(p.get<std::string>());
        return t;
    } catch (...) {}
    return {};
}
