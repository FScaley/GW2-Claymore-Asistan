#pragma once
#include "HttpClient.h"
#include <string>
#include <vector>
#include <chrono>
#include <functional>

struct GeminiResponse {
    std::string text;
    std::string interactionId;
    std::string error;
    std::string activeModel;
    int statusCode = 0;
    int retrySeconds = 0;
    bool ok = false;
    bool fallbackUsed = false;
};

struct ModelCooldown {
    std::string model;
    std::chrono::steady_clock::time_point until;
    int waitSeconds = 0;
};

class GeminiClient {
public:
    GeminiClient();

    void SetApiKey(const std::string& key) { m_apiKey = key; }
    void SetModelChain(const std::vector<std::string>& chain);

    GeminiResponse Ask(const std::string& question,
                       const std::string& systemPrompt,
                       const std::string& prevInteractionId = "",
                       const std::string& interactionModel = "");

    bool HasApiKey() const { return !m_apiKey.empty(); }

    using LogFunc = std::function<void(const std::string&)>;
    void SetLogger(LogFunc fn) { m_logger = fn; }

    std::vector<ModelCooldown> GetCooldowns() const;
    int GetShortestWait() const;

    static constexpr const char* API_HOST = "generativelanguage.googleapis.com";

    static const std::vector<std::string> DEFAULT_CHAIN;

private:
    GeminiResponse DoRequest(const std::string& model,
                             const std::string& question,
                             const std::string& systemPrompt,
                             const std::string& prevInteractionId);

    int ParseRetrySeconds(const std::string& msg) const;
    void Log(const std::string& msg) const;

    HttpClient m_http;
    std::string m_apiKey;
    std::vector<std::string> m_chain;

    struct CooldownEntry {
        std::chrono::steady_clock::time_point until;
        int waitSeconds = 0;
    };
    std::vector<CooldownEntry> m_cooldowns;

    LogFunc m_logger;
};
