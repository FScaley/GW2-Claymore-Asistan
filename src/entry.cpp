#include <Windows.h>
#include <string>
#include <filesystem>
#include "nexus/Nexus.h"
#include "mumble/Mumble.h"
#include "imgui/imgui.h"
#include "core/ConfigManager.h"
#include "core/HttpClient.h"
#include "core/Worker.h"
#include "core/GW2Client.h"
#include "core/ItemIndex.h"
#include "core/FunctionHandler.h"
#include "core/ChatHistory.h"
#include "chat/ChatWindow.h"
#include "map/MarkerOverlay.h"
#include "icon_data.h"

void AddonLoad(AddonAPI_t* aApi);
void AddonUnload();
void AddonRender();
void AddonOptions();
void OverlayRender();

static constexpr int VER_MAJOR = 0;
static constexpr int VER_MINOR = 5;
static constexpr int VER_BUILD = 13;
#define CLAYMORE_VERSION_STR "0.5.13"

AddonDefinition_t AddonDef = {};
HMODULE hSelf = nullptr;
AddonAPI_t* APIDefs = nullptr;
NexusLinkData_t* NexusLink = nullptr;
Mumble::Data* MumbleLink = nullptr;
Mumble::Identity* MumbleIdent = nullptr;

ConfigManager* g_config = nullptr;
Worker* g_worker = nullptr;
ChatWindow* g_chatWindow = nullptr;
GW2Client* g_gw2 = nullptr;
ItemIndex* g_itemIndex = nullptr;
FunctionHandler* g_funcHandler = nullptr;
std::string g_configPath;
std::string g_addonDir;
bool g_showWindow = true;
ImFont* g_font = nullptr;
MarkerOverlay* g_overlay = nullptr;
ChatHistory* g_history = nullptr;
uint64_t g_lastEntitySeq = 0;

static const char* QA_ID = "QA_CLAYMORE";
static const char* KB_ID = "KB_CLAYMORE_TOGGLE";

void OnFontReceived(const char* aIdentifier, void* aFont) {
    g_font = (ImFont*)aFont;
}

void OnIconReceived(const char* aIdentifier, Texture_t* aTexture) {
    if (aTexture && APIDefs)
        APIDefs->QuickAccess_Add(QA_ID, "ICON_CLA", "ICON_CLA", KB_ID, "Claymore Law Asistan");
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason, LPVOID) {
    if (ul_reason == DLL_PROCESS_ATTACH) hSelf = hModule;
    return TRUE;
}

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    AddonDef.Signature = -77043;
    AddonDef.APIVersion = NEXUS_API_VERSION;
    AddonDef.Name = "Claymore Law Asistan";
    AddonDef.Version.Major = VER_MAJOR;
    AddonDef.Version.Minor = VER_MINOR;
    AddonDef.Version.Build = VER_BUILD;
    AddonDef.Version.Revision = 0;
    AddonDef.Author = "Onur";
    AddonDef.Description = "GW2 AI Asistan - Gemini destekli oyun ici yardimci";
    AddonDef.Load = AddonLoad;
    AddonDef.Unload = AddonUnload;
    AddonDef.Flags = AF_None;
    AddonDef.Provider = UP_GitHub;
    AddonDef.UpdateLink = "https://github.com/FScaley/GW2-Claymore-Asistan";
    return &AddonDef;
}

void OnKeybind(const char* aIdentifier, bool aIsRelease) {
    if (!aIsRelease && std::string(aIdentifier) == KB_ID)
        g_showWindow = !g_showWindow;
}

void AddonLoad(AddonAPI_t* aApi) {
    APIDefs = aApi;
    ImGui::SetCurrentContext((ImGuiContext*)APIDefs->ImguiContext);
    ImGui::SetAllocatorFunctions(
        (void*(*)(size_t, void*))APIDefs->ImguiMalloc,
        (void(*)(void*, void*))APIDefs->ImguiFree);

    NexusLink = (NexusLinkData_t*)APIDefs->DataLink_Get("DL_NEXUS_LINK");
    MumbleLink = (Mumble::Data*)APIDefs->DataLink_Get("DL_MUMBLE_LINK");
    MumbleIdent = (Mumble::Identity*)APIDefs->DataLink_Get("DL_MUMBLE_LINK_IDENTITY");

    g_addonDir = APIDefs->Paths_GetAddonDirectory("claymore-asistan");
    std::filesystem::create_directories(g_addonDir);
    g_configPath = g_addonDir + "\\config.json";

    g_config = new ConfigManager();
    if (!g_config->Load(g_configPath)) {
        g_config->Save(g_configPath);
    }

    auto* api = APIDefs;
    auto logger = [api](const std::string& msg) {
        api->Log(LOGL_WARNING, "Claymore", msg.c_str());
    };

    g_gw2 = new GW2Client();
    g_gw2->SetLogger(logger);
    g_itemIndex = new ItemIndex();
    g_itemIndex->Load(g_addonDir + "\\items_index.json");
    g_funcHandler = new FunctionHandler(g_gw2, g_itemIndex);
    g_funcHandler->SetLogger(logger);
    g_funcHandler->SetGw2ApiKey(g_config->GetGw2ApiKey());

    g_history = new ChatHistory(g_addonDir + "\\history");
    g_chatWindow = new ChatWindow();
    g_overlay = new MarkerOverlay();
    g_worker = new Worker();
    g_worker->Start(g_config, g_funcHandler, logger, g_history);

    g_showWindow = true;

    APIDefs->GUI_Register(RT_Render, AddonRender);
    APIDefs->GUI_Register(RT_Render, OverlayRender);
    APIDefs->GUI_Register(RT_OptionsRender, AddonOptions);
    APIDefs->InputBinds_RegisterWithString(KB_ID, OnKeybind, "ALT+C");
    APIDefs->Textures_GetOrCreateFromMemory("ICON_CLA", (void*)ICON_CLA_PNG, ICON_CLA_PNG_SIZE);
    APIDefs->QuickAccess_Add(QA_ID, "ICON_CLA", "ICON_CLA", KB_ID, "Claymore Law Asistan");

    char fontPath[MAX_PATH];
    GetWindowsDirectoryA(fontPath, MAX_PATH);
    strcat_s(fontPath, "\\Fonts\\segoeui.ttf");
    APIDefs->Fonts_AddFromFile("FONT_CLAYMORE", 16.0f, fontPath, OnFontReceived, nullptr);

    APIDefs->Log(LOGL_INFO, "Claymore", "Claymore Law Asistan v" CLAYMORE_VERSION_STR " loaded.");

    // Field diagnostics for "works for everyone but me": Windows version, whether a proxy exists
    // that WinHTTP (DEFAULT_PROXY) would ignore, and the key's shape - never the key.
    APIDefs->Log(LOGL_INFO, "Claymore", HttpClient::Diagnostics().c_str());
    {
        const std::string& key = g_config->GetApiKey();
        std::string problem = ConfigManager::ApiKeyProblem(key);
        std::string status = key.empty()
            ? std::string("API anahtari: yok (Options > Claymore Law Asistan)")
            : "API anahtari: " + std::to_string(key.size()) + " karakter"
              + (problem.empty() ? std::string(", gecerli") : " - SORUN: " + problem);
        APIDefs->Log(problem.empty() ? LOGL_INFO : LOGL_WARNING, "Claymore", status.c_str());
    }
}

void AddonUnload() {
    APIDefs->Fonts_Release("FONT_CLAYMORE", OnFontReceived);
    g_font = nullptr;

    APIDefs->GUI_Deregister(AddonRender);
    APIDefs->GUI_Deregister(OverlayRender);
    APIDefs->GUI_Deregister(AddonOptions);
    APIDefs->QuickAccess_Remove(QA_ID);
    APIDefs->InputBinds_Deregister(KB_ID);

    if (g_worker) { g_worker->Stop(); delete g_worker; g_worker = nullptr; }
    if (g_overlay) { delete g_overlay; g_overlay = nullptr; }
    if (g_chatWindow) { delete g_chatWindow; g_chatWindow = nullptr; }
    if (g_history) { delete g_history; g_history = nullptr; }

    if (g_itemIndex) {
        g_itemIndex->Save(g_addonDir + "\\items_index.json");
        delete g_itemIndex; g_itemIndex = nullptr;
    }
    if (g_funcHandler) { delete g_funcHandler; g_funcHandler = nullptr; }
    if (g_gw2) { delete g_gw2; g_gw2 = nullptr; }

    if (g_config) {
        g_config->Save(g_configPath);
        delete g_config; g_config = nullptr;
    }

    APIDefs->Log(LOGL_INFO, "Claymore", "Claymore Law Asistan unloaded.");
}

void AddonRender() {
    if (!g_showWindow || !g_chatWindow || !g_worker) return;
    ImFont* f = g_font;
    if (f) ImGui::PushFont(f);
    g_chatWindow->Render(g_worker, &g_showWindow);
    if (f) ImGui::PopFont();
}

void OverlayRender() {
    if (!g_overlay || !g_worker || !MumbleLink) return;

    uint64_t seq = g_worker->GetEntitySeq();
    if (seq != g_lastEntitySeq && seq > 0) {
        g_lastEntitySeq = seq;
        auto snap = g_worker->GetChatSnapshot();
        g_overlay->SetTarget(snap.entityCoords, snap.mapRects);
    }

    g_overlay->Render(MumbleLink, MumbleIdent, NexusLink);
}

void AddonOptions() {
    if (!g_config) return;
    ImFont* f = g_font;
    if (f) ImGui::PushFont(f);
    ImGui::Text("Claymore Law Asistan v" CLAYMORE_VERSION_STR);
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Font testi: \xc4\x9f\xc3\xbc\xc5\x9f\xc4\xb1\xc3\xb6\xc3\xa7\xc4\xb0\xc4\x9e\xc5\x9e");
    ImGui::Separator();

    static char keyBuf[256] = "";
    static bool keyLoaded = false;
    if (!keyLoaded) {
        strncpy_s(keyBuf, g_config->GetApiKey().c_str(), sizeof(keyBuf) - 1);
        keyLoaded = true;
    }
    ImGui::Text("Gemini API Key:");
    ImGui::InputText("##apikey", keyBuf, sizeof(keyBuf), ImGuiInputTextFlags_Password);
    ImGui::SameLine();
    if (ImGui::Button("Kaydet")) {
        g_config->SetApiKey(keyBuf);                                        // sanitized (whitespace, NBSP, quotes)
        strncpy_s(keyBuf, g_config->GetApiKey().c_str(), sizeof(keyBuf) - 1);
        g_config->Save(g_configPath);
        if (g_worker) {
            g_worker->Stop();
            auto* api2 = APIDefs;
            g_worker->Start(g_config, g_funcHandler, [api2](const std::string& msg) {
                api2->Log(LOGL_WARNING, "Claymore", msg.c_str());
            }, g_history);
        }
    }

    if (g_config->GetApiKey().empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "API key girilmedi!");
    } else {
        std::string problem = ConfigManager::ApiKeyProblem(g_config->GetApiKey());
        if (!problem.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", problem.c_str());
        else
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "API key aktif");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("GW2 API Key:");
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Achievement ilerlemesi icin. Tum izinleri secin.");

    static char gw2KeyBuf[256] = "";
    static bool gw2KeyLoaded = false;
    if (!gw2KeyLoaded) {
        strncpy_s(gw2KeyBuf, g_config->GetGw2ApiKey().c_str(), sizeof(gw2KeyBuf) - 1);
        gw2KeyLoaded = true;
    }
    ImGui::InputText("##gw2apikey", gw2KeyBuf, sizeof(gw2KeyBuf), ImGuiInputTextFlags_Password);
    ImGui::SameLine();
    if (ImGui::Button("Kaydet##gw2")) {
        g_config->SetGw2ApiKey(gw2KeyBuf);
        strncpy_s(gw2KeyBuf, g_config->GetGw2ApiKey().c_str(), sizeof(gw2KeyBuf) - 1);
        g_config->Save(g_configPath);
        if (g_funcHandler) g_funcHandler->SetGw2ApiKey(g_config->GetGw2ApiKey());
    }

    if (g_config->GetGw2ApiKey().empty()) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "GW2 API key girilmedi (opsiyonel)");
    } else {
        std::string problem = ConfigManager::ApiKeyProblem(g_config->GetGw2ApiKey());
        if (!problem.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", problem.c_str());
        else
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "GW2 API key aktif");
    }
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Ilerleme 5-15 dk gecikmelidir (GW2 API cache).");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Model Zinciri:");
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Birincil modelde kota asiminda sirasiyla sonrakiler denenir.");

    static const char* KNOWN_MODELS[] = {
        "gemini-3.5-flash",
        "gemini-3.5-flash-lite",
        "gemini-3.8-flash"
    };
    static const int MODEL_COUNT = 3;

    auto& chain = g_config->GetModelChain();
    auto& defaults = ConfigManager::DEFAULT_MODEL_CHAIN;
    auto& active = chain.empty() ? defaults : chain;

    for (size_t i = 0; i < active.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::Text("%zu.", i + 1);
        ImGui::SameLine();

        int sel = -1;
        for (int k = 0; k < MODEL_COUNT; ++k)
            if (active[i] == KNOWN_MODELS[k]) { sel = k; break; }

        ImGui::SetNextItemWidth(220);
        if (ImGui::Combo("##model", &sel, KNOWN_MODELS, MODEL_COUNT) && sel >= 0) {
            auto copy = active;
            copy[i] = KNOWN_MODELS[sel];
            g_config->SetModelChain(copy);
            g_config->Save(g_configPath);
            if (g_worker) {
                g_worker->Stop();
                auto* api2 = APIDefs;
                g_worker->Start(g_config, g_funcHandler, [api2](const std::string& msg) {
                    api2->Log(LOGL_WARNING, "Claymore", msg.c_str());
                }, g_history);
            }
        }

        if (i > 0) {
            ImGui::SameLine();
            if (ImGui::SmallButton("\xe2\x96\xb2")) {
                auto copy = active;
                std::swap(copy[i], copy[i - 1]);
                g_config->SetModelChain(copy);
                g_config->Save(g_configPath);
            }
        }
        if (i + 1 < active.size()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("\xe2\x96\xbc")) {
                auto copy = active;
                std::swap(copy[i], copy[i + 1]);
                g_config->SetModelChain(copy);
                g_config->Save(g_configPath);
            }
        }
        ImGui::PopID();
    }

    if (g_itemIndex) {
        size_t count = g_itemIndex->Size();
        if (count > 0)
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Item cache: %zu item", count);
    }

    if (f) ImGui::PopFont();
}
