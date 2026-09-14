#pragma once
#include <string>
#include <unordered_map>
#include <mutex>

class ItemIndex {
public:
    bool Load(const std::string& path);
    void Save(const std::string& path) const;

    int Find(const std::string& name) const;
    void Add(const std::string& name, int id);
    void AddBatch(const std::vector<std::pair<std::string, int>>& items);
    size_t Size() const;

private:
    static std::string ToLower(const std::string& s);

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, int> m_nameToId;
    std::string m_path;
    bool m_dirty = false;
    int m_dirtyCount = 0;
};
