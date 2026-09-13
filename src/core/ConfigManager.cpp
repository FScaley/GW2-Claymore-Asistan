#include "ConfigManager.h"
#include <json.hpp>
#include <fstream>

using json = nlohmann::json;

const std::vector<std::string> ConfigManager::DEFAULT_MODEL_CHAIN = {
    "gemini-3.5-flash",
    "gemini-3.5-flash-lite",
    "gemini-3.8-flash"
};

bool ConfigManager::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        return false;

    try {
        json j = json::parse(f);
        if (j.contains("gemini_api_key")) m_apiKey = j["gemini_api_key"].get<std::string>();
        if (j.contains("window_x"))       m_windowX = j["window_x"].get<float>();
        if (j.contains("window_y"))       m_windowY = j["window_y"].get<float>();
        if (j.contains("window_w"))       m_windowW = j["window_w"].get<float>();
        if (j.contains("window_h"))       m_windowH = j["window_h"].get<float>();

        if (j.contains("model_chain") && j["model_chain"].is_array()) {
            m_modelChain.clear();
            for (auto& m : j["model_chain"])
                if (m.is_string()) m_modelChain.push_back(m.get<std::string>());
        }
    } catch (...) {
        return false;
    }

    if (m_modelChain.empty())
        m_modelChain = DEFAULT_MODEL_CHAIN;

    return true;
}

void ConfigManager::Save(const std::string& path) const {
    json j;
    j["gemini_api_key"] = m_apiKey;
    j["model_chain"] = m_modelChain.empty() ? DEFAULT_MODEL_CHAIN : m_modelChain;
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
