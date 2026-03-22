#include "back_door.h"
#include "hydra_log.h"

BackDoor::BackDoor(ganl::NetworkEngine& engine)
    : engine_(engine) {
}

BackDoor::~BackDoor() {
}

bool BackDoor::handleEvent(const ganl::IoEvent& event) {
    // TODO: handle ConnectSuccess, ConnectFail, Read, Write, Close
    (void)event;
    return false;
}

BackDoorLink* BackDoor::connectToGame(const GameConfig& game,
                                      HydraSessionId sessionId) {
    // TODO: initiateConnect via GANL
    LOG_INFO("Back door: would connect to %s:%u for session %lu",
        game.host.c_str(), game.port, (unsigned long)sessionId);
    (void)game;
    (void)sessionId;
    return nullptr;
}

void BackDoor::sendToGame(BackDoorLink* link, const std::string& data) {
    // TODO: queue write via GANL
    (void)link;
    (void)data;
}

void BackDoor::closeLink(BackDoorLink* link) {
    // TODO: close connection
    (void)link;
}

BackDoorLink* BackDoor::findLink(ganl::ConnectionHandle handle) {
    // TODO: link map lookup
    (void)handle;
    return nullptr;
}
