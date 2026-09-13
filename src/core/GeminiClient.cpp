#include "GeminiClient.h"
#include <json.hpp>

using json = nlohmann::json;

GeminiClient::GeminiClient() {}

GeminiResponse GeminiClient::DoRequest(const std::string& model,
                                       const std::string& question,
                                       const std::string& systemPrompt,
                                       const std::string& prevInteractionId)
{
    GeminiResponse result;

    json body;
    body["model"] = model;
    body["input"] = question;
    if (!systemPrompt.empty())
        body["system_instruction"] = systemPrompt;
    // Google Search grounding free tier'da kota sorunu yapiyor — Faz 2'de opsiyonel olacak
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
            auto pos = msg.find("retry in ");
            if (pos != std::string::npos) {
                auto end = msg.find("s", pos + 9);
                std::string wait = msg.substr(pos + 9, end - pos - 9);
                try {
                    int secs = static_cast<int>(std::stof(wait)) + 1;
                    result.error = "Rate limit - " + std::to_string(secs) + " sn bekle.";
                } catch (...) {
                    result.error = "Rate limit - biraz bekle.";
                }
            } else {
                result.error = "Rate limit - biraz bekle.";
            }
        } else if (resp->statusCode == 401 || resp->statusCode == 403) {
            result.error = "API key gecersiz veya yetkisiz";
        } else if ((resp->statusCode == 400 || resp->statusCode == 404) && !prevInteractionId.empty()) {
            result.error = msg + " (Sohbet gecmisi sifirlanmali)";
        } else {
            result.error = msg.empty() ? ("HTTP " + std::to_string(resp->statusCode)) : msg;
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
                                 const std::string& prevInteractionId)
{
    auto result = DoRequest(m_model, question, systemPrompt, prevInteractionId);

    // 429 = rate limit tum key icin gecerli, fallback YAPMA (bosa istek harcar)
    // 500/503 = model-spesifik hata (high demand), fallback dene
    if (!result.ok && !m_fallback.empty() && m_fallback != m_model) {
        if (result.statusCode == 500 || result.statusCode == 503) {
            result = DoRequest(m_fallback, question, systemPrompt, prevInteractionId);
        }
    }

    return result;
}
