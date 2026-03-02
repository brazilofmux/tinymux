#ifndef GANL_SLAVE_SPAWN_POSIX_H
#define GANL_SLAVE_SPAWN_POSIX_H

#ifndef _WIN32

#include <network_engine.h>

namespace ganl {

ConnectionHandle spawnSlavePosix(NetworkEngine& engine, const SlaveSpawnOptions& options, ErrorCode& error);

} // namespace ganl

#endif // _WIN32

#endif // GANL_SLAVE_SPAWN_POSIX_H
