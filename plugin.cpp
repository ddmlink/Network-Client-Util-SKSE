#include "logger.h";
#include "ixwebsocket_wrapper.h"
#include <thread>;
#include <chrono>;
#include <atomic>;
#include "SimpleIni.h"
#include "UI.h"

inline std::atomic<bool> g_frameworkActive{true};
inline std::thread g_tickThread;

void LoadClientSettings() {
    CSimpleIniA ini;
    ini.SetUnicode();

    SI_Error rc = ini.LoadFile(iniPath);
    if (rc < 0) {
        logger::warn("Could not load NetworkClientUtil.ini. HTTP/WebSocket clients enabled by default.");
        return;
    }

    bool httpEnabled = ini.GetBoolValue("HttpClient", "bEnabled", true);
    g_httpClientEnabled.store(httpEnabled, std::memory_order_relaxed);
    logger::info("HTTP client enabled: {}", httpEnabled);

    bool wsEnabled = ini.GetBoolValue("WebSocketClient", "bEnabled", true);
    g_websocketClientEnabled.store(wsEnabled, std::memory_order_relaxed);
    logger::info("WebSocket client enabled: {}", wsEnabled);
}

int GetPluginVersion(RE::StaticFunctionTag*) { return 1; }

std::string UrlEncodeString(RE::StaticFunctionTag*, std::string url) { return UrlEncode(url); }

// HTTP Client Functions
void HttpGet(RE::StaticFunctionTag*, std::string url, std::string tag, std::vector<std::string> headers) { g_httpService.GetAsync(url, tag, headers); }
void HttpGetWithParams(RE::StaticFunctionTag*, std::string url, std::vector<std::string> fields, std::string tag,
    std::vector<std::string> headers) {
    g_httpService.GetWithParams(url, fields, tag, headers);
}

void HttpPost(RE::StaticFunctionTag*, std::string url, std::string jsonBody, std::string tag,
              std::vector<std::string> headers) {
    g_httpService.PostAsync(url, jsonBody, tag, headers);
}
void HttpPostWithParams(RE::StaticFunctionTag*, std::string url, std::vector<std::string> fields, std::string tag,
    std::vector<std::string> headers) {
    g_httpService.PostParamsAsync(url, fields, tag, headers);
}

// WebSocket Client Functions
void ConnectWebSocket(RE::StaticFunctionTag*, std::string url, std::string tag, std::vector<std::string> filters, std::vector<std::string> headers) { g_webSocketService.Connect(url, tag, filters, headers); }
void DisconnectWebSocket(RE::StaticFunctionTag*, std::string tag) { g_webSocketService.Disconnect(tag); }
void SendWebSocketMessage(RE::StaticFunctionTag*, std::string tag, std::string message) {
    g_webSocketService.Send(tag, message);
}
bool IsWebSocketConnected(RE::StaticFunctionTag*, std::string tag) { return g_webSocketService.IsConnected(tag); }
void UpdateWebSocketFilters(RE::StaticFunctionTag*, std::string tag, std::vector<std::string> filters) {
    g_webSocketService.UpdateFilters(tag, filters);
}

void StartTicking();

void OnMessage(SKSE::MessagingInterface::Message* message) { 
    switch (message->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            LoadClientSettings();
            logger::info("Initializing IXWebSocket.");
            g_httpService.Init();
            logger::info("Start checking response queue.");
            StartTicking();
            UI::RegisterMenu();
            logger::info("Done loading.");
            break;
        case SKSE::MessagingInterface::kPostLoad:
            break;
        case SKSE::MessagingInterface::kPreLoadGame:
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
            break;
        case SKSE::MessagingInterface::kNewGame:
            break;
        
    }
}

void Tick() {
    logger::info("TickLoop entered, g_frameworkActive = {}", g_frameworkActive.load());
    while (g_frameworkActive.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));

        SKSE::GetTaskInterface()->AddTask([]() {
            //logger::info("Tick fired");
            g_httpResults.DrainQueue(10);
        });
    }

    logger::info("Tick thread exiting");
}

void StartTicking() { 
    g_frameworkActive.store(true, std::memory_order_relaxed);
    g_tickThread = std::thread(Tick);
}

bool PapyrusFunctions(RE::BSScript::IVirtualMachine* vm) { 
    vm->RegisterFunction("Get", "NCU_Http", HttpGet);
    vm->RegisterFunction("GetWithParams", "NCU_Http", HttpGetWithParams);
    vm->RegisterFunction("Post", "NCU_Http", HttpPost);
    vm->RegisterFunction("PostWithParams", "NCU_Http", HttpPostWithParams);
    vm->RegisterFunction("Connect", "NCU_Ws", ConnectWebSocket);
    vm->RegisterFunction("Disconnect", "NCU_Ws", DisconnectWebSocket);
    vm->RegisterFunction("SendMessage", "NCU_Ws", SendWebSocketMessage);
    vm->RegisterFunction("IsConnected", "NCU_Ws", IsWebSocketConnected);
    vm->RegisterFunction("UrlEncodeString", "NCU_Http", UrlEncodeString);
    vm->RegisterFunction("GetPluginVersion", "NCU_Http", GetPluginVersion);
    vm->RegisterFunction("UpdateFilters", "NCU_Ws", UpdateWebSocketFilters);

    return true;
}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {
    SKSE::Init(skse);
    SetupLog();

    //////////////////////////
    // Stuff goes down here //
    //////////////////////////

    // It's best to register listeners for events here.
    // Don't do anything else here unless needed.
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);

    SKSE::GetPapyrusInterface()->Register(PapyrusFunctions);

    logger::info("Plugin done loading.");

    return true;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        ix::uninitNetSystem(); // required to free up network resources
    }

    return TRUE;
}