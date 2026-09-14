#pragma once
#include "GeminiClient.h"
#include "ConfigManager.h"
#include "FunctionHandler.h"
#include <vector>
#include <string>
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
};

class Worker {
public:
    using LogFunc = std::function<void(const std::string&)>;

    void Start(ConfigManager* config, FunctionHandler* funcHandler,
               LogFunc logger = nullptr);
    void Stop();

    ChatSnapshot GetChatSnapshot() const;
    void RequestChat(const std::string& question);
    void CancelChat();
    void ClearHistory();

private:
    void Run();
    void DoChat(const std::string& question, uint64_t gen);
    void SetToolStatus(const std::string& status);
    bool IsGenerationCurrent(uint64_t gen) const { return m_generation == gen; }

    ConfigManager* m_config = nullptr;
    FunctionHandler* m_funcHandler = nullptr;
    GeminiClient m_gemini;

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
    std::set<std::string> m_verifiedLinks;      // why the key cannot be sent ("" = fine); set in Start, read by the worker thread

    static const std::string SYSTEM_PROMPT;
    static constexpr int MAX_FC_ROUNDS = 6;
};
