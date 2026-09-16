#include "FunctionHandler.h"
#include "WikiText.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <set>

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

struct InteractiveMapMarker {
    std::string name;       // "name" field (entity/item name)
    std::string mapName;    // from "text": "[[MapName]]" — may be empty for collections
    std::string text;       // raw "text" field
    double cx = 0, cy = 0;
};

struct InteractiveMapData {
    std::vector<InteractiveMapMarker> markers;
    std::vector<int> mapIds;
};

static InteractiveMapData ParseInteractiveMapData(const std::string& wikitext) {
    InteractiveMapData data;
    size_t pos = 0;
    while (pos < wikitext.size()) {
        auto found = wikitext.find("{{interactive map", pos);
        if (found == std::string::npos) {
            found = wikitext.find("{{Interactive map", pos);
            if (found == std::string::npos) break;
        }
        pos = found;
        size_t depth = 1;
        size_t scan = pos + 2;
        size_t end = pos;
        while (scan < wikitext.size() - 1 && depth > 0) {
            if (wikitext[scan] == '{' && wikitext[scan + 1] == '{') { ++depth; ++scan; }
            else if (wikitext[scan] == '}' && wikitext[scan + 1] == '}') { --depth; if (depth == 0) { end = scan; break; } ++scan; }
            else ++scan;
        }
        std::string block = wikitext.substr(pos, end + 2 - pos);
        pos = end + 2;

        // Parse | map = 18,20,21,... (map ID list)
        auto mapField = block.find("| map");
        if (mapField == std::string::npos) mapField = block.find("|map");
        if (mapField != std::string::npos) {
            auto eq = block.find('=', mapField);
            if (eq != std::string::npos) {
                auto lineEnd = block.find('|', eq + 1);
                if (lineEnd == std::string::npos) lineEnd = block.size();
                std::string mapList = block.substr(eq + 1, lineEnd - eq - 1);
                size_t mp = 0;
                while (mp < mapList.size()) {
                    while (mp < mapList.size() && !std::isdigit((unsigned char)mapList[mp])) ++mp;
                    if (mp >= mapList.size()) break;
                    size_t numStart = mp;
                    while (mp < mapList.size() && std::isdigit((unsigned char)mapList[mp])) ++mp;
                    try {
                        int id = std::stoi(mapList.substr(numStart, mp - numStart));
                        bool dup = false;
                        for (int existing : data.mapIds) if (existing == id) { dup = true; break; }
                        if (!dup) data.mapIds.push_back(id);
                    } catch (...) {}
                }
            }
        }

        // Extract markers: { "coord": [x, y], "name": "...", "text": "..." }
        size_t mpos = 0;
        while (mpos < block.size()) {
            auto lb = block.find("\"coord\"", mpos);
            if (lb == std::string::npos) break;
            auto bracket = block.find('[', lb + 7);
            if (bracket == std::string::npos) break;
            auto rbracket = block.find(']', bracket + 1);
            if (rbracket == std::string::npos) break;
            std::string coordStr = block.substr(bracket + 1, rbracket - bracket - 1);

            double cx = 0, cy = 0;
            auto comma = coordStr.find(',');
            bool coordOk = false;
            if (comma != std::string::npos) {
                try {
                    cx = std::stod(TrimStr(coordStr.substr(0, comma)));
                    cy = std::stod(TrimStr(coordStr.substr(comma + 1)));
                    coordOk = true;
                } catch (...) {}
            }

            // Extract "name" field
            std::string markerName;
            auto nameKey = block.find("\"name\"", lb);
            auto nextCoord = block.find("\"coord\"", rbracket);
            if (nameKey != std::string::npos && (nextCoord == std::string::npos || nameKey < nextCoord)) {
                auto q1 = block.find('"', nameKey + 6);
                auto q2 = block.find('"', q1 != std::string::npos ? q1 + 1 : 0);
                if (q1 != std::string::npos && q2 != std::string::npos)
                    markerName = block.substr(q1 + 1, q2 - q1 - 1);
            }

            // Extract "text" field and try to get [[MapName]] from it
            std::string mapName, textField;
            auto textKey = block.find("\"text\"", lb);
            if (textKey != std::string::npos && (nextCoord == std::string::npos || textKey < nextCoord)) {
                auto q1 = block.find('"', textKey + 6);
                auto q2 = block.find('"', q1 != std::string::npos ? q1 + 1 : 0);
                if (q1 != std::string::npos && q2 != std::string::npos)
                    textField = block.substr(q1 + 1, q2 - q1 - 1);
                auto dblBracket = textField.find("[[");
                auto dblClose = textField.find("]]", dblBracket != std::string::npos ? dblBracket : 0);
                if (dblBracket != std::string::npos && dblClose != std::string::npos)
                    mapName = textField.substr(dblBracket + 2, dblClose - dblBracket - 2);
            }

            if (coordOk)
                data.markers.push_back({markerName, mapName, textField, cx, cy});

            mpos = rbracket + 1;
        }
    }
    return data;
}

static std::vector<std::string> FindEventLinks(const std::string& wikitext) {
    std::vector<std::string> links;

    // GW2 wiki uses {{event|Event Name}} template, not [[Event Name]] links
    size_t pos = 0;
    while (pos < wikitext.size()) {
        auto ev = wikitext.find("{{event|", pos);
        if (ev == std::string::npos) break;
        size_t nameStart = ev + 8;
        auto rb = wikitext.find("}}", nameStart);
        if (rb == std::string::npos) break;
        std::string name = TrimStr(wikitext.substr(nameStart, rb - nameStart));
        auto pipe = name.find('|');
        if (pipe != std::string::npos) name = name.substr(0, pipe);
        name = TrimStr(name);
        if (!name.empty()) {
            bool dup = false;
            for (auto& existing : links) if (existing == name) { dup = true; break; }
            if (!dup) links.push_back(name);
        }
        pos = rb + 2;
    }

    // Also check {{see|EventName#...}} pattern
    pos = 0;
    while (pos < wikitext.size()) {
        auto see = wikitext.find("{{see|", pos);
        if (see == std::string::npos) break;
        size_t nameStart = see + 6;
        auto rb = wikitext.find("}}", nameStart);
        if (rb == std::string::npos) break;
        std::string name = TrimStr(wikitext.substr(nameStart, rb - nameStart));
        auto pipe = name.find('|');
        if (pipe != std::string::npos) name = name.substr(0, pipe);
        auto hash = name.find('#');
        if (hash != std::string::npos) name = name.substr(0, hash);
        name = TrimStr(name);
        if (!name.empty() && name.size() > 8) {
            bool dup = false;
            for (auto& existing : links) if (existing == name) { dup = true; break; }
            if (!dup) links.push_back(name);
        }
        pos = rb + 2;
    }

    return links;
}

static std::vector<std::pair<double, double>> ParseMultiCoordinates(const std::string& value) {
    std::vector<std::pair<double, double>> coords;
    size_t pos = 0;
    while (pos < value.size()) {
        auto lb = value.find('[', pos);
        if (lb == std::string::npos) break;
        auto rb = value.find(']', lb + 1);
        if (rb == std::string::npos) break;
        std::string inner = value.substr(lb + 1, rb - lb - 1);
        auto comma = inner.find(',');
        if (comma != std::string::npos) {
            try {
                double x = std::stod(TrimStr(inner.substr(0, comma)));
                double y = std::stod(TrimStr(inner.substr(comma + 1)));
                coords.push_back({x, y});
            } catch (...) {}
        }
        pos = rb + 1;
    }
    return coords;
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

static std::string FindTemplateBlock(const std::string& text, const std::string& name) {
    std::string lower = LowerStr(text);
    std::string target = "{{" + LowerStr(name);
    size_t pos = lower.find(target);
    if (pos == std::string::npos) return "";
    size_t afterName = pos + 2 + name.size();
    if (afterName < text.size()) {
        char c = text[afterName];
        if (c != '|' && c != '\n' && c != '\r' && c != ' ' && c != '}') return "";
    }
    size_t i = pos + 2;
    int depth = 1;
    while (i < text.size()) {
        if (text[i] == '{' && i + 1 < text.size() && text[i + 1] == '{') { depth++; i += 2; continue; }
        if (text[i] == '}' && i + 1 < text.size() && text[i + 1] == '}') {
            depth--;
            if (depth == 0) return text.substr(pos + 2, i - pos - 2);
            i += 2;
            continue;
        }
        i++;
    }
    return "";
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
    else if (call.name == "gw2_build")
        result.resultText = HandleBuild(call.arguments, shouldCancel);
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

    tools.push_back({
        {"type", "function"},
        {"name", "gw2_build"},
        {"description", "Search metabattle.com for GW2 builds by class or elite specialization. Returns build metadata (rating, game mode, role), the in-game template code (paste-ready [&D...] chat link), and a usage guide. The result includes 'alternatives' — other builds for the same class. Use for specific build/gear/trait questions. NOT for 'which class should I play' (answer those directly) or collection/achievement item lists (use gw2_wiki)."},
        {"parameters", {
            {"type", "object"},
            {"properties", {
                {"query", {
                    {"type", "string"},
                    {"description", "Class or elite specialization name in English ONLY (e.g. 'firebrand', 'guardian', 'dragonhunter', 'mechanist'). Do NOT include game mode words like 'wvw', 'pvp', 'pve' here - put those in the 'mode' parameter instead. The search is class/spec-based: extra words return nothing."}
                }},
                {"mode", {
                    {"type", "string"},
                    {"description", "Optional game mode filter. Values from metabattle: 'pve', 'pvp', 'wvw', 'open world', 'raid', 'fractal', 'wvw zerg', 'wvw roaming'. Matches as substring of the build's 'designed for' field."}
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

    std::string locationsSectionBody;

    if (htmlPage.found && !htmlPage.html.empty()) {
        std::string text = WikiText::HtmlToText(htmlPage.html);
        std::vector<WikiSection> sections;
        std::string lead = WikiText::SplitLead(text, sections);
        for (auto& s : sections) {
            sectionNames.push_back(s.title);
            if (LowerStr(s.title) == "locations") locationsSectionBody = s.body;
        }

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
    auto imapData = ParseInteractiveMapData(wikiPage.wikitext);

    if (!areas.empty()) {
        result["location_areas"] = areas;

        struct LocEntry {
            int mapId = 0;
            std::string mapName;
            std::vector<std::string> areas;
            bool npcHere = false;
            json waypoints;
            double contRect[4] = {};
            double mapRect[4] = {};
            bool hasRects = false;
            std::vector<GW2Sector> sectors;
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
            if (entries.size() >= LOCATION_MAP_CAP) { otherAreas.push_back(area); continue; }

            auto mapInfo = m_gw2->GetMapWithWaypoints(mapId);
            if (!mapInfo.found || mapInfo.waypoints.empty()) continue;

            LocEntry e;
            e.mapId = mapId;
            e.mapName = mapInfo.name;
            e.areas.push_back(area);
            e.npcHere = hasNpcCoord && InContinentRect(mapInfo, npcX, npcY);
            e.waypoints = BuildWaypointList(mapInfo.waypoints, e.npcHere, npcX, npcY);
            e.sectors = mapInfo.sectors;
            if (mapInfo.hasContRect && mapInfo.hasMapRect) {
                e.contRect[0] = mapInfo.contRect[0][0]; e.contRect[1] = mapInfo.contRect[0][1];
                e.contRect[2] = mapInfo.contRect[1][0]; e.contRect[3] = mapInfo.contRect[1][1];
                e.mapRect[0] = mapInfo.mapRect[0][0]; e.mapRect[1] = mapInfo.mapRect[0][1];
                e.mapRect[2] = mapInfo.mapRect[1][0]; e.mapRect[3] = mapInfo.mapRect[1][1];
                e.hasRects = true;
            }
            entries.push_back(std::move(e));
        }

        // Supplement from the Locations section text: maps listed there but missing from
        // the infobox get real waypoints.  The section lists regions then "- Map" / "- Area".
        if (!locationsSectionBody.empty()) {
            std::string curMap;
            size_t lpos = 0;
            while (lpos <= locationsSectionBody.size() && entries.size() < LOCATION_MAP_CAP) {
                if (Cancelled(cancel)) return CANCELLED_JSON;
                auto lnl = locationsSectionBody.find('\n', lpos);
                std::string ln = locationsSectionBody.substr(lpos,
                    lnl == std::string::npos ? std::string::npos : lnl - lpos);
                lpos = (lnl == std::string::npos) ? locationsSectionBody.size() + 1 : lnl + 1;
                ln = TrimStr(ln);
                if (ln.empty()) { curMap.clear(); continue; }
                if (ln.size() > 2 && ln.compare(0, 2, "- ") == 0) {
                    std::string name = TrimStr(ln.substr(2));
                    auto dash = name.find(" \xe2\x80\x94 ");
                    if (dash == std::string::npos) dash = name.find(" -- ");
                    if (dash != std::string::npos) name = TrimStr(name.substr(0, dash));
                    if (curMap.empty()) {
                        curMap = name;
                        bool already = false;
                        for (auto& e : entries)
                            if (LowerStr(e.mapName) == LowerStr(curMap)) { already = true; break; }
                        if (already) continue;
                        int mapId = ResolveMapId(curMap);
                        if (mapId <= 0) continue;
                        bool dup = false;
                        for (auto& e : entries) if (e.mapId == mapId) { dup = true; break; }
                        if (dup) continue;
                        auto mapInfo = m_gw2->GetMapWithWaypoints(mapId);
                        if (!mapInfo.found || mapInfo.waypoints.empty()) continue;
                        LocEntry e;
                        e.mapId = mapId;
                        e.mapName = mapInfo.name;
                        e.npcHere = hasNpcCoord && InContinentRect(mapInfo, npcX, npcY);
                        e.waypoints = BuildWaypointList(mapInfo.waypoints, e.npcHere, npcX, npcY);
                        e.sectors = mapInfo.sectors;
                        if (mapInfo.hasContRect && mapInfo.hasMapRect) {
                            e.contRect[0] = mapInfo.contRect[0][0]; e.contRect[1] = mapInfo.contRect[0][1];
                            e.contRect[2] = mapInfo.contRect[1][0]; e.contRect[3] = mapInfo.contRect[1][1];
                            e.mapRect[0] = mapInfo.mapRect[0][0]; e.mapRect[1] = mapInfo.mapRect[0][1];
                            e.mapRect[2] = mapInfo.mapRect[1][0]; e.mapRect[3] = mapInfo.mapRect[1][1];
                            e.hasRects = true;
                        }
                        entries.push_back(std::move(e));
                    } else {
                        for (auto& e : entries)
                            if (LowerStr(e.mapName) == LowerStr(curMap)) { e.areas.push_back(name); break; }
                    }
                } else {
                    curMap.clear();
                }
            }
        }

        std::stable_sort(entries.begin(), entries.end(),
                         [](const LocEntry& a, const LocEntry& b) { return a.npcHere && !b.npcHere; });

        // Entity coord side-channel: populate for marker overlay
        m_entityCoords.clear();
        m_mapRects.clear();
        for (auto& e : entries) {
            if (e.hasRects) {
                MapRects mr;
                std::copy(e.contRect, e.contRect + 4, mr.contRect);
                std::copy(e.mapRect, e.mapRect + 4, mr.mapRect);
                m_mapRects[e.mapId] = mr;
            }

            bool foundExact = false;

            // Priority 1: {{interactive map}} per-map coordinates
            for (auto& im : imapData.markers) {
                if (!im.mapName.empty() && LowerStr(im.mapName) == LowerStr(e.mapName)) {
                    EntityCoord ec;
                    ec.name = wikiPage.title;
                    ec.mapId = e.mapId;
                    ec.mapName = e.mapName;
                    ec.areas = e.areas;
                    ec.cx = im.cx;
                    ec.cy = im.cy;
                    ec.hasCoord = true;
                    ec.source = EntityCoord::Exact;
                    m_entityCoords.push_back(std::move(ec));
                    foundExact = true;
                    break;
                }
            }

            // Priority 2: wiki infobox | coordinates (npcHere)
            if (!foundExact && e.npcHere && hasNpcCoord) {
                EntityCoord ec;
                ec.name = wikiPage.title;
                ec.mapId = e.mapId;
                ec.mapName = e.mapName;
                ec.areas = e.areas;
                ec.cx = npcX;
                ec.cy = npcY;
                ec.hasCoord = true;
                ec.source = EntityCoord::Exact;
                m_entityCoords.push_back(std::move(ec));
                foundExact = true;
            }

            // Priority 3: sector center per area (one entry per matched area)
            if (!foundExact && !e.sectors.empty()) {
                for (auto& area : e.areas) {
                    std::string la = LowerStr(area);
                    for (auto& sec : e.sectors) {
                        std::string ls = LowerStr(sec.name);
                        if (la == ls || ls.find(la) != std::string::npos || la.find(ls) != std::string::npos) {
                            EntityCoord ec;
                            ec.name = wikiPage.title;
                            ec.mapId = e.mapId;
                            ec.mapName = e.mapName;
                            ec.areas = {area};
                            ec.cx = sec.x;
                            ec.cy = sec.y;
                            ec.hasCoord = true;
                            ec.source = EntityCoord::Sector;
                            m_entityCoords.push_back(std::move(ec));
                            break;
                        }
                    }
                }
            }

            // Priority 4: no coord — HUD-only entry
            if (!foundExact) {
                bool hasSector = false;
                for (auto& ec : m_entityCoords)
                    if (ec.mapId == e.mapId && ec.source == EntityCoord::Sector) { hasSector = true; break; }
                if (!hasSector) {
                    EntityCoord ec;
                    ec.name = wikiPage.title;
                    ec.mapId = e.mapId;
                    ec.mapName = e.mapName;
                    ec.areas = e.areas;
                    m_entityCoords.push_back(std::move(ec));
                }
            }
        }

        // Event page hop: upgrade Sector entries with exact event spawn coordinates
        bool hasSector = false;
        for (auto& ec : m_entityCoords)
            if (ec.source == EntityCoord::Sector) { hasSector = true; break; }

        if (hasSector && !Cancelled(cancel)) {
            auto eventLinks = FindEventLinks(wikiPage.wikitext);
            for (auto& eventTitle : eventLinks) {
                if (Cancelled(cancel)) break;
                auto eventPage = m_gw2->WikiGetPage(eventTitle);
                if (!eventPage.found) continue;

                auto eventImapData = ParseInteractiveMapData(eventPage.wikitext);
                auto& eventMarkers = eventImapData.markers;
                std::string lead = LeadOf(eventPage.wikitext);
                auto eventCoords = ParseMultiCoordinates(InfoboxField(lead, "coordinates"));

                std::vector<std::pair<double, double>> candidates;
                for (auto& em : eventMarkers) candidates.push_back({em.cx, em.cy});
                for (auto& c : eventCoords) candidates.push_back(c);

                for (auto& [cx, cy] : candidates) {
                    // Determine which map this coordinate belongs to via continent_rect
                    int targetMapId = 0;
                    for (auto& [mid, mr] : m_mapRects) {
                        double x1 = std::min(mr.contRect[0], mr.contRect[2]);
                        double x2 = std::max(mr.contRect[0], mr.contRect[2]);
                        double y1 = std::min(mr.contRect[1], mr.contRect[3]);
                        double y2 = std::max(mr.contRect[1], mr.contRect[3]);
                        if (cx >= x1 && cx <= x2 && cy >= y1 && cy <= y2) { targetMapId = mid; break; }
                    }

                    double bestDist = 1e18;
                    EntityCoord* bestEc = nullptr;
                    for (auto& ec : m_entityCoords) {
                        if (ec.source != EntityCoord::Sector) continue;
                        if (targetMapId > 0 && ec.mapId != targetMapId) continue;
                        double dx = ec.cx - cx, dy = ec.cy - cy;
                        double d = dx * dx + dy * dy;
                        if (d < bestDist) { bestDist = d; bestEc = &ec; }
                    }
                    if (bestEc) {
                        bestEc->cx = cx;
                        bestEc->cy = cy;
                        bestEc->source = EntityCoord::Exact;
                    }
                }
            }
        }

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

    // Achievement page hop: if no entity coords yet, look for achievement/collection links
    // and check those pages for {{interactive map}}
    if (m_entityCoords.empty() && imapData.markers.empty() && !Cancelled(cancel)) {
        // Find achievement links: {{achievement icon|Name}}, {{achievement|Name}}, [[Name]] in achievment context
        std::vector<std::string> achieveLinks;
        size_t apos = 0;
        while (apos < wikiPage.wikitext.size()) {
            // Match {{achievement icon|X}} and {{achievement|X}}
            for (const char* pat : {"{{achievement icon|", "{{achievement|", "{{Achievement icon|", "{{Achievement|"}) {
                auto found = wikiPage.wikitext.find(pat, apos);
                if (found != std::string::npos && (apos == 0 || found == apos)) {
                    apos = found;
                    size_t nameStart = found + std::strlen(pat);
                    auto rb = wikiPage.wikitext.find("}}", nameStart);
                    if (rb != std::string::npos) {
                        std::string name = wikiPage.wikitext.substr(nameStart, rb - nameStart);
                        auto pipe = name.find('|');
                        if (pipe != std::string::npos) name = name.substr(0, pipe);
                        auto hash = name.find('#');
                        if (hash != std::string::npos) name = name.substr(0, hash);
                        name = TrimStr(name);
                        if (!name.empty() && name.size() > 3) {
                            bool dup = false;
                            for (auto& l : achieveLinks) if (l == name) { dup = true; break; }
                            if (!dup) achieveLinks.push_back(name);
                        }
                        apos = rb + 2;
                    } else { ++apos; }
                    goto nextAchieve;
                }
            }
            ++apos;
            nextAchieve:;
        }

        for (auto& achieveTitle : achieveLinks) {
            if (Cancelled(cancel)) break;
            if (achieveLinks.size() > 5) break;
            auto achievePage = m_gw2->WikiGetPage(achieveTitle);
            if (!achievePage.found) continue;
            auto achieveImap = ParseInteractiveMapData(achievePage.wikitext);
            if (!achieveImap.markers.empty()) {
                imapData = std::move(achieveImap);
                break;
            }
        }
    }

    // Coordinate search fallback: if no entity coords yet and no interactive map on this page,
    // search for a related page that HAS {{interactive map}} (insource: CirrusSearch).
    // Only use results whose title shares significant word overlap with the original query
    // to avoid showing markers from the wrong page.
    if (m_entityCoords.empty() && imapData.markers.empty() && !Cancelled(cancel)) {
        auto queryWords = std::vector<std::string>();
        {
            std::string lower = LowerStr(title);
            std::string word;
            for (char c : lower) {
                if (std::isalnum((unsigned char)c)) word += c;
                else { if (word.size() >= 3) queryWords.push_back(word); word.clear(); }
            }
            if (word.size() >= 3) queryWords.push_back(word);
        }

        auto imapHits = m_gw2->WikiSearchWithInteractiveMap(title, 3);
        for (auto& hit : imapHits) {
            if (Cancelled(cancel)) break;
            if (hit.title == title) continue;

            // At least one query word must appear in the hit title
            std::string hitLower = LowerStr(hit.title);
            bool anyMatch = false;
            for (auto& qw : queryWords)
                if (hitLower.find(qw) != std::string::npos) { anyMatch = true; break; }
            if (!anyMatch && !queryWords.empty()) continue;

            auto imapPage = m_gw2->WikiGetPage(hit.title);
            if (!imapPage.found) continue;
            auto hitImap = ParseInteractiveMapData(imapPage.wikitext);
            if (!hitImap.markers.empty()) {
                imapData = std::move(hitImap);
                break;
            }
        }
    }

    // Collection/interactive map pages: produce entity coords from markers
    if (m_entityCoords.empty() && !imapData.markers.empty() && !Cancelled(cancel)) {
        // Bulk-fetch map rects for all referenced maps
        std::vector<int> missingMaps;
        for (int mid : imapData.mapIds)
            if (m_mapRects.find(mid) == m_mapRects.end()) missingMaps.push_back(mid);

        if (!missingMaps.empty()) {
            auto maps = m_gw2->GetMaps(missingMaps);
            for (auto& m : maps) {
                if (m.hasContRect && m.hasMapRect) {
                    MapRects mr;
                    mr.contRect[0] = m.contRect[0][0]; mr.contRect[1] = m.contRect[0][1];
                    mr.contRect[2] = m.contRect[1][0]; mr.contRect[3] = m.contRect[1][1];
                    mr.mapRect[0] = m.mapRect[0][0]; mr.mapRect[1] = m.mapRect[0][1];
                    mr.mapRect[2] = m.mapRect[1][0]; mr.mapRect[3] = m.mapRect[1][1];
                    m_mapRects[m.id] = mr;
                }
            }
        }

        for (auto& im : imapData.markers) {
            // Geometric map resolution: find which map contains this coordinate
            int resolvedMapId = 0;
            std::string resolvedMapName;
            for (auto& [mid, mr] : m_mapRects) {
                double x1 = std::min(mr.contRect[0], mr.contRect[2]);
                double x2 = std::max(mr.contRect[0], mr.contRect[2]);
                double y1 = std::min(mr.contRect[1], mr.contRect[3]);
                double y2 = std::max(mr.contRect[1], mr.contRect[3]);
                if (im.cx >= x1 && im.cx <= x2 && im.cy >= y1 && im.cy <= y2) {
                    resolvedMapId = mid;
                    break;
                }
            }
            if (resolvedMapId <= 0) continue;

            EntityCoord ec;
            ec.name = im.name.empty() ? wikiPage.title : im.name;
            ec.mapId = resolvedMapId;
            ec.mapName = im.mapName.empty() ? im.text : im.mapName;
            ec.cx = im.cx;
            ec.cy = im.cy;
            ec.hasCoord = true;
            ec.source = EntityCoord::Exact;
            if (!im.text.empty()) ec.areas = {im.text};
            m_entityCoords.push_back(std::move(ec));
        }
    }

    // Achievement progress filtering: mark done entities
    if (!m_entityCoords.empty() && m_gw2ApiKey.empty()) {
        result["account_progress_hint"] = "GW2 API key not configured. User can enter it in Options > Claymore Asistan > GW2 API Key to track achievement progress.";
    }
    if (!m_entityCoords.empty() && !m_gw2ApiKey.empty() && !Cancelled(cancel)) {
        // Extract achievement ID from HTML: #achievement(\d+)
        int achieveId = 0;
        if (htmlPage.found && !htmlPage.html.empty()) {
            std::string searchPat = "#achievement";
            size_t pos = htmlPage.html.find(searchPat);
            if (pos != std::string::npos) {
                pos += searchPat.size();
                std::string num;
                while (pos < htmlPage.html.size() && std::isdigit((unsigned char)htmlPage.html[pos]))
                    num += htmlPage.html[pos++];
                if (!num.empty()) try { achieveId = std::stoi(num); } catch (...) {}
            }
        }

        if (achieveId > 0) {
            auto achieveInfo = m_gw2->GetAchievement(achieveId);
            auto accountProgress = m_gw2->GetAccountAchievement(achieveId, m_gw2ApiKey);

            if (achieveInfo.found && accountProgress.found) {
                // Build set of done bit indices
                std::set<int> doneBits(accountProgress.bits.begin(), accountProgress.bits.end());

                // Match entity coords to achievement bits by name (substring)
                for (auto& ec : m_entityCoords) {
                    std::string ecNameLower = LowerStr(ec.name);
                    for (size_t i = 0; i < achieveInfo.bits.size(); ++i) {
                        std::string bitTextLower = LowerStr(achieveInfo.bits[i].text);
                        if (!ecNameLower.empty() && !bitTextLower.empty() &&
                            (bitTextLower.find(ecNameLower) != std::string::npos ||
                             ecNameLower.find(bitTextLower) != std::string::npos)) {
                            ec.bitIndex = static_cast<int>(i);
                            ec.done = doneBits.count(static_cast<int>(i)) > 0;
                            break;
                        }
                    }
                }

                // If achievement is fully done, mark all
                if (accountProgress.done)
                    for (auto& ec : m_entityCoords) ec.done = true;

                // Add progress to result JSON so the AI can answer "which ones did I do?"
                json progress;
                progress["achievement_id"] = achieveId;
                progress["achievement_name"] = achieveInfo.name;
                progress["done"] = accountProgress.done;
                progress["current"] = accountProgress.current;
                progress["max"] = accountProgress.max;
                progress["total_bits"] = achieveInfo.bits.size();
                progress["completed_bits"] = accountProgress.bits.size();

                json completedNames = json::array();
                json remainingNames = json::array();
                for (size_t i = 0; i < achieveInfo.bits.size(); ++i) {
                    if (doneBits.count(static_cast<int>(i)) > 0)
                        completedNames.push_back(achieveInfo.bits[i].text);
                    else
                        remainingNames.push_back(achieveInfo.bits[i].text);
                }
                progress["completed"] = completedNames;
                progress["remaining"] = remainingNames;
                result["account_progress"] = progress;
            }
        }
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
    auto cacheIt = m_guideCache.find(top.id);
    std::string text;
    std::string pageTitle, pageUrl, pageModified;

    if (cacheIt != m_guideCache.end()) {
        text = cacheIt->second.text;
        pageTitle = cacheIt->second.title;
        pageUrl = cacheIt->second.url;
        pageModified = cacheIt->second.modified;
    } else {
        auto page = m_gw2->GuideGetContent(top.selfHref);
        if (!page.found)
            return "{\"error\": \"Guide content not available: " + top.title + "\"}";
        if (Cancelled(cancel)) return CANCELLED_JSON;

        text = WikiText::HtmlToText(page.html);

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
        pageTitle = page.title;
        pageUrl = page.url;
        pageModified = page.modified;

        m_guideCache[top.id] = {pageTitle, pageUrl, pageModified, text};
    }

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
    result["title"] = pageTitle;
    result["url"] = pageUrl;
    if (!pageModified.empty())
        result["modified"] = pageModified.substr(0, 10);
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

std::string FunctionHandler::HandleBuild(const json& args, const CancelCheck& cancel) {
    std::string query = args.value("query", "");
    std::string mode = LowerStr(TrimStr(args.value("mode", "")));
    std::string sectionWanted = TrimStr(args.value("section", ""));
    if (query.empty()) return "{\"error\": \"query parameter required\"}";

    auto results = m_gw2->BuildSearch(query, 10);
    if (results.empty())
        return "{\"error\": \"No build found for: " + query + "\"}";
    if (Cancelled(cancel)) return CANCELLED_JSON;

    struct BuildMeta {
        std::string title;
        std::string profession;
        std::string specialization;
        std::string designedFor;
        std::string focus;
        std::string rating;
        std::string difficulty;
        std::string templateCode;
        std::string timestamp;
    };

    std::vector<BuildMeta> fetched;
    int selectedIdx = -1;

    for (size_t i = 0; i < results.size() && fetched.size() < BUILD_FETCH_CAP; ++i) {
        if (Cancelled(cancel)) return CANCELLED_JSON;

        auto page = m_gw2->BuildGetPage(results[i].title);
        if (!page.found) continue;

        if (page.wikitext.rfind("#REDIRECT", 0) == 0) {
            auto lb = page.wikitext.find("[[");
            auto rb = page.wikitext.find("]]");
            if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
                std::string target = page.wikitext.substr(lb + 2, rb - lb - 2);
                auto hash = target.find('#');
                if (hash != std::string::npos) target = target.substr(0, hash);
                page = m_gw2->BuildGetPage(target);
                if (!page.found) continue;
            } else continue;
        }

        std::string buildBlock = FindTemplateBlock(page.wikitext, "Build");
        BuildMeta bm;
        bm.title = page.title;
        bm.profession = InfoboxField(buildBlock, "profession");
        bm.specialization = InfoboxField(buildBlock, "specialization");
        bm.designedFor = InfoboxField(buildBlock, "designed for");
        bm.focus = InfoboxField(buildBlock, "focus");
        bm.rating = InfoboxField(buildBlock, "rating");
        bm.difficulty = InfoboxField(buildBlock, "difficulty");
        bm.timestamp = results[i].timestamp;

        std::string tcBlock = FindTemplateBlock(page.wikitext, "TemplateCode");
        if (!tcBlock.empty()) {
            auto ls = tcBlock.find("[&");
            if (ls != std::string::npos) {
                auto le = tcBlock.find(']', ls);
                if (le != std::string::npos)
                    bm.templateCode = tcBlock.substr(ls, le - ls + 1);
            }
        }

        fetched.push_back(std::move(bm));

        if (selectedIdx < 0) {
            std::string lr = LowerStr(fetched.back().rating);
            bool ineligible = (lr == "archived" || lr == "draft" || lr == "trash" || lr == "test");
            if (!ineligible) {
                if (mode.empty() || LowerStr(fetched.back().designedFor).find(mode) != std::string::npos)
                    selectedIdx = static_cast<int>(fetched.size()) - 1;
            }
        }
    }

    if (fetched.empty())
        return "{\"error\": \"No build page available for: " + query + "\"}";
    if (selectedIdx < 0) {
        for (size_t i = 0; i < fetched.size(); ++i) {
            std::string lr = LowerStr(fetched[i].rating);
            if (lr != "archived" && lr != "draft" && lr != "trash" && lr != "test") {
                selectedIdx = static_cast<int>(i);
                break;
            }
        }
    }
    if (selectedIdx < 0) selectedIdx = 0;

    auto& sel = fetched[selectedIdx];

    if (Cancelled(cancel)) return CANCELLED_JSON;
    auto htmlPage = m_gw2->BuildGetPageHtml(sel.title);

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
                    if (content.size() > BUILD_TEXT_BUDGET)
                        content = content.substr(0, BUILD_TEXT_BUDGET) + "\n... (truncated)";
                    break;
                }
            }
            if (content.empty())
                return "{\"error\": \"Section not found: " + sectionWanted + "\"}";
        }

        if (content.empty()) {
            content = lead;
            for (auto& s : sections) {
                if (content.size() >= BUILD_TEXT_BUDGET) break;
                std::string chunk = "\n\n## " + s.title + "\n" + s.body;
                if (content.size() + chunk.size() > BUILD_TEXT_BUDGET) {
                    size_t room = BUILD_TEXT_BUDGET - content.size();
                    content += chunk.substr(0, room);
                    content += "\n... (truncated - call gw2_build with section='" + s.title + "' for the rest)";
                    break;
                }
                content += chunk;
            }
        }
    }

    json result;
    result["source"] = "metabattle";
    result["title"] = sel.title;
    std::string slug = sel.title;
    std::replace(slug.begin(), slug.end(), ' ', '_');
    result["url"] = "https://metabattle.com/wiki/" + slug;
    if (!sel.profession.empty()) result["profession"] = sel.profession;
    if (!sel.specialization.empty()) result["specialization"] = sel.specialization;
    if (!sel.designedFor.empty()) result["designed_for"] = sel.designedFor;
    if (!sel.focus.empty()) result["focus"] = sel.focus;
    if (!sel.rating.empty()) result["rating"] = sel.rating;
    if (!sel.difficulty.empty()) result["difficulty"] = sel.difficulty;
    if (!sel.templateCode.empty()) result["template_code"] = sel.templateCode;
    if (!sel.timestamp.empty()) result["modified"] = sel.timestamp.substr(0, 10);

    if (!content.empty()) result["content"] = content;
    if (!sectionNames.empty()) result["sections"] = sectionNames;

    if (!mode.empty() && LowerStr(sel.designedFor).find(mode) == std::string::npos)
        result["mode_note"] = "No build matched mode '" + mode + "'; showing the top result instead.";
    {
        std::string lr = LowerStr(sel.rating);
        if (lr == "archived" || lr == "draft" || lr == "trash" || lr == "test")
            result["rating_note"] = "This build is rated '" + sel.rating + "' and may be outdated.";
    }

    json alts = json::array();
    for (size_t i = 0; i < fetched.size(); ++i) {
        if (static_cast<int>(i) == selectedIdx) continue;
        json a;
        a["title"] = fetched[i].title;
        if (!fetched[i].designedFor.empty()) a["designed_for"] = fetched[i].designedFor;
        if (!fetched[i].rating.empty()) a["rating"] = fetched[i].rating;
        if (!fetched[i].templateCode.empty()) a["template_code"] = fetched[i].templateCode;
        alts.push_back(a);
    }
    for (size_t i = fetched.size(); i < results.size() && alts.size() < 10; ++i) {
        json a;
        a["title"] = results[i].title;
        alts.push_back(a);
    }
    if (!alts.empty()) result["alternatives"] = alts;

    return result.dump();
}

FunctionHandler::EntityData FunctionHandler::TakeEntityData() {
    EntityData d;
    d.coords = std::move(m_entityCoords);
    d.rects = std::move(m_mapRects);
    m_entityCoords.clear();
    m_mapRects.clear();
    return d;
}

