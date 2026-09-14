#include "FunctionHandler.h"
#include "WikiText.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

using json = nlohmann::json;

static const char* CANCELLED_JSON = "{\"error\": \"cancelled by user\"}";

static std::string TrimStr(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

static std::string LowerStr(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

static std::string LeadOf(const std::string& wikitext) {
    auto firstHeader = wikitext.find("\n==");
    return wikitext.substr(0, firstHeader == std::string::npos ? wikitext.size() : firstHeader);
}

static std::string InfoboxField(const std::string& text, const std::string& field) {
    size_t pos = 0;
    while ((pos = text.find('|', pos)) != std::string::npos) {
        size_t p = pos + 1;
        while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) ++p;
        if (text.compare(p, field.size(), field) == 0) {
            size_t q = p + field.size();
            while (q < text.size() && (text[q] == ' ' || text[q] == '\t')) ++q;
            if (q < text.size() && text[q] == '=') {
                size_t nl = text.find('\n', q);
                return TrimStr(text.substr(q + 1, nl == std::string::npos ? std::string::npos : nl - q - 1));
            }
        }
        pos = pos + 1;
    }
    return "";
}

static std::string StripWikiLink(std::string s) {
    s = TrimStr(s);
    if (s.size() >= 4 && s.compare(0, 2, "[[") == 0) {
        auto rb = s.find("]]");
        if (rb != std::string::npos) {
            std::string inner = s.substr(2, rb - 2);
            auto pipe = inner.find('|');
            s = TrimStr(pipe == std::string::npos ? inner : inner.substr(0, pipe));
        }
    }
    return s;
}

static std::vector<std::string> SplitLocations(std::string value, size_t cap) {
    for (const char* br : {"<br />", "<br/>", "<br>"}) {
        size_t p;
        while ((p = value.find(br)) != std::string::npos) value.replace(p, std::strlen(br), ";");
    }
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= value.size() && out.size() < cap) {
        auto semi = value.find(';', start);
        std::string part = value.substr(start, semi == std::string::npos ? std::string::npos : semi - start);
        while (true) {
            auto a = part.find("{{");
            if (a == std::string::npos) break;
            auto b = part.find("}}", a);
            if (b == std::string::npos) { part = part.substr(0, a); break; }
            part.erase(a, b - a + 2);
        }
        part = StripWikiLink(part);
        if (!part.empty()) out.push_back(part);
        if (semi == std::string::npos) break;
        start = semi + 1;
    }
    return out;
}

static bool ParseCoordinates(const std::string& value, double& x, double& y) {
    auto lb = value.find('[');
    auto rb = value.find(']');
    std::string inner = (lb != std::string::npos && rb != std::string::npos && rb > lb)
        ? value.substr(lb + 1, rb - lb - 1) : value;
    auto comma = inner.find(',');
    if (comma == std::string::npos) return false;
    try {
        x = std::stod(TrimStr(inner.substr(0, comma)));
        y = std::stod(TrimStr(inner.substr(comma + 1)));
        return true;
    } catch (...) {
        return false;
    }
}

static bool InContinentRect(const GW2MapInfo& m, double x, double y) {
    if (!m.hasContRect) return false;
    double x1 = std::min(m.contRect[0][0], m.contRect[1][0]);
    double x2 = std::max(m.contRect[0][0], m.contRect[1][0]);
    double y1 = std::min(m.contRect[0][1], m.contRect[1][1]);
    double y2 = std::max(m.contRect[0][1], m.contRect[1][1]);
    return x >= x1 && x <= x2 && y >= y1 && y <= y2;
}

static json BuildWaypointList(std::vector<GW2Waypoint> wps, bool sortByDist, double px, double py) {
    auto dist = [&](const GW2Waypoint& w) {
        return w.hasCoord ? std::hypot(w.x - px, w.y - py) : 1e18;
    };
    if (sortByDist)
        std::sort(wps.begin(), wps.end(),
                  [&](const GW2Waypoint& a, const GW2Waypoint& b) { return dist(a) < dist(b); });
    json arr = json::array();
    for (size_t i = 0; i < wps.size(); ++i) {
        json w;
        w["name"] = wps[i].name;
        w["chat_link"] = wps[i].chatLink;
        if (sortByDist && i == 0 && wps[i].hasCoord) w["nearest"] = true;
        arr.push_back(w);
    }
    return arr;
}

FunctionHandler::FunctionHandler(GW2Client* gw2, ItemIndex* index)
    : m_gw2(gw2), m_index(index) {}

FunctionResult FunctionHandler::Handle(const FunctionCall& call, CancelCheck shouldCancel) {
    FunctionResult result;
    result.callId = call.id;
    result.name = call.name;

    if (call.name == "gw2_item_info")
        result.resultText = HandleItemInfo(call.arguments, shouldCancel);
    else if (call.name == "gw2_recipe")
        result.resultText = HandleRecipe(call.arguments, shouldCancel);
    else if (call.name == "gw2_map")
        result.resultText = HandleMap(call.arguments, shouldCancel);
    else if (call.name == "gw2_wiki")
        result.resultText = HandleWiki(call.arguments, shouldCancel);
    else if (call.name == "gw2_guide")
        result.resultText = HandleGuide(call.arguments, shouldCancel);
    else
        result.resultText = "{\"error\": \"Unknown function: " + call.name + "\"}";

    return result;
}

json FunctionHandler::GetToolDefinitions() {
    json tools = json::array();

    tools.push_back({
        {"type", "function"},
        {"name", "gw2_item_info"},
        {"description", "Get GW2 item details and Trading Post prices by item name. Returns item info (id, rarity, level, chat_link) and current TP buy/sell prices formatted in gold/silver/copper."},
        {"parameters", {
            {"type", "object"},
            {"properties", {
                {"name", {
                    {"type", "string"},
                    {"description", "Item name in English (e.g. 'Dusk', 'Glob of Ectoplasm', 'Mystic Coin')"}
                }}
            }},
            {"required", json::array({"name"})}
        }}
    });

    tools.push_back({
        {"type", "function"},
        {"name", "gw2_recipe"},
        {"description", "Look up crafting recipe for a GW2 item by name. Returns ingredients with quantities, crafting disciplines, rating, and ingredient TP prices."},
        {"parameters", {
            {"type", "object"},
            {"properties", {
                {"name", {
                    {"type", "string"},
                    {"description", "Output item name in English (e.g. 'Deldrimor Steel Ingot', 'Superior Rune of the Scholar')"}
                }}
            }},
            {"required", json::array({"name"})}
        }}
    });

    tools.push_back({
        {"type", "function"},
        {"name", "gw2_map"},
        {"description", "Get GW2 map info with all waypoints and their REAL chat_link codes. Use this whenever the user asks about a location, waypoint, or map. Returns verified waypoint codes that work in-game."},
        {"parameters", {
            {"type", "object"},
            {"properties", {
                {"name", {
                    {"type", "string"},
                    {"description", "Map name in English (e.g. 'Queensdale', 'Kessex Hills', 'Verdant Brink')"}
                }}
            }},
            {"required", json::array({"name"})}
        }}
    });

    tools.push_back({
        {"type", "function"},
        {"name", "gw2_wiki"},
        {"description", "Search the GW2 Wiki and return the page as readable text plus a 'sections' list. For mounts, legendaries, collections and achievements the result also includes 'sub_collections': the COMPLETE item list of every linked collection, extracted directly from the wiki tables — always relay those lists verbatim. If the page itself is a collection, its items are in 'items'. For NPC, boss and event pages the result includes 'location_areas' and 'locations': one entry per map the NPC appears in ({map_name, areas, npc_here, waypoints[] with REAL chat_link codes}); 'npc_here': true marks the map containing the NPC's known coordinates and its waypoints are sorted nearest-first ('nearest': true on the first). Pick the entry that matches the user's question (e.g. the map they named); 'map_name'/'nearby_waypoints' mirror the primary entry. For areas in 'other_locations' call gw2_map. IMPORTANT: 'locations' is derived from the infobox and may be incomplete; the full list with conditions is in the 'Locations' section in 'content'. Pass 'section' (a name from the 'sections' list) to fetch one specific section in full when the default content was truncated."},
        {"parameters", {
            {"type", "object"},
            {"properties", {
                {"query", {
                    {"type", "string"},
                    {"description", "Exact English wiki page title when known, singular (e.g. 'Roller Beetle', 'Miyani', 'Janthir Syntri Renown Token'); otherwise a short keyword phrase. The search is title-based: NEVER append filler such as 'Guild Wars 2', 'GW2', 'location', 'guide', 'farm' - extra words return nothing."}
                }},
                {"section", {
                    {"type", "string"},
                    {"description", "Optional. Exact section title from a previous result's 'sections' list (e.g. 'Unlocking', 'Acquisition') to fetch that section in full."}
                }}
            }},
            {"required", json::array({"query"})}
        }}
    });

    tools.push_back({
        {"type", "function"},
        {"name", "gw2_guide"},
        {"description", "Search guildjen.com for a community guide and return it as readable text. Use for step-by-step walkthroughs, farming routes, leveling/gearing strategy, mode introductions (WvW/PvP/fractals/raids), and 'en iyi yol' questions. NOT for collection or achievement item lists (those come from gw2_wiki). The result includes a 'modified' date showing when the guide was last updated."},
        {"parameters", {
            {"type", "object"},
            {"properties", {
                {"query", {
                    {"type", "string"},
                    {"description", "Short English keywords for the guide topic (e.g. 'fishing guide', 'wvw beginner', 'legendary armor raid', 'gold farming'). Keep it concise — 2-4 words."}
                }},
                {"section", {
                    {"type", "string"},
                    {"description", "Optional. Exact section title from a previous result's 'sections' list to fetch that section in full when content was truncated."}
                }}
            }},
            {"required", json::array({"query"})}
        }}
    });

    return tools;
}

int FunctionHandler::ResolveItemId(const std::string& name) {
    int cached = m_index->Find(name);
    if (cached > 0) return cached;

    auto results = m_gw2->WikiSearch(name, 3);
    if (results.empty()) return 0;

    for (auto& r : results) {
        auto page = m_gw2->WikiGetPage(r.title);
        if (!page.found) continue;

        std::string idStr = GW2Client::ExtractItemIdFromWikitext(page.wikitext);
        if (!idStr.empty()) {
            int id = std::stoi(idStr);
            m_index->Add(r.title, id);
            m_index->Add(name, id);
            return id;
        }
    }
    return 0;
}

int FunctionHandler::ResolveMapId(const std::string& rawName, int depth) {
    std::string name = TrimStr(rawName);
    if (name.empty() || depth > 2) return 0;
    int cached = m_index->Find("map:" + name);
    if (cached > 0) return cached;

    auto results = m_gw2->WikiSearch(name, 5);
    if (results.empty()) return 0;

    for (auto& r : results) {
        auto page = m_gw2->WikiGetPage(r.title);
        if (!page.found) continue;

        if (page.wikitext.rfind("#REDIRECT", 0) == 0) {
            auto lb = page.wikitext.find("[[");
            auto rb = page.wikitext.find("]]");
            if (lb == std::string::npos || rb == std::string::npos || rb <= lb) continue;
            std::string target = page.wikitext.substr(lb + 2, rb - lb - 2);
            auto hash = target.find('#');
            if (hash != std::string::npos) target = target.substr(0, hash);
            page = m_gw2->WikiGetPage(target);
            if (!page.found) continue;
        }

        std::string lead = LeadOf(page.wikitext);
        if (LowerStr(lead).find("location infobox") == std::string::npos) continue;

        std::string idStr = GW2Client::ExtractItemIdFromWikitext(lead);
        if (!idStr.empty()) {
            int id = std::stoi(idStr);
            m_index->Add("map:" + name, id);
            m_index->Add("map:" + r.title, id);
            return id;
        }

        std::string within = StripWikiLink(InfoboxField(lead, "within"));
        if (!within.empty() && LowerStr(within) != LowerStr(name)) {
            int id = ResolveMapId(within, depth + 1);
            if (id > 0) {
                m_index->Add("map:" + name, id);
                m_index->Add("map:" + r.title, id);
                return id;
            }
        }
    }
    return 0;
}

std::string FunctionHandler::HandleMap(const json& args, const CancelCheck& cancel) {
    std::string name = args.value("name", "");
    if (name.empty()) return "{\"error\": \"name parameter required\"}";

    int mapId = ResolveMapId(name);
    if (Cancelled(cancel)) return CANCELLED_JSON;
    if (mapId <= 0)
        return "{\"error\": \"Map not found: " + name + "\"}";

    auto mapInfo = m_gw2->GetMapWithWaypoints(mapId);
    if (!mapInfo.found)
        return "{\"error\": \"Map ID " + std::to_string(mapId) + " not found in API\"}";

    json result;
    result["map_id"] = mapInfo.id;
    result["name"] = mapInfo.name;
    result["level_range"] = std::to_string(mapInfo.minLevel) + "-" + std::to_string(mapInfo.maxLevel);
    result["region"] = mapInfo.regionName;
    result["continent"] = mapInfo.continentName;

    json waypoints = json::array();
    for (auto& wp : mapInfo.waypoints) {
        json w;
        w["name"] = wp.name;
        w["chat_link"] = wp.chatLink;
        waypoints.push_back(w);
    }
    result["waypoints"] = waypoints;
    result["waypoint_count"] = mapInfo.waypoints.size();

    return result.dump();
}

std::string FunctionHandler::HandleItemInfo(const json& args, const CancelCheck& cancel) {
    std::string name = args.value("name", "");
    if (name.empty()) return "{\"error\": \"name parameter required\"}";

    int itemId = ResolveItemId(name);
    if (Cancelled(cancel)) return CANCELLED_JSON;
    if (itemId <= 0)
        return "{\"error\": \"Item not found: " + name + "\"}";

    auto item = m_gw2->GetItem(itemId);
    if (!item.found)
        return "{\"error\": \"Item ID " + std::to_string(itemId) + " not found in GW2 API\"}";

    json result;
    result["id"] = item.id;
    result["name"] = item.name;
    result["type"] = item.type;
    result["rarity"] = item.rarity;
    result["level"] = item.level;
    result["chat_link"] = item.chatLink;
    if (!item.description.empty())
        result["description"] = item.description;
    result["vendor_value"] = GW2Client::FormatPrice(item.vendorValue);

    auto price = m_gw2->GetPrice(itemId);
    if (price.found) {
        result["tradeable"] = true;
        result["tp_buy"] = GW2Client::FormatPrice(price.buyPrice);
        result["tp_buy_quantity"] = price.buyQuantity;
        result["tp_sell"] = GW2Client::FormatPrice(price.sellPrice);
        result["tp_sell_quantity"] = price.sellQuantity;
    } else {
        result["tradeable"] = false;
    }

    m_index->Add(item.name, item.id);

    return result.dump();
}

std::string FunctionHandler::HandleRecipe(const json& args, const CancelCheck& cancel) {
    std::string name = args.value("name", "");
    if (name.empty()) return "{\"error\": \"name parameter required\"}";

    int itemId = ResolveItemId(name);
    if (Cancelled(cancel)) return CANCELLED_JSON;
    if (itemId <= 0)
        return "{\"error\": \"Item not found: " + name + "\"}";

    auto recipeIds = m_gw2->SearchRecipesByOutput(itemId);
    if (recipeIds.empty())
        return "{\"error\": \"No recipe found for: " + name + "\"}";

    auto recipe = m_gw2->GetRecipe(recipeIds[0]);
    if (!recipe.found)
        return "{\"error\": \"Recipe details not available\"}";

    std::vector<int> ingredientIds;
    for (auto& ing : recipe.ingredients)
        ingredientIds.push_back(ing.itemId);

    auto items = m_gw2->GetItems(ingredientIds);
    auto prices = m_gw2->GetPrices(ingredientIds);

    json result;
    result["recipe_id"] = recipe.id;
    result["output_item_id"] = recipe.outputItemId;
    result["output_count"] = recipe.outputCount;
    result["min_rating"] = recipe.minRating;
    result["disciplines"] = recipe.disciplines;
    if (!recipe.chatLink.empty())
        result["chat_link"] = recipe.chatLink;

    json ingredients = json::array();
    int totalCost = 0;
    for (auto& ing : recipe.ingredients) {
        json ingJ;
        ingJ["item_id"] = ing.itemId;
        ingJ["count"] = ing.count;

        for (auto& item : items) {
            if (item.id == ing.itemId) {
                ingJ["name"] = item.name;
                m_index->Add(item.name, item.id);
                break;
            }
        }
        for (auto& p : prices) {
            if (p.itemId == ing.itemId && p.found) {
                int cost = p.sellPrice * ing.count;
                ingJ["unit_price"] = GW2Client::FormatPrice(p.sellPrice);
                ingJ["total_price"] = GW2Client::FormatPrice(cost);
                totalCost += cost;
                break;
            }
        }
        ingredients.push_back(ingJ);
    }

    result["ingredients"] = ingredients;
    result["estimated_craft_cost"] = GW2Client::FormatPrice(totalCost);

    auto outputPrice = m_gw2->GetPrice(recipe.outputItemId);
    if (outputPrice.found) {
        result["output_tp_sell"] = GW2Client::FormatPrice(outputPrice.sellPrice);
        int profit = outputPrice.sellPrice - totalCost;
        int profitAfterTax = static_cast<int>(outputPrice.sellPrice * 0.85) - totalCost;
        result["profit_before_tax"] = GW2Client::FormatPrice(profit);
        result["profit_after_tax"] = GW2Client::FormatPrice(profitAfterTax);
    }

    return result.dump();
}

json FunctionHandler::ExtractCollectionItems(const std::string& html) {
    json items = json::array();
    auto rows = WikiText::ExtractTableRows(html, "mech1");
    for (auto& row : rows) {
        if (row.cells.size() < 2) continue;
        const std::string& first = row.cells[0];
        bool numeric = !first.empty() &&
            std::all_of(first.begin(), first.end(), [](unsigned char c) { return std::isdigit(c); });
        if (!numeric) continue;

        json item;
        item["name"] = row.cells[1];
        std::string hint;
        for (size_t i = row.cells.size(); i-- > 2;) {
            if (row.cells[i].rfind("Hint:", 0) == 0) {
                hint = TrimStr(row.cells[i].substr(5));
                break;
            }
        }
        if (!hint.empty()) item["hint"] = hint;
        items.push_back(item);
    }
    return items;
}

std::string FunctionHandler::HandleWiki(const json& args, const CancelCheck& cancel) {
    std::string query = args.value("query", "");
    std::string sectionWanted = TrimStr(args.value("section", ""));
    if (query.empty()) return "{\"error\": \"query parameter required\"}";

    auto results = m_gw2->WikiSearch(query, 3);
    if (results.empty()) {
        json err;
        err["error"] = "No wiki page matched: " + query;
        err["hint"] = "The wiki search is title-based. Retry with the exact English page title only "
                      "(singular, no extra words such as 'GW2', 'Guild Wars 2', 'location', 'farm', 'guide'), "
                      "or with the single most specific noun from the question (e.g. 'Leviathan').";
        return err.dump();
    }
    if (Cancelled(cancel)) return CANCELLED_JSON;

    std::string title = results[0].title;
    auto wikiPage = m_gw2->WikiGetPage(title);
    if (!wikiPage.found)
        return "{\"error\": \"Wiki page not found: " + title + "\"}";

    if (wikiPage.wikitext.rfind("#REDIRECT", 0) == 0) {
        auto lb = wikiPage.wikitext.find("[[");
        auto rb = wikiPage.wikitext.find("]]");
        if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
            std::string target = wikiPage.wikitext.substr(lb + 2, rb - lb - 2);
            auto hash = target.find('#');
            if (hash != std::string::npos) target = target.substr(0, hash);
            auto redirected = m_gw2->WikiGetPage(target);
            if (redirected.found) { wikiPage = redirected; title = redirected.title; }
        }
    }
    if (Cancelled(cancel)) return CANCELLED_JSON;

    auto htmlPage = m_gw2->WikiGetPageHtml(title);
    if (Cancelled(cancel)) return CANCELLED_JSON;

    json result;
    result["title"] = wikiPage.title;
    result["url"] = "https://wiki.guildwars2.com/wiki/" + wikiPage.title;

    std::string content;
    json sectionNames = json::array();

    if (htmlPage.found && !htmlPage.html.empty()) {
        std::string text = WikiText::HtmlToText(htmlPage.html);
        std::vector<WikiSection> sections;
        std::string lead = WikiText::SplitLead(text, sections);
        for (auto& s : sections) sectionNames.push_back(s.title);

        if (!sectionWanted.empty()) {
            std::string want = LowerStr(sectionWanted);
            for (auto& s : sections) {
                if (LowerStr(s.title) == want) {
                    content = "## " + s.title + "\n" + s.body;
                    if (content.size() > WIKI_TEXT_BUDGET)
                        content = content.substr(0, WIKI_TEXT_BUDGET) + "\n... (truncated)";
                    result["section"] = s.title;
                    break;
                }
            }
            if (content.empty())
                result["section_error"] = "Section not found: " + sectionWanted;
        }

        if (content.empty()) {
            content = lead;
            for (auto& s : sections) {
                if (content.size() >= WIKI_TEXT_BUDGET) break;
                std::string chunk = "\n\n## " + s.title + "\n" + s.body;
                if (content.size() + chunk.size() > WIKI_TEXT_BUDGET) {
                    size_t room = WIKI_TEXT_BUDGET - content.size();
                    content += chunk.substr(0, room);
                    content += "\n... (truncated - call gw2_wiki with section='" + s.title + "' for the rest)";
                    break;
                }
                content += chunk;
            }
        }

        auto ownItems = ExtractCollectionItems(htmlPage.html);
        if (!ownItems.empty()) {
            result["items"] = ownItems;
            result["item_count"] = ownItems.size();
        }
    } else {
        content = wikiPage.wikitext.substr(0, WIKI_TEXT_BUDGET);
        result["content_source"] = "wikitext_fallback";
    }

    result["content"] = content;
    if (!sectionNames.empty()) result["sections"] = sectionNames;

    if (results.size() > 1) {
        json related = json::array();
        for (size_t i = 1; i < results.size(); ++i)
            related.push_back(results[i].title);
        result["related"] = related;
    }
    if (results[0].fulltext) {
        result["search_mode"] = "fulltext";
        result["search_note"] = "No page title matched the query; this is the best full-text match. "
                                "Check that it answers the question - if not, call gw2_wiki with an exact page title "
                                "(see 'related').";
    }

    std::string idStr = GW2Client::ExtractItemIdFromWikitext(wikiPage.wikitext);
    if (!idStr.empty()) {
        int id = std::stoi(idStr);
        result["item_id"] = id;
        m_index->Add(wikiPage.title, id);
    }

    auto subNames = WikiText::ExtractSubCollectionNames(wikiPage.wikitext, WIKI_SUBPAGE_CAP);
    json subs = json::array();
    std::string selfLower = LowerStr(wikiPage.title);
    for (auto& name : subNames) {
        if (Cancelled(cancel)) return CANCELLED_JSON;
        if (LowerStr(name) == selfLower) continue;
        auto sub = m_gw2->WikiGetPageHtml(name);
        if (!sub.found || sub.html.empty()) continue;
        auto items = ExtractCollectionItems(sub.html);
        if (items.empty()) continue;
        json s;
        s["name"] = sub.title;
        s["item_count"] = items.size();
        s["items"] = items;
        subs.push_back(s);
    }
    if (!subs.empty()) result["sub_collections"] = subs;

    std::string lead = LeadOf(wikiPage.wikitext);
    std::vector<std::string> areas = SplitLocations(InfoboxField(lead, "location"), 4);
    double npcX = 0, npcY = 0;
    bool hasNpcCoord = ParseCoordinates(InfoboxField(lead, "coordinates"), npcX, npcY);

    if (!areas.empty()) {
        result["location_areas"] = areas;

        struct LocEntry {
            int mapId = 0;
            std::string mapName;
            std::vector<std::string> areas;
            bool npcHere = false;
            json waypoints;
        };
        std::vector<LocEntry> entries;
        json otherAreas = json::array();

        for (auto& area : areas) {
            if (Cancelled(cancel)) return CANCELLED_JSON;
            int mapId = ResolveMapId(area);
            if (mapId <= 0) { otherAreas.push_back(area); continue; }

            bool merged = false;
            for (auto& e : entries)
                if (e.mapId == mapId) { e.areas.push_back(area); merged = true; break; }
            if (merged) continue;
            if (entries.size() >= 3) { otherAreas.push_back(area); continue; }

            auto mapInfo = m_gw2->GetMapWithWaypoints(mapId);
            if (!mapInfo.found || mapInfo.waypoints.empty()) continue;

            LocEntry e;
            e.mapId = mapId;
            e.mapName = mapInfo.name;
            e.areas.push_back(area);
            e.npcHere = hasNpcCoord && InContinentRect(mapInfo, npcX, npcY);
            e.waypoints = BuildWaypointList(mapInfo.waypoints, e.npcHere, npcX, npcY);
            entries.push_back(std::move(e));
        }

        std::stable_sort(entries.begin(), entries.end(),
                         [](const LocEntry& a, const LocEntry& b) { return a.npcHere && !b.npcHere; });

        if (!entries.empty()) {
            json locs = json::array();
            for (auto& e : entries) {
                json l;
                l["map_name"] = e.mapName;
                l["map_id"] = e.mapId;
                l["areas"] = e.areas;
                l["npc_here"] = e.npcHere;
                l["waypoints"] = e.waypoints;
                locs.push_back(l);
            }
            result["locations"] = locs;
            result["map_name"] = entries[0].mapName;
            result["location_area"] = entries[0].areas[0];
            result["nearby_waypoints"] = entries[0].waypoints;
        }
        if (!otherAreas.empty()) result["other_locations"] = otherAreas;
    }

    return result.dump();
}

std::string FunctionHandler::HandleGuide(const json& args, const CancelCheck& cancel) {
    std::string query = args.value("query", "");
    std::string sectionWanted = TrimStr(args.value("section", ""));
    if (query.empty()) return "{\"error\": \"query parameter required\"}";

    auto results = m_gw2->GuideSearch(query, 5);
    if (results.empty())
        return "{\"error\": \"No guide found for: " + query + "\"}";
    if (Cancelled(cancel)) return CANCELLED_JSON;

    auto& top = results[0];
    auto page = m_gw2->GuideGetContent(top.selfHref);
    if (!page.found)
        return "{\"error\": \"Guide content not available: " + top.title + "\"}";
    if (Cancelled(cancel)) return CANCELLED_JSON;

    std::string text = WikiText::HtmlToText(page.html);

    // WordPress uses <h1> for top-level sections; normalize to ## so SplitLead finds them.
    std::string normalized;
    size_t pos = 0;
    while (pos <= text.size()) {
        auto nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
        if (line.size() > 2 && line.compare(0, 2, "# ") == 0 && (line.size() < 3 || line[2] != '#'))
            normalized += "## " + line.substr(2) + "\n";
        else
            normalized += line + "\n";
    }
    text = normalized;

    std::vector<WikiSection> sections;
    std::string lead = WikiText::SplitLead(text, sections);
    json sectionNames = json::array();
    for (auto& s : sections) sectionNames.push_back(s.title);

    std::string content;
    if (!sectionWanted.empty()) {
        std::string want = LowerStr(sectionWanted);
        for (auto& s : sections) {
            if (LowerStr(s.title) == want) {
                content = "## " + s.title + "\n" + s.body;
                if (content.size() > GUIDE_TEXT_BUDGET)
                    content = content.substr(0, GUIDE_TEXT_BUDGET) + "\n... (truncated)";
                break;
            }
        }
        if (content.empty())
            return "{\"error\": \"Section not found: " + sectionWanted + "\"}";
    }

    if (content.empty()) {
        content = lead;
        for (auto& s : sections) {
            if (content.size() >= GUIDE_TEXT_BUDGET) break;
            std::string chunk = "\n\n## " + s.title + "\n" + s.body;
            if (content.size() + chunk.size() > GUIDE_TEXT_BUDGET) {
                size_t room = GUIDE_TEXT_BUDGET - content.size();
                content += chunk.substr(0, room);
                content += "\n... (truncated - call gw2_guide with section='" + s.title + "' for the rest)";
                break;
            }
            content += chunk;
        }
    }

    json result;
    result["source"] = "guildjen";
    result["title"] = page.title;
    result["url"] = page.url;
    if (!page.modified.empty())
        result["modified"] = page.modified.substr(0, 10);
    result["content"] = content;
    if (!sectionNames.empty()) result["sections"] = sectionNames;

    if (results.size() > 1) {
        json related = json::array();
        for (size_t i = 1; i < results.size(); ++i)
            related.push_back(results[i].title);
        result["related"] = related;
    }

    return result.dump();
}

