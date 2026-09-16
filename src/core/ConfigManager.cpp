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
        if (j.contains("gemini_api_key")) SetApiKey(j["gemini_api_key"].get<std::string>());
        if (j.contains("gw2_api_key")) SetGw2ApiKey(j["gw2_api_key"].get<std::string>());
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

namespace {

// Leading/trailing junk a pasted key may carry. UTF-8 sequences: NBSP (C2 A0), narrow NBSP
// (E2 80 AF), zero-width space / non-joiner / joiner (E2 80 8B/8C/8D), BOM (EF BB BF).
const char* const KEY_JUNK[] = {
    " ", "\t", "\r", "\n", "\v", "\f",
    "\xc2\xa0", "\xe2\x80\xaf", "\xe2\x80\x8b", "\xe2\x80\x8c", "\xe2\x80\x8d", "\xef\xbb\xbf",
    "\"", "'", "`"
};

bool StripOneEnd(std::string& s) {
    for (const char* j : KEY_JUNK) {
        size_t n = std::char_traits<char>::length(j);
        if (s.size() < n) continue;
        if (s.compare(0, n, j) == 0) { s.erase(0, n); return true; }
        if (s.compare(s.size() - n, n, j) == 0) { s.erase(s.size() - n); return true; }
    }
    return false;
}

} // namespace

std::string ConfigManager::SanitizeApiKey(const std::string& raw) {
    std::string s = raw;
    while (!s.empty() && StripOneEnd(s)) {}
    return s;
}

std::string ConfigManager::ApiKeyProblem(const std::string& key) {
    for (size_t i = 0; i < key.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(key[i]);
        if (c >= 0x21 && c <= 0x7E) continue;
        std::string kind = c == ' '  ? "bosluk"
                         : c < 0x20  ? "kontrol karakteri (satir sonu/tab)"
                                     : "ASCII disi karakter (Turkce harf veya gorunmez bosluk gibi)";
        return "API anahtarinin " + std::to_string(i + 1) + ". karakteri gecersiz: " + kind
             + ". Ayarlardan anahtari silip yeniden yapistirin.";
    }
    return "";
}

void ConfigManager::Save(const std::string& path) const {
    json j;
    j["gemini_api_key"] = m_apiKey;
    if (!m_gw2ApiKey.empty()) j["gw2_api_key"] = m_gw2ApiKey;
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
