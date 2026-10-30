#pragma once

#include "events/event.h"

class ClientInstance;

struct ClientInstanceUpdateEvent : public Event {
    ClientInstance* clientInstance = nullptr;
    bool isInitFinished = false;
    bool allowGameUpdate = false;

    ClientInstanceUpdateEvent(ClientInstance* instance, bool initFinished, bool allowUpdate = false)
        : clientInstance(instance), isInitFinished(initFinished), allowGameUpdate(allowUpdate) {
    }
};
