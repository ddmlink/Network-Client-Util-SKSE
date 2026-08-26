#include "includes/response_event.h"

RE::BSFixedString networkResponseEventName = "OnNetworkResponse";

struct NetworkResponseEvent;
bool EraseEvent(NetworkResponseEvent* event);

struct NetworkResponseEvent
{
    bool active;
    std::string name;
    std::string response;
    std::vector<RE::VMHandle> registeredHandles;

    NetworkResponseEvent(RE::BSFixedString sName, RE::BSFixedString sResponse) { 
        name = sName;
        response = sResponse;
    }

    private:
    void HandleEvent() {
        if (registeredHandles.size() == 0) {
            EraseEvent(this);
            return;
        }

        if (!RE::SkyrimVM::GetSingleton()) {
            logger::error("Could not locate RE::SkyrimVM::GetSingleton()");
            return;
        }

        auto* args = RE::MakeFunctionArguments((name, response));
        for (int i = 0; i < registeredHandles.size(); i++) {
            // RE::BSScript::IFunctionArguments* args
            RE::SkyrimVM::GetSingleton()->SendAndRelayEvent(registeredHandles[i], networkResponseEventName, args);
        }

        delete args;
    }
};