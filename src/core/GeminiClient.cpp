#include "GeminiClient.h"
#include <algorithm>
#include <thread>

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

GeminiResponse GeminiClient::ParseResponse(const std::string& body, int statusCode,
                                            const std::string& model) {
    GeminiResponse result;
    result.activeModel = model;
    result.statusCode = statusCode;

    json parsed;
    try {
        parsed = json::parse(body);
    } catch (...) {
        result.error = "JSON parse hatasi";
        return result;
    }

    if (statusCode != 200) {
        std::string msg;
        if (parsed.contains("error") && parsed["error"].contains("message"))
            msg = parsed["error"]["message"].get<std::string>();

        if (statusCode == 429) {
            result.retrySeconds = ParseRetrySeconds(msg);
            result.error = "Rate limit (" + model + ") - " + std::to_string(result.retrySeconds) + " sn";
            Log("[429] " + model + ": " + msg);
        } else if (statusCode == 401 || statusCode == 403) {
            result.error = "API key gecersiz veya yetkisiz";
            Log("[" + std::to_string(statusCode) + "] " + msg);
        } else {
            result.error = msg.empty() ? ("HTTP " + std::to_string(statusCode)) : msg;
            Log("[" + std::to_string(statusCode) + "] " + model + ": " + msg);
        }
        return result;
    }

    if (parsed.contains("id"))
        result.interactionId = parsed["id"].get<std::string>();

    result.status = parsed.value("status", "completed");

    if (parsed.contains("steps") && parsed["steps"].is_array()) {
        for (auto& step : parsed["steps"]) {
            std::string stepType = step.value("type", "");

            if (stepType == "function_call") {
                FunctionCallInfo fc;
                fc.id = step.value("id", "");
                fc.name = step.value("name", "");
                if (step.contains("arguments"))
                    fc.arguments = step["arguments"];
                result.functionCalls.push_back(std::move(fc));
            }

            if (stepType == "model_output") {
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
    }

    if (result.status == "requires_action" && !result.functionCalls.empty()) {
        result.ok = false;
    } else if (result.status == "completed" || !result.text.empty()) {
        result.ok = true;
    }

    return result;
}

GeminiResponse GeminiClient::DoRequest(const std::string& model,
                                       const std::string& jsonBody) {
    GeminiResponse result;
    result.activeModel = model;

    std::string headers = "x-goog-api-key: " + m_apiKey + "\r\n";
    auto resp = m_http.Post(API_HOST, "/v1beta/interactions", jsonBody, headers);

    if (!resp.has_value()) {
        result.error = "Baglanti hatasi";
        return result;
    }

    return ParseResponse(resp->body, resp->statusCode, model);
}

GeminiResponse GeminiClient::Ask(const std::string& question,
                                 const std::string& systemPrompt,
                                 const std::string& prevInteractionId,
                                 const std::string& interactionModel,
                                 const nlohmann::json& tools)
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

        json body;
        body["model"] = m_chain[i];
        body["input"] = question;
        if (!systemPrompt.empty())
            body["system_instruction"] = systemPrompt;
        body["stream"] = false;

        if (!idToSend.empty())
            body["previous_interaction_id"] = idToSend;

        if (!tools.is_null() && tools.is_array() && !tools.empty())
            body["tools"] = tools;

        anyAttempted = true;
        auto result = DoRequest(m_chain[i], body.dump());

        if (result.ok || result.RequiresAction()) {
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

GeminiResponse GeminiClient::SendFunctionResults(
    const std::string& model,
    const std::string& interactionId,
    const std::vector<std::pair<std::string, std::pair<std::string, std::string>>>& results,
    const nlohmann::json& tools,
    const std::string& systemPrompt)
{
    json inputArr = json::array();
    for (auto& [callId, nameAndResult] : results) {
        json fr;
        fr["type"] = "function_result";
        fr["name"] = nameAndResult.first;
        fr["call_id"] = callId;
        fr["result"] = json::array();
        fr["result"].push_back({{"type", "text"}, {"text", nameAndResult.second}});
        inputArr.push_back(fr);
    }

    json body;
    body["model"] = model;
    body["input"] = inputArr;
    body["previous_interaction_id"] = interactionId;
    body["stream"] = false;

    if (!systemPrompt.empty())
        body["system_instruction"] = systemPrompt;

    if (!tools.is_null() && tools.is_array() && !tools.empty())
        body["tools"] = tools;

    auto result = DoRequest(model, body.dump());

    if (result.statusCode == 429) {
        int secs = result.retrySeconds > 0 ? result.retrySeconds : 10;
        if (secs <= 12) {
            Log("FC 429 retry: " + model + " - " + std::to_string(secs) + "s bekleniyor");
            std::this_thread::sleep_for(std::chrono::seconds(secs));
            result = DoRequest(model, body.dump());
        } else {
            Log("FC 429 abandon: " + model + " - " + std::to_string(secs) + "s cok uzun");
        }

        if (result.statusCode == 429) {
            auto it = std::find(m_chain.begin(), m_chain.end(), model);
            if (it != m_chain.end()) {
                size_t i = it - m_chain.begin();
                int cdSecs = result.retrySeconds > 0 ? result.retrySeconds : 30;
                m_cooldowns[i].until = std::chrono::steady_clock::now() + std::chrono::seconds(cdSecs);
                m_cooldowns[i].waitSeconds = cdSecs;
                Log("Cooldown: " + model + " = " + std::to_string(cdSecs) + "s (FC)");
            }
        }
    }

    return result;
}
