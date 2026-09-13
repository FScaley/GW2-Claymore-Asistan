#include "GeminiClient.h"
#include <json.hpp>
#include <algorithm>

using json = nlohmann::json;

const std::vector<std::string> GeminiClient::DEFAULT_CHAIN = {
    "gemini-3.5-flash-lite",
    "gemini-3.5-flash",
    "gemini-3.8-flash"
};

GeminiClient::GeminiClient() {
    SetModelChain(DEFAULT_CHAIN);
}

void GeminiClient::SetModelChain(const std::vector<std::string>& chain) {
    m_chain = chain.empty() ? DEFAULT_CHAIN : chain;
    m_cooldowns.resize(m_chain.size());
    for (auto& cd : m_cooldowns) {
        cd.until = std::chrono::steady_clock::time_point{};
        cd.waitSeconds = 0;
    }
}

int GeminiClient::ParseRetrySeconds(const std::string& msg) const {
    auto pos = msg.find("retry in ");
    if (pos == std::string::npos) return 30;
    try {
        return static_cast<int>(std::stof(msg.substr(pos + 9))) + 1;
    } catch (...) {
        return 30;
    }
}

void GeminiClient::Log(const std::string& msg) const {
    if (m_logger) m_logger(msg);
}

std::vector<ModelCooldown> GeminiClient::GetCooldowns() const {
    std::vector<ModelCooldown> result;
    auto now = std::chrono::steady_clock::now();
    for (size_t i = 0; i < m_chain.size(); ++i) {
        if (m_cooldowns[i].until > now) {
            int remaining = static_cast<int>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    m_cooldowns[i].until - now).count());
            result.push_back({m_chain[i], m_cooldowns[i].until, remaining});
        }
    }
    return result;
}

int GeminiClient::GetShortestWait() const {
    auto now = std::chrono::steady_clock::now();
    int shortest = 999;
    bool allCooling = true;
    for (size_t i = 0; i < m_chain.size(); ++i) {
        if (m_cooldowns[i].until <= now) {
            allCooling = false;
            break;
        }
        int remaining = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(
                m_cooldowns[i].until - now).count());
        shortest = std::min(shortest, remaining);
    }
    return allCooling ? shortest : 0;
}

GeminiResponse GeminiClient::DoRequest(const std::string& model,
                                       const std::string& question,
                                       const std::string& systemPrompt,
                                       const std::string& prevInteractionId)
{
    GeminiResponse result;
    result.activeModel = model;

    json body;
    body["model"] = model;
    body["input"] = question;
    if (!systemPrompt.empty())
        body["system_instruction"] = systemPrompt;
    body["stream"] = false;

    if (!prevInteractionId.empty())
        body["previous_interaction_id"] = prevInteractionId;

    std::string bodyStr = body.dump();
    std::string headers = "x-goog-api-key: " + m_apiKey + "\r\n";

    auto resp = m_http.Post(API_HOST, "/v1beta/interactions", bodyStr, headers);

    if (!resp.has_value()) {
        result.error = "Baglanti hatasi";
        return result;
    }

    result.statusCode = resp->statusCode;

    json parsed;
    try {
        parsed = json::parse(resp->body);
    } catch (...) {
        result.error = "JSON parse hatasi";
        return result;
    }

    if (resp->statusCode != 200) {
        std::string msg;
        if (parsed.contains("error") && parsed["error"].contains("message"))
            msg = parsed["error"]["message"].get<std::string>();

        if (resp->statusCode == 429) {
            result.retrySeconds = ParseRetrySeconds(msg);
            result.error = "Rate limit (" + model + ") - " + std::to_string(result.retrySeconds) + " sn";
            Log("[429] " + model + ": " + msg);
        } else if (resp->statusCode == 401 || resp->statusCode == 403) {
            result.error = "API key gecersiz veya yetkisiz";
            Log("[" + std::to_string(resp->statusCode) + "] " + msg);
        } else if ((resp->statusCode == 400 || resp->statusCode == 404) && !prevInteractionId.empty()) {
            result.error = msg + " (Sohbet gecmisi sifirlanmali)";
            Log("[" + std::to_string(resp->statusCode) + "] " + model + ": " + msg);
        } else {
            result.error = msg.empty() ? ("HTTP " + std::to_string(resp->statusCode)) : msg;
            Log("[" + std::to_string(resp->statusCode) + "] " + model + ": " + msg);
        }
        return result;
    }

    if (parsed.contains("id"))
        result.interactionId = parsed["id"].get<std::string>();

    if (parsed.contains("steps") && parsed["steps"].is_array()) {
        for (auto& step : parsed["steps"]) {
            if (!step.contains("type") || step["type"] != "model_output")
                continue;
            if (!step.contains("content") || !step["content"].is_array())
                continue;
            for (auto& c : step["content"]) {
                if (c.contains("text") && c["text"].is_string()) {
                    if (!result.text.empty())
                        result.text += "\n";
                    result.text += c["text"].get<std::string>();
                }
            }
        }
    }

    result.ok = true;
    return result;
}

GeminiResponse GeminiClient::Ask(const std::string& question,
                                 const std::string& systemPrompt,
                                 const std::string& prevInteractionId,
                                 const std::string& interactionModel)
{
    auto now = std::chrono::steady_clock::now();
    GeminiResponse lastResult;
    bool anyAttempted = false;

    for (size_t i = 0; i < m_chain.size(); ++i) {
        if (m_cooldowns[i].until > now) {
            Log("Cooldown: " + m_chain[i] + " atlaniyor");
            continue;
        }

        bool matchesInteractionModel = (!interactionModel.empty() && m_chain[i] == interactionModel);
        std::string idToSend = matchesInteractionModel ? prevInteractionId : "";

        anyAttempted = true;
        auto result = DoRequest(m_chain[i], question, systemPrompt, idToSend);

        if (result.ok) {
            result.fallbackUsed = (i > 0);
            return result;
        }

        if (result.statusCode == 429) {
            int secs = result.retrySeconds > 0 ? result.retrySeconds : 30;
            m_cooldowns[i].until = std::chrono::steady_clock::now() + std::chrono::seconds(secs);
            m_cooldowns[i].waitSeconds = secs;
            Log("Cooldown: " + m_chain[i] + " = " + std::to_string(secs) + "s");
            lastResult = result;
            continue;
        }

        if (result.statusCode == 500 || result.statusCode == 503) {
            Log("Sunucu hatasi: " + m_chain[i] + " (" + std::to_string(result.statusCode) + ")");
            lastResult = result;
            continue;
        }

        return result;
    }

    int wait = GetShortestWait();
    if (wait > 0 || !anyAttempted) {
        if (wait <= 0) wait = 30;
        lastResult.statusCode = 429;
        lastResult.error = "Tum modeller mesgul - " + std::to_string(wait) + " sn bekle";
        lastResult.activeModel = m_chain.empty() ? "" : m_chain[0];
    }
    return lastResult;
}
