#pragma once
#include <string>

class ConfigManager {
public:
    bool Load(const std::string& path);
    void Save(const std::string& path) const;

    const std::string& GetApiKey() const { return m_apiKey; }
    void SetApiKey(const std::string& key) { m_apiKey = key; }

    const std::string& GetModel() const { return m_model; }
    void SetModel(const std::string& model) { m_model = model; }

    const std::string& GetModelFallback() const { return m_modelFallback; }
    void SetModelFallback(const std::string& model) { m_modelFallback = model; }

    const std::string& GetModelTier() const { return m_modelTier; }
    void SetModelTier(const std::string& tier) { m_modelTier = tier; }

    float GetWindowX() const { return m_windowX; }
    float GetWindowY() const { return m_windowY; }
    float GetWindowW() const { return m_windowW; }
    float GetWindowH() const { return m_windowH; }
    void SetWindowPos(float x, float y) { m_windowX = x; m_windowY = y; }
    void SetWindowSize(float w, float h) { m_windowW = w; m_windowH = h; }

private:
    std::string m_apiKey;
    std::string m_model = "gemini-3.8-flash";
    std::string m_modelFallback = "gemini-3.5-flash";
    std::string m_modelTier = "auto";
    float m_windowX = 100.0f;
    float m_windowY = 100.0f;
    float m_windowW = 400.0f;
    float m_windowH = 500.0f;
};
