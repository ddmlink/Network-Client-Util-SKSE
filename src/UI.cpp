#include "UI.h"
#include "ixwebsocket_wrapper.h"
#include "logger.h";
#include "SimpleIni.h"
#include "SKSEMenuFramework.h"

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
    if (ImGuiMCP::Checkbox("Enable HTTP Client", &httpEnabled)) {
        g_httpClientEnabled.store(httpEnabled, std::memory_order_relaxed);
        SaveHttpClientSettings();
    }

    ImGuiMCP::SameLine();

    httpLogging = g_httpLoggingEnabled.load(std::memory_order_relaxed);
    if (ImGuiMCP::Checkbox("Enable HTTP Logging", &httpLogging)) {
        g_httpLoggingEnabled.store(httpLogging, std::memory_order_relaxed);
    }

    ImGuiMCP::Separator();

    wsEnabled = g_websocketClientEnabled.load(std::memory_order_relaxed);
    if (ImGuiMCP::Checkbox("Enable WebSocket Client", &wsEnabled)) {
        g_websocketClientEnabled.store(wsEnabled, std::memory_order_relaxed);
        SaveHttpClientSettings();
    }

    ImGuiMCP::Text("Connected WebSocket Clients");

    // for now, let's limit our checks to once per second
    auto now = std::chrono::steady_clock::now();
    if (now - lastTagRefresh > std::chrono::milliseconds(1000)) {
        cachedTags = g_webSocketService.GetConnectedTags();
        lastTagRefresh = now;
    }

    if (cachedTags.empty()) {
        ImGuiMCP::TextDisabled("No active connections");
    } else {
        for (const auto& tag : cachedTags) {
            ImGuiMCP::Text("%s", tag.c_str());
            ImGuiMCP::SameLine();

            std::string logButtonId = "View Log##" + tag;
            bool& isOpen = openLogWindows[tag];
            if (ImGuiMCP::Button(logButtonId.c_str())) {
                isOpen = !isOpen;
            }

            ImGuiMCP::SameLine();

            bool loggingEnabled = g_wsLog.IsEnabled(tag);
            std::string checkboxId = "Log##" + tag;
            if (ImGuiMCP::Checkbox(checkboxId.c_str(), &loggingEnabled)) {
                g_wsLog.SetEnabled(tag, loggingEnabled);
            }

            std::string buttonId = "Disconnect##" + tag;
            if (ImGuiMCP::Button(buttonId.c_str())) {
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
    ImGuiMCP::Begin("HTTP Log");

    if (ImGuiMCP::Button("Clear")) {
        g_httpLog.Clear();
    }
    ImGuiMCP::BeginChild("LogScroll", ImGuiMCP::ImVec2(0, 0), false, ImGuiMCP::ImGuiWindowFlags_HorizontalScrollbar);

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
    std::string windowTitle = "WS Log: " + tag;

    auto viewport = ImGuiMCP::GetMainViewport();
    ImGuiMCP::SetNextWindowSize(ImGuiMCP::ImVec2{viewport->Size.x * 0.4f, viewport->Size.y * 0.4f}, ImGuiMCP::ImGuiCond_FirstUseEver);
    ImGuiMCP::Begin(windowTitle.c_str(), &isOpen);

    if (ImGuiMCP::Button("Clear")) {
        g_wsLog.Clear(tag);
    }

    ImGuiMCP::Separator();
    ImGuiMCP::BeginChild("LogScroll", ImGuiMCP::ImVec2(0, 0), false, ImGuiMCP::ImGuiWindowFlags_HorizontalScrollbar);

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
        logger::warn("SKSE Menu Framework not installed — settings menu unavailable");
        return;
    }

    SKSEMenuFramework::SetSection("Network Client Util");
    SKSEMenuFramework::AddSectionItem("Settings", UI::Render);
}