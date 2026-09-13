#pragma once
#include <string>
#include <vector>

class ConfigManager {
public:
    bool Load(const std::string& path);
    void Save(const std::string& path) const;

    const std::string& GetApiKey() const { return m_apiKey; }
    void SetApiKey(const std::string& key) { m_apiKey = SanitizeApiKey(key); }

    // Strips what a copy-paste drags along: ASCII whitespace, NBSP, zero-width characters, BOM,
    // wrapping quotes. Applied on Load and on every SetApiKey, so a key never carries them.
    static std::string SanitizeApiKey(const std::string& raw);
    // "" when every character can travel in an HTTP header (printable ASCII); otherwise a Turkish
    // sentence naming the position and kind of the offending character - never the key itself.
    // A non-ASCII byte in the key makes WinHTTP reject the request locally (error 87, no response).
    static std::string ApiKeyProblem(const std::string& key);

    const std::vector<std::string>& GetModelChain() const { return m_modelChain; }
    void SetModelChain(const std::vector<std::string>& chain) { m_modelChain = chain; }

    float GetWindowX() const { return m_windowX; }
    float GetWindowY() const { return m_windowY; }
    float GetWindowW() const { return m_windowW; }
    float GetWindowH() const { return m_windowH; }
    void SetWindowPos(float x, float y) { m_windowX = x; m_windowY = y; }
    void SetWindowSize(float w, float h) { m_windowW = w; m_windowH = h; }

    static const std::vector<std::string> DEFAULT_MODEL_CHAIN;

private:
    std::string m_apiKey;
    std::vector<std::string> m_modelChain;
    float m_windowX = 100.0f;
    float m_windowY = 100.0f;
    float m_windowW = 400.0f;
    float m_windowH = 500.0f;
};
