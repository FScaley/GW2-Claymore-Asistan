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
#include "chat/ChatWindow.h"

void AddonLoad(AddonAPI_t* aApi);
void AddonUnload();
void AddonRender();
void AddonOptions();

AddonDefinition_t AddonDef = {};
HMODULE hSelf = nullptr;
AddonAPI_t* APIDefs = nullptr;
NexusLinkData_t* NexusLink = nullptr;
Mumble::Data* MumbleLink = nullptr;

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

void OnFontReceived(const char* aIdentifier, void* aFont) {
    g_font = (ImFont*)aFont;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason, LPVOID) {
    if (ul_reason == DLL_PROCESS_ATTACH) hSelf = hModule;
    return TRUE;
}

static const char* QA_ID = "QA_CLAYMORE";
static const char* KB_ID = "KB_CLAYMORE_TOGGLE";

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    AddonDef.Signature = -77043;
    AddonDef.APIVersion = NEXUS_API_VERSION;
    AddonDef.Name = "Claymore Asistan";
    AddonDef.Version.Major = 0;
    AddonDef.Version.Minor = 3;
    AddonDef.Version.Build = 17;
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

    g_chatWindow = new ChatWindow();
    g_worker = new Worker();
    g_worker->Start(g_config, g_funcHandler, logger);

    g_showWindow = true;

    APIDefs->GUI_Register(RT_Render, AddonRender);
    APIDefs->GUI_Register(RT_OptionsRender, AddonOptions);
    APIDefs->InputBinds_RegisterWithString(KB_ID, OnKeybind, "ALT+C");
    APIDefs->QuickAccess_Add(QA_ID, "ICON_CLAYMORE", "ICON_CLAYMORE_HOVER", KB_ID, "Claymore Asistan");

    APIDefs->Textures_LoadFromURL("ICON_CLAYMORE",
        "https://wiki.guildwars2.com", "/images/3/37/Crimson_Antique_Claymore.png", nullptr);
    APIDefs->Textures_LoadFromURL("ICON_CLAYMORE_HOVER",
        "https://wiki.guildwars2.com", "/images/3/37/Crimson_Antique_Claymore.png", nullptr);

    char fontPath[MAX_PATH];
    GetWindowsDirectoryA(fontPath, MAX_PATH);
    strcat_s(fontPath, "\\Fonts\\segoeui.ttf");
    APIDefs->Fonts_AddFromFile("FONT_CLAYMORE", 16.0f, fontPath, OnFontReceived, nullptr);

    APIDefs->Log(LOGL_INFO, "Claymore", "Claymore Asistan v0.3.17 loaded.");

    // Field diagnostics for "works for everyone but me": Windows version, whether a proxy exists
    // that WinHTTP (DEFAULT_PROXY) would ignore, and the key's shape - never the key.
    APIDefs->Log(LOGL_INFO, "Claymore", HttpClient::Diagnostics().c_str());
    {
        const std::string& key = g_config->GetApiKey();
        std::string problem = ConfigManager::ApiKeyProblem(key);
        std::string status = key.empty()
            ? std::string("API anahtari: yok (Options > Claymore Asistan)")
            : "API anahtari: " + std::to_string(key.size()) + " karakter"
              + (problem.empty() ? std::string(", gecerli") : " - SORUN: " + problem);
        APIDefs->Log(problem.empty() ? LOGL_INFO : LOGL_WARNING, "Claymore", status.c_str());
    }
}

void AddonUnload() {
    APIDefs->Fonts_Release("FONT_CLAYMORE", OnFontReceived);
    g_font = nullptr;

    APIDefs->GUI_Deregister(AddonRender);
    APIDefs->GUI_Deregister(AddonOptions);
    APIDefs->QuickAccess_Remove(QA_ID);
    APIDefs->InputBinds_Deregister(KB_ID);

    if (g_worker) { g_worker->Stop(); delete g_worker; g_worker = nullptr; }
    if (g_chatWindow) { delete g_chatWindow; g_chatWindow = nullptr; }

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

    APIDefs->Log(LOGL_INFO, "Claymore", "Claymore Asistan unloaded.");
}

void AddonRender() {
    if (!g_showWindow || !g_chatWindow || !g_worker) return;
    ImFont* f = g_font;
    if (f) ImGui::PushFont(f);
    g_chatWindow->Render(g_worker, &g_showWindow);
    if (f) ImGui::PopFont();
}

void AddonOptions() {
    if (!g_config) return;
    ImFont* f = g_font;
    if (f) ImGui::PushFont(f);
    ImGui::Text("Claymore Asistan v0.3.17");
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
            });
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
    auto& chain = g_config->GetModelChain();
    std::string chainStr;
    for (size_t i = 0; i < chain.size(); ++i) {
        if (i > 0) chainStr += " > ";
        chainStr += chain[i];
    }
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Model zinciri: %s", chainStr.c_str());

    if (g_itemIndex) {
        size_t count = g_itemIndex->Size();
        if (count > 0)
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Item cache: %zu item", count);
    }

    if (f) ImGui::PopFont();
}
