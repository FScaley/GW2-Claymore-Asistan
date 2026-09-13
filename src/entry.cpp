#include <Windows.h>
#include <string>
#include <filesystem>
#include "nexus/Nexus.h"
#include "mumble/Mumble.h"
#include "imgui/imgui.h"
#include "core/ConfigManager.h"
#include "core/Worker.h"
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
std::string g_configPath;
std::string g_addonDir;
bool g_showWindow = true;

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
    AddonDef.Version.Minor = 1;
    AddonDef.Version.Build = 0;
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

    g_chatWindow = new ChatWindow();
    g_worker = new Worker();
    g_worker->Start(g_config);

    g_showWindow = true;

    APIDefs->GUI_Register(RT_Render, AddonRender);
    APIDefs->GUI_Register(RT_OptionsRender, AddonOptions);
    APIDefs->InputBinds_RegisterWithString(KB_ID, OnKeybind, "ALT+C");
    APIDefs->QuickAccess_Add(QA_ID, "ICON_CLAYMORE", "ICON_CLAYMORE_HOVER", KB_ID, "Claymore Asistan");

    APIDefs->Textures_LoadFromURL("ICON_CLAYMORE",
        "https://wiki.guildwars2.com", "/images/3/37/Crimson_Antique_Claymore.png", nullptr);
    APIDefs->Textures_LoadFromURL("ICON_CLAYMORE_HOVER",
        "https://wiki.guildwars2.com", "/images/3/37/Crimson_Antique_Claymore.png", nullptr);

    APIDefs->Log(LOGL_INFO, "Claymore", "Claymore Asistan v0.1.0 loaded.");
}

void AddonUnload() {
    APIDefs->GUI_Deregister(AddonRender);
    APIDefs->GUI_Deregister(AddonOptions);
    APIDefs->QuickAccess_Remove(QA_ID);
    APIDefs->InputBinds_Deregister(KB_ID);

    if (g_worker) { g_worker->Stop(); delete g_worker; g_worker = nullptr; }
    if (g_chatWindow) { delete g_chatWindow; g_chatWindow = nullptr; }
    if (g_config) {
        g_config->Save(g_configPath);
        delete g_config; g_config = nullptr;
    }

    APIDefs->Log(LOGL_INFO, "Claymore", "Claymore Asistan unloaded.");
}

void AddonRender() {
    if (!g_showWindow || !g_chatWindow || !g_worker) return;
    g_chatWindow->Render(g_worker, &g_showWindow);
}

void AddonOptions() {
    if (!g_config) return;
    ImGui::Text("Claymore Asistan v0.1.0");
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
        g_config->SetApiKey(keyBuf);
        g_config->Save(g_configPath);
        if (g_worker) {
            g_worker->Stop();
            g_worker->Start(g_config);
        }
    }

    if (g_config->GetApiKey().empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "API key girilmedi!");
    } else {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "API key aktif");
    }
}
