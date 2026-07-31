// Standalone regression for #1891 — Hydra passwords stay out of worlds.txt.
// Build:
//   g++ -std=c++17 -I src -o test_world_cred test_world_cred.cpp
//       src/world.cpp src/credential_store.cpp
#include "world.h"
#include "credential_store.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

static std::string slurp(const std::string& path) {
    std::ifstream f(path);
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

static void expect(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    char dir[] = "/tmp/hydra-world-cred-XXXXXX";
    expect(mkdtemp(dir) != nullptr, "mkdtemp");
    std::string worlds = std::string(dir) + "/worlds.txt";
    std::string creds = std::string(dir) + "/worlds.cred";

    // Seed a legacy plaintext hydra line.
    {
        std::ofstream f(worlds);
        f << "hydra home 127.0.0.1 4201 alice s3cret mygame\n";
        f << "world plain example.com 23\n";
    }
    chmod(worlds.c_str(), 0600);

    WorldDB db;
    expect(db.load(worlds), "load legacy worlds.txt");
    const World* h = db.find("home");
    expect(h && h->use_hydra, "hydra world loaded");
    expect(h->hydra_user == "alice", "user");
    expect(h->hydra_pass == "s3cret", "password migrated into memory");
    expect(h->hydra_game == "mygame", "game");

    expect(db.save(worlds), "save without plaintext password");
    std::string body = slurp(worlds);
    expect(body.find("s3cret") == std::string::npos,
           "password must not appear in worlds.txt after save");
    expect(body.find("hydra home") != std::string::npos, "hydra line present");
    expect(body.find(" alice ") != std::string::npos, "username still listed");
    expect(body.find(" mygame") != std::string::npos, "game still listed");

    // Credential file must exist and be mode 0600 (on Unix).
    struct stat st;
    expect(stat(creds.c_str(), &st) == 0, "worlds.cred created");
    expect((st.st_mode & 077) == 0, "worlds.cred mode 0600");

    // Reload from new format + cred file.
    WorldDB db2;
    expect(db2.load(worlds), "reload after migration");
    const World* h2 = db2.find("home");
    expect(h2 && h2->hydra_pass == "s3cret", "password from credential store");
    expect(h2->hydra_user == "alice", "user from store/file");

    // Insecure worlds.txt refused without override.
    chmod(worlds.c_str(), 0644);
    unsetenv("HYDRA_ALLOW_INSECURE_WORLDS");
    WorldDB db3;
    expect(!db3.load(worlds), "insecure worlds.txt must fail closed");

    setenv("HYDRA_ALLOW_INSECURE_WORLDS", "1", 1);
    WorldDB db4;
    expect(db4.load(worlds), "override allows insecure load");
    unsetenv("HYDRA_ALLOW_INSECURE_WORLDS");
    chmod(worlds.c_str(), 0600);

    // remove drops credential.
    db2.remove("home");
    expect(CredStore::LoadPassword("home").empty(), "remove clears credential");

    std::cout << "test_world_cred: ok\n";

    unlink(worlds.c_str());
    unlink(creds.c_str());
    rmdir(dir);
    return 0;
}
