#ifndef RESTART_H
#define RESTART_H

#include <string>
#include <vector>

struct App;

// Returns the path to the restart state file (~/.titanfugue/restart.dat).
std::string restart_file_path();

// Serialize all live state to a JSON restart file.
bool serialize_restart(App& app, const std::string& path,
                       const std::vector<std::string>& original_argv);

// Restore state from a restart file. Adopts live FDs, deletes the file.
bool restore_restart(App& app, const std::string& path);

// Serialize state, shut down the terminal, and execv the same binary.
// On execv failure, re-initializes the terminal and prints an error.
void perform_restart(App& app, const std::vector<std::string>& original_argv);

#endif // RESTART_H
