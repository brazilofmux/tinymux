#ifndef HYDRA_BACK_DOOR_H
#define HYDRA_BACK_DOOR_H

#include "hydra_types.h"
#include "config.h"
#include <network_engine.h>
#include <network_types.h>

struct BackDoorLink {
    ganl::ConnectionHandle  handle{ganl::InvalidConnectionHandle};
    HydraSessionId          sessionId{InvalidHydraSessionId};

    std::string             gameName;
    std::string             character;

    ganl::ProtocolState     protoState;
    ganl::EncodingType      encoding{ganl::EncodingType::Utf8};

    LinkState               state{LinkState::Connecting};

    int                     retryCount{0};
    time_t                  nextRetry{0};
};

class BackDoor {
public:
    BackDoor(ganl::NetworkEngine& engine);
    ~BackDoor();

    // Handle a GANL event.  Returns true if consumed.
    bool handleEvent(const ganl::IoEvent& event);

    // Initiate a connection to a game.
    BackDoorLink* connectToGame(const GameConfig& game,
                                HydraSessionId sessionId);

    // Send data to a game link.
    void sendToGame(BackDoorLink* link, const std::string& data);

    // Close a back-door link.
    void closeLink(BackDoorLink* link);

    // Look up a BackDoorLink by handle.
    BackDoorLink* findLink(ganl::ConnectionHandle handle);

private:
    ganl::NetworkEngine& engine_;
    // TODO: link tracking
};

#endif // HYDRA_BACK_DOOR_H
