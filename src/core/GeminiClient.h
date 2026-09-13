#pragma once
#include "HttpClient.h"
#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include <json.hpp>

struct FunctionCallInfo {
    std::string id;
    std::string name;
    nlohmann::json arguments;
};

struct GeminiResponse {
    std::string text;
    std::string interactionId;
    std::string error;
    std::string activeModel;
    std::string status;
    int statusCode = 0;
    int retrySeconds = 0;
    bool ok = false;
    bool fallbackUsed = false;
    std::vector<FunctionCallInfo> functionCalls;

    bool RequiresAction() const { return status == "requires_action" && !functionCalls.empty(); }
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
                       const std::string& interactionModel = "",
                       const nlohmann::json& tools = nlohmann::json());

    GeminiResponse SendFunctionResults(
        const std::string& model,
        const std::string& interactionId,
        const std::vector<std::pair<std::string, std::pair<std::string, std::string>>>& results,
        const nlohmann::json& tools,
        const std::string& systemPrompt = "");

    bool HasApiKey() const { return !m_apiKey.empty(); }

    using LogFunc = std::function<void(const std::string&)>;
    void SetLogger(LogFunc fn) { m_logger = fn; }
    LogFunc GetLogger() const { return m_logger; }

    std::vector<ModelCooldown> GetCooldowns() const;
    int GetShortestWait() const;

    static constexpr const char* API_HOST = "generativelanguage.googleapis.com";

    static const std::vector<std::string> DEFAULT_CHAIN;

private:
    GeminiResponse DoRequest(const std::string& model,
                             const std::string& jsonBody);

    GeminiResponse ParseResponse(const std::string& body, int statusCode,
                                 const std::string& model);

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
