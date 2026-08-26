#pragma once
#include "SKSEMenuFramework.h"

namespace UI {
    inline bool httpEnabled;
    inline bool wsEnabled;
    inline bool httpLogging;
    inline std::vector<std::string> cachedTags;
    inline std::chrono::steady_clock::time_point lastTagRefresh{};
    inline std::unordered_map<std::string, bool> openLogWindows; // tracks which WS clients have open log windows

    void __stdcall Render();
    void RegisterMenu();
    void RenderHttpLogWindow();
    void RenderWebSocketLogWindow(const std::string& tag);
}