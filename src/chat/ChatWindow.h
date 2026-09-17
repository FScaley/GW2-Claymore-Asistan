#pragma once
#include "../core/Worker.h"
#include "../imgui/imgui.h"
#include "Markdown.h"
#include <string>
#include <vector>
#include <chrono>

class ChatWindow {
public:
    void Render(Worker* worker, bool* pOpen);

private:
    void RenderParsedLines(const std::vector<Markdown::Line>& lines);
    void RenderTokens(const std::vector<Markdown::Token>& tokens, float maxX,
                      int& linkId, const ImVec4* overrideColor);
    void CopyToClipboard(const std::string& utf8);

    char m_inputBuf[512] = "";
    bool m_scrollToBottom = false;
    bool m_refocus = false;
    size_t m_lastMsgCount = 0;
    std::string m_copiedText;
    std::chrono::steady_clock::time_point m_copiedAt;

    uint64_t m_cachedSnapshotSeq = 0;
    ChatSnapshot m_cachedSnapshot;
    std::vector<std::vector<Markdown::Line>> m_parsedMessages;
};
