#ifndef HYDRA_SESSION_MANAGER_H
#define HYDRA_SESSION_MANAGER_H

#include "hydra_types.h"
#include "config.h"
#include "front_door.h"
#include "back_door.h"
#include "scrollback.h"
#include <map>
#include <string>
#include <vector>

struct HydraSession {
    HydraSessionId      id;
    uint32_t            accountId;
    std::string         username;

    std::vector<ganl::ConnectionHandle> frontDoors;
    std::vector<BackDoorLink*>          links;
    BackDoorLink*                       activeLink{nullptr};

    ScrollBack          scrollback;

    time_t              created;
    time_t              lastActivity;

    SessionState        state{SessionState::Login};

    // Scroll-back encryption key (derived from password, in memory only)
    std::vector<uint8_t> scrollbackKey;
};

class SessionManager {
public:
    SessionManager(FrontDoor& frontDoor, BackDoor& backDoor,
                   const HydraConfig& config);
    ~SessionManager();

    // Process a line of input from a front-door connection.
    void onFrontDoorInput(ganl::ConnectionHandle handle,
                          const std::string& line);

    // Called when a front-door connection is established.
    void onFrontDoorConnect(ganl::ConnectionHandle handle);

    // Called when a front-door connection is lost.
    void onFrontDoorDisconnect(ganl::ConnectionHandle handle);

    // Called when back-door data arrives.
    void onBackDoorData(BackDoorLink* link, const std::string& data);

    // Called when a back-door connection is established.
    void onBackDoorConnect(BackDoorLink* link);

    // Called when a back-door connection is lost.
    void onBackDoorDisconnect(BackDoorLink* link);

    // Run periodic timers (idle timeouts, reconnect).
    void runTimers();

private:
    void handleLogin(FrontDoorConn* fd, const std::string& line);
    void dispatchSessionCommand(HydraSession* session,
                                ganl::ConnectionHandle fdHandle,
                                const std::string& line);
    void sendToFrontDoor(ganl::ConnectionHandle handle,
                         const std::string& text);

    FrontDoor& frontDoor_;
    BackDoor& backDoor_;
    const HydraConfig& config_;

    std::map<HydraSessionId, HydraSession*> sessions_;
    HydraSessionId nextSessionId_{1};
};

#endif // HYDRA_SESSION_MANAGER_H
