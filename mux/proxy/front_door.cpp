#include "front_door.h"
#include "hydra_log.h"

FrontDoor::FrontDoor(ganl::NetworkEngine& engine)
    : engine_(engine) {
}

FrontDoor::~FrontDoor() {
}

bool FrontDoor::initialize(const std::vector<ListenConfig>& listeners) {
    // TODO: create GANL listeners
    for (const auto& lc : listeners) {
        LOG_INFO("Front door: would listen on %s:%u (%s)",
            lc.host.c_str(), lc.port, lc.tls ? "TLS" : "plain");
    }
    return true;
}

bool FrontDoor::handleEvent(const ganl::IoEvent& event) {
    // TODO: handle Accept, Read, Write, Close for front-door connections
    (void)event;
    return false;
}

void FrontDoor::sendToClient(ganl::ConnectionHandle handle,
                             const std::string& data) {
    // TODO: queue write via GANL
    (void)handle;
    (void)data;
}

void FrontDoor::closeClient(ganl::ConnectionHandle handle) {
    // TODO: close via GANL
    (void)handle;
}

FrontDoorConn* FrontDoor::findConn(ganl::ConnectionHandle handle) {
    // TODO: connection map lookup
    (void)handle;
    return nullptr;
}
