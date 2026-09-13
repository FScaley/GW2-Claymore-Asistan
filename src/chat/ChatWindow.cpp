#include "ChatWindow.h"
#include <Windows.h>
#include <cmath>

static void PushGW2Style() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));

    ImGui::PushStyleColor(ImGuiCol_WindowBg,          ImVec4(0.05f, 0.04f, 0.03f, 0.88f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg,            ImVec4(0.08f, 0.06f, 0.03f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,      ImVec4(0.12f, 0.09f, 0.04f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed,   ImVec4(0.05f, 0.04f, 0.02f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_Border,             ImVec4(0.93f, 0.91f, 0.67f, 0.35f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg,            ImVec4(0.03f, 0.025f, 0.02f, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,            ImVec4(0.00f, 0.00f, 0.00f, 0.37f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,     ImVec4(0.10f, 0.08f, 0.04f, 0.50f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,      ImVec4(0.15f, 0.12f, 0.06f, 0.60f));
    ImGui::PushStyleColor(ImGuiCol_Button,             ImVec4(0.93f, 0.91f, 0.67f, 0.28f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,      ImVec4(0.93f, 0.91f, 0.67f, 0.50f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,       ImVec4(0.93f, 0.91f, 0.67f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,        ImVec4(0.02f, 0.02f, 0.02f, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,      ImVec4(0.50f, 0.50f, 0.50f, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.60f, 0.60f, 0.60f, 0.55f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,  ImVec4(0.70f, 0.70f, 0.70f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_ResizeGrip,         ImVec4(0.93f, 0.91f, 0.67f, 0.15f));
    ImGui::PushStyleColor(ImGuiCol_ResizeGripHovered,  ImVec4(0.93f, 0.91f, 0.67f, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_ResizeGripActive,   ImVec4(0.93f, 0.91f, 0.67f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_Separator,          ImVec4(0.93f, 0.91f, 0.67f, 0.20f));
    ImGui::PushStyleColor(ImGuiCol_Header,             ImVec4(0.93f, 0.91f, 0.67f, 0.10f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,      ImVec4(0.93f, 0.91f, 0.67f, 0.25f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,       ImVec4(0.93f, 0.91f, 0.67f, 0.35f));
    ImGui::PushStyleColor(ImGuiCol_Text,               ImVec4(1.00f, 1.00f, 1.00f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled,       ImVec4(0.60f, 0.58f, 0.50f, 1.00f));
}

static void PopGW2Style() {
    ImGui::PopStyleColor(25);
    ImGui::PopStyleVar(7);
}

static const ImVec4 COL_GOLD      = ImVec4(0.93f, 0.91f, 0.67f, 1.0f);
static const ImVec4 COL_USER      = ImVec4(0.55f, 0.75f, 1.00f, 1.0f);
static const ImVec4 COL_ASSISTANT = ImVec4(0.93f, 0.91f, 0.67f, 1.0f);
static const ImVec4 COL_SYSTEM    = ImVec4(1.00f, 0.35f, 0.30f, 1.0f);
static const ImVec4 COL_DIM       = ImVec4(0.60f, 0.58f, 0.50f, 1.0f);
static const ImVec4 COL_BODY      = ImVec4(1.00f, 1.00f, 1.00f, 1.0f);
static const ImVec4 COL_CHATLINK  = ImVec4(0.40f, 0.85f, 0.95f, 1.0f);
static const ImVec4 COL_BOLD      = ImVec4(1.00f, 0.84f, 0.00f, 1.0f);

void ChatWindow::CopyToClipboard(const std::string& utf8) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (wlen > 0) {
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, wlen * sizeof(wchar_t));
        if (h) {
            MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, (wchar_t*)GlobalLock(h), wlen);
            GlobalUnlock(h);
            if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h);
        }
    }
    CloseClipboard();
    m_copiedText = utf8;
    m_copiedAt = std::chrono::steady_clock::now();
}

struct RichToken {
    enum Type { Plain, Bold, ChatLink, Newline };
    Type type;
    std::string text;
    ImVec4 color;
};

static std::vector<RichToken> Tokenize(const std::string& text) {
    std::vector<RichToken> tokens;
    size_t pos = 0;
    size_t len = text.size();
    std::string accum;

    auto flushAccum = [&](ImVec4 color, RichToken::Type type) {
        if (accum.empty()) return;
        size_t start = 0;
        while (start < accum.size()) {
            size_t nl = accum.find('\n', start);
            if (nl == start) {
                tokens.push_back({RichToken::Newline, "", {}});
                start = nl + 1;
            } else {
                std::string chunk = (nl == std::string::npos)
                    ? accum.substr(start) : accum.substr(start, nl - start);
                size_t wstart = 0;
                while (wstart < chunk.size()) {
                    size_t sp = chunk.find(' ', wstart);
                    std::string word;
                    if (sp == std::string::npos) {
                        word = chunk.substr(wstart);
                        wstart = chunk.size();
                    } else {
                        word = chunk.substr(wstart, sp - wstart + 1);
                        wstart = sp + 1;
                    }
                    if (!word.empty())
                        tokens.push_back({type, word, color});
                }
                if (nl != std::string::npos) {
                    tokens.push_back({RichToken::Newline, "", {}});
                    start = nl + 1;
                } else {
                    break;
                }
            }
        }
        accum.clear();
    };

    while (pos < len) {
        if (text[pos] == '[' && pos + 1 < len && text[pos + 1] == '&') {
            flushAccum(COL_BODY, RichToken::Plain);
            size_t end = text.find(']', pos);
            if (end != std::string::npos) {
                tokens.push_back({RichToken::ChatLink, text.substr(pos, end - pos + 1), COL_CHATLINK});
                pos = end + 1;
                continue;
            }
        }
        if (pos + 1 < len && text[pos] == '*' && text[pos + 1] == '*') {
            flushAccum(COL_BODY, RichToken::Plain);
            size_t end = text.find("**", pos + 2);
            if (end != std::string::npos) {
                std::string bold = text.substr(pos + 2, end - pos - 2);
                accum = bold;
                flushAccum(COL_BOLD, RichToken::Bold);
                pos = end + 2;
                continue;
            }
        }
        accum += text[pos];
        pos++;
    }
    flushAccum(COL_BODY, RichToken::Plain);
    return tokens;
}

void ChatWindow::RenderFormattedText(const std::string& text) {
    auto tokens = Tokenize(text);
    bool firstOnLine = true;
    int linkId = 0;
    float maxX = ImGui::GetContentRegionMax().x;

    for (auto& tok : tokens) {
        if (tok.type == RichToken::Newline) {
            ImGui::NewLine();
            firstOnLine = true;
            continue;
        }

        ImVec2 sz = ImGui::CalcTextSize(tok.text.c_str());
        if (tok.type == RichToken::ChatLink) sz.x += 8.0f;

        if (!firstOnLine) {
            ImGui::SameLine(0, 0);
            if (ImGui::GetCursorPosX() + sz.x > maxX) {
                ImGui::NewLine();
            }
        }

        if (tok.type == RichToken::ChatLink) {
            ImGui::PushID(linkId++);
            ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.20f, 0.40f, 0.45f, 0.30f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered,  ImVec4(0.25f, 0.50f, 0.55f, 0.50f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,   ImVec4(0.30f, 0.60f, 0.65f, 0.70f));
            ImGui::PushStyleColor(ImGuiCol_Text, COL_CHATLINK);
            if (ImGui::Selectable(tok.text.c_str(), false, 0, ImVec2(sz.x, 0))) {
                CopyToClipboard(tok.text);
            }
            ImGui::PopStyleColor(4);
            bool justCopied = m_copiedText == tok.text &&
                std::chrono::steady_clock::now() - m_copiedAt < std::chrono::milliseconds(1500);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(justCopied ? "Kopyalandi!" : "Tikla: kopyala (oyun icine yapistir)");
            ImGui::PopID();
        } else {
            ImGui::TextColored(tok.color, "%s", tok.text.c_str());
        }
        firstOnLine = false;
    }
}

void ChatWindow::Render(Worker* worker, bool* pOpen) {
    if (!*pOpen) return;

    PushGW2Style();

    ImGui::SetNextWindowSizeConstraints(ImVec2(280, 220), ImVec2(800, 900));

    if (!ImGui::Begin("Claymore Asistan##chat", pOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        PopGW2Style();
        return;
    }

    ChatSnapshot snap = worker->GetChatSnapshot();

    if (!snap.model.empty()) {
        std::string modelTag = snap.model;
        float tagWidth = ImGui::CalcTextSize(modelTag.c_str()).x;
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::SameLine(avail - tagWidth);
        ImGui::PushStyleColor(ImGuiCol_Text, COL_DIM);
        ImGui::Text("%s", modelTag.c_str());
        ImGui::PopStyleColor();
    }

    float inputAreaHeight = ImGui::GetFrameHeightWithSpacing() + 6.0f;
    ImGui::BeginChild("##chatarea", ImVec2(0, -inputAreaHeight), false);

    if (snap.messages.empty() && !snap.busy) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, COL_DIM);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextWrapped("GW2 hakkinda bir soru sor...\n\nOrnek:\n- Dusk nedir?\n- Ascended armor nasil yapilir?\n- Queensdale'de en yakin waypoint?");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

    for (size_t i = 0; i < snap.messages.size(); ++i) {
        const auto& msg = snap.messages[i];

        if (i > 0) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.35f, 0.28f, 0.12f, 0.20f));
            ImGui::Separator();
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }

        ImGui::PushID(static_cast<int>(i));

        if (msg.role == ChatMessage::User) {
            ImGui::PushStyleColor(ImGuiCol_Text, COL_USER);
            ImGui::Text("Sen:");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, COL_BODY);
            ImGui::TextWrapped("%s", msg.text.c_str());
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
        } else if (msg.role == ChatMessage::Assistant) {
            ImGui::PushStyleColor(ImGuiCol_Text, COL_ASSISTANT);
            ImGui::Text("Claymore:");
            ImGui::PopStyleColor();
            ImGui::Spacing();
            RenderFormattedText(msg.text);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, COL_SYSTEM);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextWrapped("%s", msg.text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }

        ImGui::PopID();
    }

    if (snap.busy) {
        if (!snap.messages.empty()) ImGui::Spacing();
        int dots = static_cast<int>(fmod(ImGui::GetTime() * 2.0, 4.0));
        const char* anim[] = {"Dusunuyor", "Dusunuyor.", "Dusunuyor..", "Dusunuyor..."};
        ImGui::PushStyleColor(ImGuiCol_Text, COL_ASSISTANT);
        ImGui::Text("%s", anim[dots]);
        ImGui::PopStyleColor();
    }

    if (snap.messages.size() != m_lastMsgCount) {
        ImGui::SetScrollHereY(1.0f);
        m_lastMsgCount = snap.messages.size();
    }
    if (m_scrollToBottom) {
        ImGui::SetScrollHereY(1.0f);
        m_scrollToBottom = false;
    }

    ImGui::EndChild();

    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.45f, 0.36f, 0.14f, 0.35f));
    ImGui::Separator();
    ImGui::PopStyleColor();

    float buttonWidth = 56.0f;
    float inputWidth = ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x;

    bool disabled = snap.busy;
    if (disabled)
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.45f);

    ImGui::PushItemWidth(inputWidth);
    if (m_refocus) {
        ImGui::SetKeyboardFocusHere();
        m_refocus = false;
    }
    bool enterPressed = ImGui::InputText("##chatinput", m_inputBuf, sizeof(m_inputBuf),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    bool sendClicked = ImGui::Button("Sor##send", ImVec2(buttonWidth, 0));

    if (disabled)
        ImGui::PopStyleVar();

    if ((enterPressed || sendClicked) && !disabled && m_inputBuf[0] != '\0') {
        worker->RequestChat(m_inputBuf);
        m_inputBuf[0] = '\0';
        m_scrollToBottom = true;
        m_refocus = true;
    }

    ImGui::End();
    PopGW2Style();
}
