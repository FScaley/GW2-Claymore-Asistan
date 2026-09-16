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
static const ImVec4 COL_TOOL      = ImVec4(0.40f, 0.90f, 0.60f, 1.0f);
static const ImVec4 COL_CODE      = ImVec4(0.80f, 0.84f, 0.90f, 1.0f);
static const ImVec4 COL_HEADER    = ImVec4(0.96f, 0.86f, 0.45f, 1.0f);
static const ImVec4 COL_MARKER    = ImVec4(0.75f, 0.70f, 0.50f, 1.0f);

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

static ImVec4 StyleColor(Markdown::Style s) {
    switch (s) {
        case Markdown::Style::Bold:       return COL_BOLD;
        case Markdown::Style::InlineCode: return COL_CODE;
        case Markdown::Style::ChatLink:   return COL_CHATLINK;
        default:                          return COL_BODY;
    }
}

void ChatWindow::RenderTokens(const std::vector<Markdown::Token>& tokens, float maxX,
                              int& linkId, const ImVec4* overrideColor) {
    bool firstOnLine = true;
    for (auto& tok : tokens) {
        ImVec2 sz = ImGui::CalcTextSize(tok.text.c_str());
        if (tok.style == Markdown::Style::ChatLink) sz.x += 8.0f;

        if (!firstOnLine) {
            ImGui::SameLine(0, 0);
            if (ImGui::GetCursorPosX() + sz.x > maxX)
                ImGui::NewLine();
        }

        if (tok.style == Markdown::Style::ChatLink) {
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
            ImVec4 col = overrideColor ? *overrideColor : StyleColor(tok.style);
            ImGui::TextColored(col, "%s", tok.text.c_str());
        }
        firstOnLine = false;
    }
}

void ChatWindow::RenderFormattedText(const std::string& text) {
    auto lines = Markdown::Parse(text);
    int linkId = 0;
    float maxX = ImGui::GetContentRegionMax().x;
    float indentUnit = ImGui::CalcTextSize("    ").x;
    float spaceW = ImGui::CalcTextSize(" ").x;
    bool prevBlank = false;

    for (auto& ln : lines) {
        if (ln.kind == Markdown::LineKind::Blank) {
            if (!prevBlank) ImGui::Spacing();
            prevBlank = true;
            continue;
        }
        prevBlank = false;

        if (ln.kind == Markdown::LineKind::Rule) {
            ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.93f, 0.91f, 0.67f, 0.25f));
            ImGui::Separator();
            ImGui::PopStyleColor();
            continue;
        }

        float ind = indentUnit * ln.indent;
        if (ind > 0) ImGui::Indent(ind);

        if (ln.kind == Markdown::LineKind::Header) {
            ImGui::Spacing();
            RenderTokens(ln.tokens, maxX, linkId, &COL_HEADER);
            ImGui::Spacing();
        } else if (ln.kind == Markdown::LineKind::Bullet || ln.kind == Markdown::LineKind::Numbered) {
            std::string marker = (ln.kind == Markdown::LineKind::Bullet) ? "-" : ln.marker;
            ImGui::TextColored(COL_MARKER, "%s", marker.c_str());
            float mw = ImGui::CalcTextSize(marker.c_str()).x + spaceW;
            ImGui::Indent(mw);
            ImGui::SameLine(0, spaceW);
            if (ln.tokens.empty()) ImGui::NewLine();
            else RenderTokens(ln.tokens, maxX, linkId, nullptr);
            ImGui::Unindent(mw);
        } else {
            RenderTokens(ln.tokens, maxX, linkId, nullptr);
        }

        if (ind > 0) ImGui::Unindent(ind);
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

    if (!snap.activeModel.empty()) {
        std::string modelTag = snap.activeModel;
        if (snap.fallbackUsed)
            modelTag += " (yedek)";
        float tagWidth = ImGui::CalcTextSize(modelTag.c_str()).x;
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::SameLine(avail - tagWidth);
        ImGui::PushStyleColor(ImGuiCol_Text, snap.fallbackUsed
            ? ImVec4(1.0f, 0.75f, 0.30f, 1.0f) : COL_DIM);
        ImGui::Text("%s", modelTag.c_str());
        if (snap.fallbackUsed && ImGui::IsItemHovered())
            ImGui::SetTooltip("Birincil model kota asiminda - yedek model kullanildi.\nFree API key ile normaldir.");
        ImGui::PopStyleColor();
    }

    float inputAreaHeight = ImGui::GetFrameHeightWithSpacing() + 6.0f;
    ImGui::BeginChild("##chatarea", ImVec2(0, -inputAreaHeight), false);

    if (snap.messages.empty() && !snap.busy) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, COL_DIM);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextWrapped("GW2 hakkinda bir soru sor...\n\nOrnek:\n- Dusk kac altin?\n- Deldrimor Steel Ingot tarifi nedir?\n- Miyani nerede?\n- Roller Beetle nasil acilir?");
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
        ImGui::BeginGroup();

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
        ImGui::EndGroup();

        if (ImGui::BeginPopupContextItem("msg_ctx")) {
            if (ImGui::MenuItem("Mesaji kopyala")) {
                CopyToClipboard(msg.text);
            }
            if (snap.messages.size() > 1 && ImGui::MenuItem("Tum sohbeti kopyala")) {
                std::string all;
                for (const auto& m : snap.messages) {
                    if (!all.empty()) all += "\n\n";
                    if (m.role == ChatMessage::User) all += "Sen: ";
                    else if (m.role == ChatMessage::Assistant) all += "Claymore: ";
                    all += m.text;
                }
                CopyToClipboard(all);
            }
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    if (snap.busy) {
        if (!snap.messages.empty()) ImGui::Spacing();

        if (!snap.toolStatus.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, COL_TOOL);
            int dots = static_cast<int>(fmod(ImGui::GetTime() * 2.0, 4.0));
            std::string dotStr(dots, '.');
            ImGui::Text("%s%s", snap.toolStatus.c_str(), dotStr.c_str());
            ImGui::PopStyleColor();
        } else {
            int dots = static_cast<int>(fmod(ImGui::GetTime() * 2.0, 4.0));
            const char* anim[] = {"Dusunuyor", "Dusunuyor.", "Dusunuyor..", "Dusunuyor..."};
            ImGui::PushStyleColor(ImGuiCol_Text, COL_ASSISTANT);
            ImGui::Text("%s", anim[dots]);
            ImGui::PopStyleColor();
        }
    }

    if (snap.messages.size() != m_lastMsgCount) {
        ImGui::SetScrollHereY(1.0f);
        m_lastMsgCount = snap.messages.size();
    }
    if (m_scrollToBottom) {
        ImGui::SetScrollHereY(1.0f);
        m_scrollToBottom = false;
    }

    if (!snap.messages.empty() && ImGui::BeginPopupContextWindow("chat_area_ctx",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Tum sohbeti kopyala")) {
            std::string all;
            for (const auto& m : snap.messages) {
                if (!all.empty()) all += "\n\n";
                if (m.role == ChatMessage::User) all += "Sen: ";
                else if (m.role == ChatMessage::Assistant) all += "Claymore: ";
                all += m.text;
            }
            CopyToClipboard(all);
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();

    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.45f, 0.36f, 0.14f, 0.35f));
    ImGui::Separator();
    ImGui::PopStyleColor();

    float clearWidth = 56.0f;
    float cancelWidth = 56.0f;
    float sendWidth = 56.0f;
    float spacing = ImGui::GetStyle().ItemSpacing.x;

    if (snap.busy) {
        float inputWidth = ImGui::GetContentRegionAvail().x - cancelWidth - spacing;
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.45f);
        ImGui::PushItemWidth(inputWidth);
        ImGui::InputText("##chatinput", m_inputBuf, sizeof(m_inputBuf));
        ImGui::PopItemWidth();
        ImGui::PopStyleVar();
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.25f, 0.20f, 0.50f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.30f, 0.25f, 0.70f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.00f, 0.35f, 0.30f, 0.90f));
        if (ImGui::Button("Iptal##cancel", ImVec2(cancelWidth, 0))) {
            worker->CancelChat();
        }
        ImGui::PopStyleColor(3);
    } else {
        float inputWidth = ImGui::GetContentRegionAvail().x - sendWidth - clearWidth - spacing * 2;

        ImGui::PushItemWidth(inputWidth);
        if (m_refocus) {
            ImGui::SetKeyboardFocusHere();
            m_refocus = false;
        }
        bool enterPressed = ImGui::InputText("##chatinput", m_inputBuf, sizeof(m_inputBuf),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopItemWidth();
        ImGui::SameLine();
        bool sendClicked = ImGui::Button("Sor##send", ImVec2(sendWidth, 0));
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.40f, 0.35f, 0.25f, 0.30f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.50f, 0.45f, 0.30f, 0.50f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.60f, 0.55f, 0.40f, 0.70f));
        if (ImGui::Button("Temizle##clear", ImVec2(clearWidth, 0))) {
            worker->ClearHistory();
            m_lastMsgCount = 0;
        }
        ImGui::PopStyleColor(3);

        if ((enterPressed || sendClicked) && m_inputBuf[0] != '\0') {
            worker->RequestChat(m_inputBuf);
            m_inputBuf[0] = '\0';
            m_scrollToBottom = true;
            m_refocus = true;
        }
    }

    ImGui::End();
    PopGW2Style();
}
