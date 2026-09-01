#include "UI.h"
#include "ixwebsocket_wrapper.h"
#include "logger.h";
#include "SimpleIni.h"
#include "SKSEMenuFramework.h"
#include "translation.h"

void SaveHttpClientSettings() {
    CSimpleIniA ini;
    ini.SetUnicode();
    ini.LoadFile(iniPath);

    ini.SetBoolValue("HttpClient", "bEnabled", g_httpClientEnabled.load(std::memory_order_relaxed));
    ini.SetBoolValue("WebSocketClient", "bEnabled", g_websocketClientEnabled.load(std::memory_order_relaxed));

    SI_Error rc = ini.SaveFile("Data/SKSE/Plugins/ixwebsocket_skse.ini");
    if (rc < 0) {
        logger::warn("Failed to save ixwebsocket_skse.ini");
    }
}

void __stdcall UI::Render() {
    httpEnabled = g_httpClientEnabled.load(std::memory_order_relaxed);
    if (ImGuiMCP::Checkbox((g_translations.Get("EnableHttpClient") + "##NetworkClientUtil").c_str(), &httpEnabled)) {
        g_httpClientEnabled.store(httpEnabled, std::memory_order_relaxed);
        SaveHttpClientSettings();
    }

    ImGuiMCP::SameLine();

    httpLogging = g_httpLoggingEnabled.load(std::memory_order_relaxed);

    if (ImGuiMCP::Checkbox((g_translations.Get("EnableHttpLogging") + "##NetworkClientUtil").c_str(), &httpLogging)) {
        g_httpLoggingEnabled.store(httpLogging, std::memory_order_relaxed);
    }

    ImGuiMCP::Separator();

    wsEnabled = g_websocketClientEnabled.load(std::memory_order_relaxed);
    if (ImGuiMCP::Checkbox((g_translations.Get("EnableWebSocketConnect") + "##NetworkClientUtil").c_str(), &wsEnabled)) {
        g_websocketClientEnabled.store(wsEnabled, std::memory_order_relaxed);
        SaveHttpClientSettings();
    }

    ImGuiMCP::Text("%s", g_translations.Get("ConnectedClients").c_str());

    // for now, let's limit our checks to once per second
    auto now = std::chrono::steady_clock::now();
    if (now - lastTagRefresh > std::chrono::milliseconds(1000)) {
        cachedTags = g_webSocketService.GetConnectedTags();
        lastTagRefresh = now;
    }

    if (cachedTags.empty()) {
        ImGuiMCP::TextDisabled("%s", g_translations.Get("NoClientsConnected").c_str());
    } else {
        for (const auto& tag : cachedTags) {
            ImGuiMCP::Text("%s", tag.c_str());
            ImGuiMCP::SameLine();

            bool loggingEnabled = g_wsLog.IsEnabled(tag);
            if (ImGuiMCP::Checkbox((g_translations.Get("EnableClientLogging") + "##" + tag + "NetworkClientUtil").c_str(),
                                   &loggingEnabled)) {
                g_wsLog.SetEnabled(tag, loggingEnabled);
            }

            ImGuiMCP::SameLine();

            bool& isOpen = openLogWindows[tag];
            if (ImGuiMCP::Button((g_translations.Get("ClientLoggingView") + "##" + tag + "NetworkClientUtil").c_str())) {
                isOpen = !isOpen;
            }

            ImGuiMCP::SameLine();

            if (ImGuiMCP::Button((g_translations.Get("ClientDisconnect") + "##" + tag + "##NetworkClientUtil").c_str())) {
                g_webSocketService.Disconnect(tag);
                lastTagRefresh = {}; // refresh now!
            }
        }
    }

    if (httpLogging) {
        RenderHttpLogWindow();
    }

    for (auto& [tag, isOpen] : openLogWindows) {
        if (isOpen) {
            RenderWebSocketLogWindow(tag);
        }
    }
}

void UI::RenderHttpLogWindow() {
    auto viewport = ImGuiMCP::GetMainViewport();
    ImGuiMCP::SetNextWindowSize(ImGuiMCP::ImVec2{viewport->Size.x * 0.4f, viewport->Size.y * 0.4f}, ImGuiMCP::ImGuiCond_FirstUseEver);
    ImGuiMCP::Begin((g_translations.Get("HttpLogWindow") + "##NetworkClientUtil").c_str());

    if (ImGuiMCP::Button((g_translations.Get("ClearBtn") + "##NetworkClientUtil").c_str())) {
        g_httpLog.Clear();
    }
    ImGuiMCP::BeginChild("LogScroll##NetworkClientUtil", ImGuiMCP::ImVec2(0, 0), false, ImGuiMCP::ImGuiWindowFlags_HorizontalScrollbar);

    auto lines = g_httpLog.GetSnapshot();
    for (const auto& line : lines) {
        ImGuiMCP::TextUnformatted(line.c_str());
    }

    if (ImGuiMCP::GetScrollY() >= ImGuiMCP::GetScrollMaxY()) {
        ImGuiMCP::SetScrollHereY(1.0f);
    }
    ImGuiMCP::EndChild();
    ImGuiMCP::End();
}

void UI::RenderWebSocketLogWindow(const std::string& tag) {
    bool isOpen = openLogWindows[tag];

    auto viewport = ImGuiMCP::GetMainViewport();
    ImGuiMCP::SetNextWindowSize(ImGuiMCP::ImVec2{viewport->Size.x * 0.4f, viewport->Size.y * 0.4f}, ImGuiMCP::ImGuiCond_FirstUseEver);
    ImGuiMCP::Begin((g_translations.Get("ClientLogWindow") + tag + "##NetworkClientUtil").c_str(), &isOpen);

    if (ImGuiMCP::Button((g_translations.Get("ClearBtn") + "##NetworkClientUtil").c_str())) {
        g_wsLog.Clear(tag);
    }

    ImGuiMCP::Separator();
    ImGuiMCP::BeginChild("LogScroll##NetworkClientUtil", ImGuiMCP::ImVec2(0, 0), false, ImGuiMCP::ImGuiWindowFlags_HorizontalScrollbar);

    auto lines = g_wsLog.GetSnapshot(tag);
    for (const auto& line : lines) {
        ImGuiMCP::TextUnformatted(line.c_str());
    }

    if (ImGuiMCP::GetScrollY() >= ImGuiMCP::GetScrollMaxY()) {
        ImGuiMCP::SetScrollHereY(1.0f);
    }

    ImGuiMCP::EndChild();
    ImGuiMCP::End();
    openLogWindows[tag] = isOpen;
}


void UI::RegisterMenu() {
    if (!SKSEMenuFramework::IsInstalled()) {
        logger::warn("SKSE Menu Framework not installed");
        return;
    }

    SKSEMenuFramework::SetSection(g_translations.Get("ModName"));
    SKSEMenuFramework::AddSectionItem(g_translations.Get("SettingsSection"), UI::Render);
}