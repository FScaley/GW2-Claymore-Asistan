#pragma once
#include "../core/Worker.h"
#include "../imgui/imgui.h"
#include <string>
#include <vector>
#include <chrono>

class ChatWindow {
public:
    void Render(Worker* worker, bool* pOpen);

private:
    void RenderFormattedText(const std::string& text);
    void CopyToClipboard(const std::string& utf8);

    char m_inputBuf[512] = "";
    bool m_scrollToBottom = false;
    bool m_refocus = false;
    size_t m_lastMsgCount = 0;
    std::string m_copiedText;
    std::chrono::steady_clock::time_point m_copiedAt;
};
