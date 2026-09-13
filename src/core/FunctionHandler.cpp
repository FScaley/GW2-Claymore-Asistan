#include "FunctionHandler.h"
#include "WikiText.h"
#include <algorithm>
#include <cctype>

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
        {"description", "Search the GW2 Wiki and return the page as readable text plus a 'sections' list. For mounts, legendaries, collections and achievements the result also includes 'sub_collections': the COMPLETE item list of every linked collection, extracted directly from the wiki tables — always relay those lists verbatim. If the page itself is a collection, its items are in 'items'. Pass 'section' (a name from the 'sections' list) to fetch one specific section in full when the default content was truncated. Do NOT use this for waypoint codes - use gw2_map instead."},
        {"parameters", {
            {"type", "object"},
            {"properties", {
                {"query", {
                    {"type", "string"},
                    {"description", "Page name or search query in English (e.g. 'Roller Beetle', 'Miyani', 'Shadow Behemoth')"}
                }},
                {"section", {
                    {"type", "string"},
                    {"description", "Optional. Exact section title from a previous result's 'sections' list (e.g. 'Unlocking', 'Acquisition') to fetch that section in full."}
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

int FunctionHandler::ResolveMapId(const std::string& name) {
    int cached = m_index->Find("map:" + name);
    if (cached > 0) return cached;

    auto results = m_gw2->WikiSearch(name, 5);
    if (results.empty()) return 0;

    for (auto& r : results) {
        auto page = m_gw2->WikiGetPage(r.title);
        if (!page.found) continue;

        bool isMapPage = page.wikitext.find("Location infobox") != std::string::npos
                      || page.wikitext.find("location infobox") != std::string::npos;
        if (!isMapPage) continue;

        std::string idStr = GW2Client::ExtractItemIdFromWikitext(page.wikitext);
        if (!idStr.empty()) {
            int id = std::stoi(idStr);
            m_index->Add("map:" + name, id);
            m_index->Add("map:" + r.title, id);
            return id;
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
    if (results.empty())
        return "{\"error\": \"No wiki results for: " + query + "\"}";
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

    std::string locationMap;
    auto firstHeader = wikiPage.wikitext.find("\n==");
    size_t leadEnd = (firstHeader != std::string::npos) ? firstHeader : wikiPage.wikitext.size();
    std::string lead = wikiPage.wikitext.substr(0, leadEnd);
    auto locPos = lead.find("| location");
    if (locPos == std::string::npos) locPos = lead.find("|location");
    if (locPos != std::string::npos) {
        auto eq = lead.find('=', locPos);
        if (eq != std::string::npos) {
            auto lb = lead.find("[[", eq);
            auto rb = lead.find("]]", eq);
            auto nl = lead.find('\n', eq);
            if (lb != std::string::npos && rb != std::string::npos && rb > lb
                && (nl == std::string::npos || lb < nl)) {
                locationMap = lead.substr(lb + 2, rb - lb - 2);
                auto pipe = locationMap.find('|');
                if (pipe != std::string::npos) locationMap = locationMap.substr(0, pipe);
            }
        }
    }

    if (!locationMap.empty()) {
        if (Cancelled(cancel)) return CANCELLED_JSON;
        int mapId = ResolveMapId(locationMap);
        if (mapId > 0) {
            auto mapInfo = m_gw2->GetMapWithWaypoints(mapId);
            if (mapInfo.found && !mapInfo.waypoints.empty()) {
                result["map_name"] = mapInfo.name;
                json waypoints = json::array();
                for (auto& wp : mapInfo.waypoints) {
                    json w;
                    w["name"] = wp.name;
                    w["chat_link"] = wp.chatLink;
                    waypoints.push_back(w);
                }
                result["nearby_waypoints"] = waypoints;
            }
        }
    }

    return result.dump();
}

