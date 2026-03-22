#ifndef HYDRA_FRONT_DOOR_H
#define HYDRA_FRONT_DOOR_H

#include "hydra_types.h"
#include "config.h"
#include <network_engine.h>
#include <network_types.h>

struct FrontDoorConn {
    ganl::ConnectionHandle  handle{ganl::InvalidConnectionHandle};
    HydraSessionId          sessionId{InvalidHydraSessionId};

    ganl::ProtocolState     protoState;
    ganl::EncodingType      encoding{ganl::EncodingType::Utf8};
    FrontDoorProto          type{FrontDoorProto::Telnet};

    // Login state machine
    enum { AwaitUsername, AwaitPassword, Authenticated } loginState{AwaitUsername};
    std::string             pendingUsername;
};

class FrontDoor {
public:
    FrontDoor(ganl::NetworkEngine& engine);
    ~FrontDoor();

    // Create listeners from config.
    bool initialize(const std::vector<ListenConfig>& listeners);

    // Handle a GANL event.  Returns true if the event was consumed.
    bool handleEvent(const ganl::IoEvent& event);

    // Send data to a front-door connection.
    void sendToClient(ganl::ConnectionHandle handle, const std::string& data);

    // Close a front-door connection.
    void closeClient(ganl::ConnectionHandle handle);

    // Look up a FrontDoorConn by handle.
    FrontDoorConn* findConn(ganl::ConnectionHandle handle);

private:
    ganl::NetworkEngine& engine_;
    // TODO: connection tracking maps
};

#endif // HYDRA_FRONT_DOOR_H
