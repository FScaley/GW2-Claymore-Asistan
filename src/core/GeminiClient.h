#pragma once
#include "HttpClient.h"
#include <string>

struct GeminiResponse {
    std::string text;
    std::string interactionId;
    std::string error;
    int statusCode = 0;
    bool ok = false;
};

class GeminiClient {
public:
    GeminiClient();

    void SetApiKey(const std::string& key) { m_apiKey = key; }
    void SetModel(const std::string& model) { m_model = model; }
    void SetModelFallback(const std::string& fallback) { m_fallback = fallback; }

    GeminiResponse Ask(const std::string& question,
                       const std::string& systemPrompt,
                       const std::string& prevInteractionId = "");

    bool HasApiKey() const { return !m_apiKey.empty(); }

    static constexpr const char* API_HOST = "generativelanguage.googleapis.com";

private:
    GeminiResponse DoRequest(const std::string& model,
                             const std::string& question,
                             const std::string& systemPrompt,
                             const std::string& prevInteractionId);

    HttpClient m_http;
    std::string m_apiKey;
    std::string m_model = "gemini-3.8-flash";
    std::string m_fallback = "gemini-3.5-flash";
};
