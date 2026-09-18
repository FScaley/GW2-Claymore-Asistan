#pragma once
#include "GeminiClient.h"
#include "ConfigManager.h"
#include "FunctionHandler.h"
#include "ChatHistory.h"
#include <vector>
#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <set>

struct ChatMessage {
    enum Role { User, Assistant, System };
    Role role;
    std::string text;
};

struct ChatSnapshot {
    std::vector<ChatMessage> messages;
    bool busy = false;
    std::string error;
    std::string activeModel;
    std::string toolStatus;
    bool fallbackUsed = false;
    uint64_t generation = 0;
    std::vector<EntityCoord> entityCoords;
    std::map<int, MapRects> mapRects;
    uint64_t entitySeq = 0;
    std::vector<ChatEntry> historyIndex;
    std::string activeHistoryId;
    uint64_t conversationSeq = 0;
};

class Worker {
public:
    using LogFunc = std::function<void(const std::string&)>;

    void Start(ConfigManager* config, FunctionHandler* funcHandler,
               LogFunc logger = nullptr, ChatHistory* history = nullptr);
    void Stop();

    ChatSnapshot GetChatSnapshot() const;
    uint64_t GetEntitySeq() const { return m_entitySeq; }
    uint64_t GetSnapshotSeq() const { return m_snapshotSeq.load(); }
    void RequestChat(const std::string& question);
    void CancelChat();
    void ClearHistory();

    void RequestNewChat();
    void RequestLoadChat(const std::string& id);
    void RequestDeleteChat(const std::string& id);
    void RequestClearChat();

private:
    void Run();
    void DoChat(const std::string& question, uint64_t gen);
    void SetToolStatus(const std::string& status);
    bool IsGenerationCurrent(uint64_t gen) const { return m_generation == gen; }

    enum class HistoryAction { None, New, Load, Delete, Clear };
    void HandleHistoryAction(HistoryAction action, const std::string& param);
    void AutoSave();
    void UpdateHistoryInSnapshot();
    std::string BuildContextPreamble();
    void ClearConversationState();

    ConfigManager* m_config = nullptr;
    FunctionHandler* m_funcHandler = nullptr;
    GeminiClient m_gemini;
    ChatHistory* m_history = nullptr;

    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::mutex m_cvMutex;
    std::condition_variable m_cv;

    mutable std::mutex m_snapshotMutex;
    ChatSnapshot m_snapshot;

    std::string m_pendingQuestion;
    std::atomic<bool> m_chatRequested{false};
    std::atomic<uint64_t> m_generation{0};

    std::string m_interactionId;
    std::string m_interactionModel;
    std::string m_keyProblem;
    std::set<std::string> m_verifiedLinks;
    std::vector<EntityCoord> m_entityCoords;
    std::map<int, MapRects> m_mapRects;
    std::atomic<uint64_t> m_entitySeq{0};
    std::atomic<uint64_t> m_snapshotSeq{0};

    std::string m_activeHistoryId;
    std::atomic<int> m_historyAction{0};
    std::string m_historyParam;
    bool m_needsReinjection = false;
    uint64_t m_conversationSeq = 0;

    static const std::string SYSTEM_PROMPT;
    static constexpr int MAX_FC_ROUNDS = 6;
};
