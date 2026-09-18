#pragma once
#include <string>
#include <vector>
#include <set>
#include <map>
#include <cstdint>

struct ChatMessage;
struct EntityCoord;
struct MapRects;

struct ChatEntry {
    std::string id;
    std::string title;
    int64_t createdAt = 0;
    int64_t updatedAt = 0;
};

struct ChatData {
    std::string id;
    std::string title;
    int64_t createdAt = 0;
    int64_t updatedAt = 0;
    std::vector<ChatMessage> messages;
    std::string interactionId;
    std::string interactionModel;
    std::set<std::string> verifiedLinks;
    std::vector<EntityCoord> entityCoords;
    std::map<int, MapRects> mapRects;
};

class ChatHistory {
public:
    explicit ChatHistory(const std::string& historyDir);

    std::vector<ChatEntry> GetIndex() const;
    void RefreshIndex();

    std::string Save(const std::string& id,
                     const std::string& title,
                     const std::vector<ChatMessage>& messages,
                     const std::string& interactionId,
                     const std::string& interactionModel,
                     const std::set<std::string>& verifiedLinks,
                     const std::vector<EntityCoord>& entityCoords,
                     const std::map<int, MapRects>& mapRects);

    bool Load(const std::string& id, ChatData& out);
    bool Delete(const std::string& id);

    static constexpr int MAX_HISTORY = 20;

    static std::string TruncateTitle(const std::string& text, size_t maxBytes = 50);

private:
    std::string m_dir;
    std::vector<ChatEntry> m_index;

    std::string FilePath(const std::string& id) const;
    void EvictOldest(const std::string& activeId);
    static std::string GenerateId();
};
