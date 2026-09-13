#include "ConfigManager.h"
#include <json.hpp>
#include <fstream>

using json = nlohmann::json;

bool ConfigManager::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        return false;

    try {
        json j = json::parse(f);
        if (j.contains("gemini_api_key")) m_apiKey = j["gemini_api_key"].get<std::string>();
        if (j.contains("model"))          m_model = j["model"].get<std::string>();
        if (j.contains("model_fallback")) m_modelFallback = j["model_fallback"].get<std::string>();
        if (j.contains("model_tier"))     m_modelTier = j["model_tier"].get<std::string>();
        if (j.contains("window_x"))       m_windowX = j["window_x"].get<float>();
        if (j.contains("window_y"))       m_windowY = j["window_y"].get<float>();
        if (j.contains("window_w"))       m_windowW = j["window_w"].get<float>();
        if (j.contains("window_h"))       m_windowH = j["window_h"].get<float>();
    } catch (...) {
        return false;
    }

    return true;
}

void ConfigManager::Save(const std::string& path) const {
    json j;
    j["gemini_api_key"] = m_apiKey;
    j["model"] = m_model;
    j["model_fallback"] = m_modelFallback;
    j["model_tier"] = m_modelTier;
    j["window_x"] = m_windowX;
    j["window_y"] = m_windowY;
    j["window_w"] = m_windowW;
    j["window_h"] = m_windowH;

    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp);
        f << j.dump(2);
    }
    std::remove(path.c_str());
    std::rename(tmp.c_str(), path.c_str());
}
