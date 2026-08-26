#pragma once
#include "SKSE/SKSE.h"

struct NetworkResponse
{
    extern std::string message;
    bool BindPapyrusFunctions(RE::BSScript::IVirtualMachine* vm);
};

inline SKSE::RegistrationSet<NetworkResponse> g_networkResponseDispatcher;