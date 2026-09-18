#include "ChatHistory.h"
#include "Worker.h"
#include "FunctionHandler.h"
#include <json.hpp>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <cstdio>

using json = nlohmann::json;

ChatHistory::ChatHistory(const std::string& historyDir)
    : m_dir(historyDir)
{
    std::filesystem::create_directories(m_dir);
    RefreshIndex();
}

std::string ChatHistory::FilePath(const std::string& id) const {
    return m_dir + "\\" + id + ".json";
}

std::string ChatHistory::GenerateId() {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "chat_" + std::to_string(ms);
}

std::string ChatHistory::TruncateTitle(const std::string& text, size_t maxBytes) {
    if (text.size() <= maxBytes) return text;
    size_t pos = maxBytes;
    while (pos > 0 && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80)
        --pos;
    std::string result = text.substr(0, pos);
    if (pos < text.size()) result += "...";
    return result;
}

void ChatHistory::RefreshIndex() {
    m_index.clear();
    try {
        for (const auto& entry : std::filesystem::directory_iterator(m_dir)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            if (ext != ".json") continue;

            std::ifstream f(entry.path());
            if (!f.is_open()) continue;

            try {
                auto j = json::parse(f);
                ChatEntry ce;
                ce.id = j.value("id", "");
                ce.title = j.value("title", "");
                ce.createdAt = j.value("created_at", int64_t(0));
                ce.updatedAt = j.value("updated_at", int64_t(0));
                if (!ce.id.empty())
                    m_index.push_back(std::move(ce));
            } catch (...) {}
        }
    } catch (...) {}

    std::sort(m_index.begin(), m_index.end(),
              [](const ChatEntry& a, const ChatEntry& b) {
                  return a.updatedAt > b.updatedAt;
              });
}

std::vector<ChatEntry> ChatHistory::GetIndex() const {
    return m_index;
}

static json SerializeEntityCoord(const EntityCoord& ec) {
    json j;
    j["name"] = ec.name;
    j["map_id"] = ec.mapId;
    j["map_name"] = ec.mapName;
    j["cx"] = ec.cx;
    j["cy"] = ec.cy;
    j["source"] = static_cast<int>(ec.source);
    j["has_coord"] = ec.hasCoord;
    j["done"] = ec.done;
    j["bit_index"] = ec.bitIndex;
    j["areas"] = ec.areas;
    return j;
}

static EntityCoord DeserializeEntityCoord(const json& j) {
    EntityCoord ec;
    ec.name = j.value("name", "");
    ec.mapId = j.value("map_id", 0);
    ec.mapName = j.value("map_name", "");
    ec.cx = j.value("cx", 0.0);
    ec.cy = j.value("cy", 0.0);
    ec.source = static_cast<EntityCoord::CoordSource>(j.value("source", 0));
    ec.hasCoord = j.value("has_coord", false);
    ec.done = j.value("done", false);
    ec.bitIndex = j.value("bit_index", -1);
    if (j.contains("areas") && j["areas"].is_array()) {
        for (const auto& a : j["areas"])
            if (a.is_string()) ec.areas.push_back(a.get<std::string>());
    }
    return ec;
}

static json SerializeMapRects(const std::map<int, MapRects>& rects) {
    json j = json::object();
    for (const auto& [mapId, mr] : rects) {
        json entry;
        entry["cont"] = {mr.contRect[0], mr.contRect[1], mr.contRect[2], mr.contRect[3]};
        entry["map"] = {mr.mapRect[0], mr.mapRect[1], mr.mapRect[2], mr.mapRect[3]};
        j[std::to_string(mapId)] = entry;
    }
    return j;
}

static std::map<int, MapRects> DeserializeMapRects(const json& j) {
    std::map<int, MapRects> result;
    if (!j.is_object()) return result;
    for (auto& [key, val] : j.items()) {
        try {
            int mapId = std::stoi(key);
            MapRects mr;
            if (val.contains("cont") && val["cont"].is_array() && val["cont"].size() == 4) {
                for (int i = 0; i < 4; ++i) mr.contRect[i] = val["cont"][i].get<double>();
            }
            if (val.contains("map") && val["map"].is_array() && val["map"].size() == 4) {
                for (int i = 0; i < 4; ++i) mr.mapRect[i] = val["map"][i].get<double>();
            }
            result[mapId] = mr;
        } catch (...) {}
    }
    return result;
}

std::string ChatHistory::Save(const std::string& id,
                              const std::string& title,
                              const std::vector<ChatMessage>& messages,
                              const std::string& interactionId,
                              const std::string& interactionModel,
                              const std::set<std::string>& verifiedLinks,
                              const std::vector<EntityCoord>& entityCoords,
                              const std::map<int, MapRects>& mapRects)
{
    std::string chatId = id.empty() ? GenerateId() : id;
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    int64_t createdAt = now;
    for (const auto& e : m_index) {
        if (e.id == chatId) { createdAt = e.createdAt; break; }
    }

    json j;
    j["id"] = chatId;
    j["title"] = title;
    j["created_at"] = createdAt;
    j["updated_at"] = now;

    json msgs = json::array();
    for (const auto& m : messages) {
        json msg;
        msg["role"] = static_cast<int>(m.role);
        msg["text"] = m.text;
        msgs.push_back(msg);
    }
    j["messages"] = msgs;

    j["interaction_id"] = interactionId;
    j["interaction_model"] = interactionModel;

    json links = json::array();
    for (const auto& l : verifiedLinks) links.push_back(l);
    j["verified_links"] = links;

    json coords = json::array();
    for (const auto& ec : entityCoords)
        coords.push_back(SerializeEntityCoord(ec));
    j["entity_coords"] = coords;

    j["map_rects"] = SerializeMapRects(mapRects);

    std::string path = FilePath(chatId);
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp);
        f << j.dump(2);
    }
    std::remove(path.c_str());
    std::rename(tmp.c_str(), path.c_str());

    RefreshIndex();
    EvictOldest(chatId);

    return chatId;
}

bool ChatHistory::Load(const std::string& id, ChatData& out) {
    std::string path = FilePath(id);
    std::ifstream f(path);
    if (!f.is_open()) return false;

    try {
        auto j = json::parse(f);
        out.id = j.value("id", "");
        out.title = j.value("title", "");
        out.createdAt = j.value("created_at", int64_t(0));
        out.updatedAt = j.value("updated_at", int64_t(0));

        out.messages.clear();
        if (j.contains("messages") && j["messages"].is_array()) {
            for (const auto& msg : j["messages"]) {
                ChatMessage cm;
                cm.role = static_cast<ChatMessage::Role>(msg.value("role", 0));
                cm.text = msg.value("text", "");
                out.messages.push_back(std::move(cm));
            }
        }

        out.interactionId = j.value("interaction_id", "");
        out.interactionModel = j.value("interaction_model", "");

        out.verifiedLinks.clear();
        if (j.contains("verified_links") && j["verified_links"].is_array()) {
            for (const auto& l : j["verified_links"])
                if (l.is_string()) out.verifiedLinks.insert(l.get<std::string>());
        }

        out.entityCoords.clear();
        if (j.contains("entity_coords") && j["entity_coords"].is_array()) {
            for (const auto& ec : j["entity_coords"])
                out.entityCoords.push_back(DeserializeEntityCoord(ec));
        }

        out.mapRects = j.contains("map_rects") ? DeserializeMapRects(j["map_rects"])
                                                : std::map<int, MapRects>{};

        return true;
    } catch (...) {
        return false;
    }
}

bool ChatHistory::Delete(const std::string& id) {
    std::string path = FilePath(id);
    bool removed = (std::remove(path.c_str()) == 0);
    if (removed) RefreshIndex();
    return removed;
}

void ChatHistory::EvictOldest(const std::string& activeId) {
    while (static_cast<int>(m_index.size()) > MAX_HISTORY) {
        const auto& oldest = m_index.back();
        if (oldest.id == activeId) break;
        std::remove(FilePath(oldest.id).c_str());
        m_index.pop_back();
    }
}
