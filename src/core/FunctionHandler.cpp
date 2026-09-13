#include "FunctionHandler.h"
#include <algorithm>

using json = nlohmann::json;

FunctionHandler::FunctionHandler(GW2Client* gw2, ItemIndex* index)
    : m_gw2(gw2), m_index(index) {}

FunctionResult FunctionHandler::Handle(const FunctionCall& call) {
    FunctionResult result;
    result.callId = call.id;
    result.name = call.name;

    if (call.name == "gw2_item_info")
        result.resultText = HandleItemInfo(call.arguments);
    else if (call.name == "gw2_recipe")
        result.resultText = HandleRecipe(call.arguments);
    else if (call.name == "gw2_map")
        result.resultText = HandleMap(call.arguments);
    else if (call.name == "gw2_wiki")
        result.resultText = HandleWiki(call.arguments);
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
        {"description", "Search the GW2 Wiki and return page content. Use for NPC locations, event info, achievement guides, and general game knowledge not covered by other tools. Do NOT use this for waypoint codes - use gw2_map instead."},
        {"parameters", {
            {"type", "object"},
            {"properties", {
                {"query", {
                    {"type", "string"},
                    {"description", "Search query in English (e.g. 'Miyani', 'Shadow Behemoth', 'Ascended armor')"}
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
    auto results = m_gw2->WikiSearch(name, 5);
    if (results.empty()) return 0;

    for (auto& r : results) {
        auto page = m_gw2->WikiGetPage(r.title);
        if (!page.found) continue;

        bool isMapPage = page.wikitext.find("Location infobox") != std::string::npos
                      || page.wikitext.find("location infobox") != std::string::npos;
        if (!isMapPage) continue;

        std::string idStr = GW2Client::ExtractItemIdFromWikitext(page.wikitext);
        if (!idStr.empty())
            return std::stoi(idStr);
    }
    return 0;
}

std::string FunctionHandler::HandleMap(const json& args) {
    std::string name = args.value("name", "");
    if (name.empty()) return "{\"error\": \"name parameter required\"}";

    int mapId = ResolveMapId(name);
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

std::string FunctionHandler::HandleItemInfo(const json& args) {
    std::string name = args.value("name", "");
    if (name.empty()) return "{\"error\": \"name parameter required\"}";

    int itemId = ResolveItemId(name);
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

std::string FunctionHandler::HandleRecipe(const json& args) {
    std::string name = args.value("name", "");
    if (name.empty()) return "{\"error\": \"name parameter required\"}";

    int itemId = ResolveItemId(name);
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

std::string FunctionHandler::HandleWiki(const json& args) {
    std::string query = args.value("query", "");
    if (query.empty()) return "{\"error\": \"query parameter required\"}";

    auto results = m_gw2->WikiSearch(query, 3);
    if (results.empty())
        return "{\"error\": \"No wiki results for: " + query + "\"}";

    auto page = m_gw2->WikiGetPage(results[0].title);
    if (!page.found)
        return "{\"error\": \"Wiki page not found: " + results[0].title + "\"}";

    json result;
    result["title"] = page.title;
    result["url"] = results[0].url;
    result["content"] = TruncateWikitext(page.wikitext);

    if (results.size() > 1) {
        json related = json::array();
        for (size_t i = 1; i < results.size(); ++i)
            related.push_back(results[i].title);
        result["related"] = related;
    }

    std::string idStr = GW2Client::ExtractItemIdFromWikitext(page.wikitext);
    if (!idStr.empty()) {
        int id = std::stoi(idStr);
        result["item_id"] = id;
        m_index->Add(page.title, id);
    }

    return result.dump();
}

std::string FunctionHandler::TruncateWikitext(const std::string& wikitext, size_t maxBytes) {
    if (wikitext.size() <= maxBytes) return wikitext;

    std::string result;
    auto extractSection = [&](const std::string& header) {
        std::string h2 = "==" + header + "==";
        std::string h2s = "== " + header + " ==";
        auto pos = wikitext.find(h2);
        if (pos == std::string::npos) pos = wikitext.find(h2s);
        if (pos == std::string::npos) return;

        auto end = wikitext.find("\n==", pos + h2.size());
        if (end == std::string::npos) end = wikitext.size();
        std::string section = wikitext.substr(pos, std::min(end - pos, (size_t)1500));
        if (!section.empty()) {
            result += "\n" + section + "\n";
        }
    };

    auto firstSection = wikitext.find("\n==");
    if (firstSection == std::string::npos) firstSection = wikitext.size();
    std::string lead = wikitext.substr(0, std::min(firstSection, (size_t)1500));
    result = lead;

    extractSection("Location");
    extractSection("Locations");
    extractSection("Acquisition");
    extractSection("Walkthrough");
    extractSection("Contents");
    extractSection("Notes");

    if (result.size() > maxBytes)
        result = result.substr(0, maxBytes) + "\n... (truncated)";

    return result;
}
