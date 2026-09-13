#pragma once
#include "GeminiClient.h"
#include "ConfigManager.h"
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>

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
    bool fallbackUsed = false;
    uint64_t generation = 0;
};

class Worker {
public:
    using LogFunc = std::function<void(const std::string&)>;

    void Start(ConfigManager* config, LogFunc logger = nullptr);
    void Stop();

    ChatSnapshot GetChatSnapshot() const;
    void RequestChat(const std::string& question);
    void ClearHistory();

private:
    void Run();
    void DoChat(const std::string& question, uint64_t gen);

    ConfigManager* m_config = nullptr;
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

    static const std::string SYSTEM_PROMPT;
};
