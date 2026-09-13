#include "ItemIndex.h"
#include <json.hpp>
#include <fstream>
#include <algorithm>

using json = nlohmann::json;

std::string ItemIndex::ToLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

bool ItemIndex::Load(const std::string& path) {
    m_path = path;
    std::ifstream f(path);
    if (!f.is_open()) return false;

    try {
        auto j = json::parse(f);
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& [key, val] : j.items()) {
            if (val.is_number_integer())
                m_nameToId[key] = val.get<int>();
        }
        m_dirty = false;
        return true;
    } catch (...) {
        return false;
    }
}

void ItemIndex::Save(const std::string& path) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (!m_dirty) return;

    json j;
    for (auto& [name, id] : m_nameToId)
        j[name] = id;

    std::string savePath = path.empty() ? m_path : path;
    std::string tmp = savePath + ".tmp";
    {
        std::ofstream f(tmp);
        f << j.dump(2);
    }
    std::remove(savePath.c_str());
    std::rename(tmp.c_str(), savePath.c_str());
}

int ItemIndex::Find(const std::string& name) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_nameToId.find(ToLower(name));
    return it != m_nameToId.end() ? it->second : 0;
}

void ItemIndex::Add(const std::string& name, int id) {
    if (name.empty() || id <= 0) return;
    std::lock_guard<std::mutex> lk(m_mutex);
    m_nameToId[ToLower(name)] = id;
    m_dirty = true;
}

void ItemIndex::AddBatch(const std::vector<std::pair<std::string, int>>& items) {
    std::lock_guard<std::mutex> lk(m_mutex);
    for (auto& [name, id] : items) {
        if (!name.empty() && id > 0)
            m_nameToId[ToLower(name)] = id;
    }
    if (!items.empty()) m_dirty = true;
}

size_t ItemIndex::Size() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_nameToId.size();
}
