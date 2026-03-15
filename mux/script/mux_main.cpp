/*! \file mux_main.cpp
 * \brief Script-mode driver for TinyMUX.
 *
 * Minimal binary that loads engine.so via COM and runs softcode from
 * stdin or a file.  No networking, no telnet, no SSL, no descriptors.
 * Same engine, different front-end.
 *
 * Usage:
 *   mux -g /path/to/game -c netmux.conf < script.mux
 *   mux -e 'think add(2,3)'
 *   echo 'think sha1(hello)' | mux
 *
 * Game directory resolution (in priority order):
 *   1. -g <dir> command-line flag
 *   2. MUX_HOME environment variable
 *   3. Current working directory
 *
 * engine.so is loaded from <gamedir>/bin/engine.so.
 * The config file path is relative to <gamedir>.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "libmux.h"
#include "modules.h"
#include "timeutil.h"
#include "alloc.h"
#include "svdrand.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Game directory resolution and engine loading.
// ---------------------------------------------------------------------------

static mux_IGameEngine *g_pEngine = nullptr;
static char g_gamedir[4096];

static bool resolve_gamedir(const char *flag_dir)
{
    const char *dir = flag_dir;

    // Priority 1: -g flag.
    // Priority 2: MUX_HOME env var.
    // Priority 3: current directory.
    if (!dir)
    {
        dir = getenv("MUX_HOME");
    }
    if (!dir)
    {
        dir = ".";
    }

    // Resolve to absolute path.
    if (!realpath(dir, g_gamedir))
    {
        fprintf(stderr, "mux: cannot resolve game directory '%s': %s\n",
                dir, strerror(errno));
        return false;
    }

    // Verify engine.so exists.
    char engine_path[4096 + 64];
    snprintf(engine_path, sizeof(engine_path), "%s/bin/engine.so", g_gamedir);
    if (access(engine_path, R_OK) != 0)
    {
        fprintf(stderr, "mux: engine.so not found at '%s'\n", engine_path);
        return false;
    }

    return true;
}

static MUX_RESULT init_com(void)
{
    MUX_RESULT mr = mux_InitModuleLibrary(IsMainProcess);
    if (MUX_FAILED(mr))
    {
        fprintf(stderr, "mux: mux_InitModuleLibrary failed (%d)\n", mr);
        return mr;
    }

    // Build engine.so path relative to game directory.
    char engine_path[4096 + 64];
    snprintf(engine_path, sizeof(engine_path), "%s/bin/engine.so", g_gamedir);

    mr = mux_AddModule(T("engine"),
                        reinterpret_cast<const UTF8 *>(engine_path));
    if (MUX_FAILED(mr))
    {
        fprintf(stderr, "mux: cannot load '%s' (%d)\n", engine_path, mr);
        return mr;
    }

    // Create IGameEngine via COM.
    mr = mux_CreateInstance(CID_GameEngine, nullptr, UseSameProcess,
                            IID_IGameEngine,
                            reinterpret_cast<void **>(&g_pEngine));
    if (MUX_FAILED(mr) || !g_pEngine)
    {
        fprintf(stderr, "mux: cannot create IGameEngine (%d)\n", mr);
        return mr;
    }
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// Script loop: read lines, process as commands.
// ---------------------------------------------------------------------------

static void script_loop(FILE *input)
{
    // TODO: acquire ICommandProcessor from engine.so to call
    // process_command directly.  For Phase 1, this is a placeholder
    // that demonstrates the COM boot sequence works.
    //
    UTF8 line[8192];
    while (fgets(reinterpret_cast<char *>(line), sizeof(line), input))
    {
        // Strip trailing newline.
        size_t len = strlen(reinterpret_cast<const char *>(line));
        if (len > 0 && line[len-1] == '\n') line[--len] = '\0';
        if (len > 0 && line[len-1] == '\r') line[--len] = '\0';

        // Skip comments and blank lines.
        if (line[0] == '\0') continue;
        if (line[0] == '#' && (len < 2 || line[1] < '0' || line[1] > '9'))
            continue;

        // TODO: process_command(executor, line) via COM interface.
        fprintf(stdout, "CMD: %s\n", line);
        fflush(stdout);
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

static void usage(void)
{
    fprintf(stderr,
        "Usage: mux [options]\n"
        "  -g <dir>      Game directory (default: $MUX_HOME or cwd)\n"
        "  -c <config>   Configuration file (default: netmux.conf)\n"
        "  -e <expr>     Evaluate single expression\n"
        "  --readonly    Don't save database on exit\n"
        "  --help        Show this help\n"
        "\n"
        "Reads softcode commands from stdin (or -e) and writes output to stdout.\n"
        "Loads engine.so from <gamedir>/bin/engine.so.\n"
        "\n"
        "Game directory resolution:\n"
        "  1. -g <dir> flag\n"
        "  2. MUX_HOME environment variable\n"
        "  3. Current working directory\n"
    );
}

int main(int argc, char *argv[])
{
    const char *conffile = "netmux.conf";
    const char *gamedir_flag = nullptr;
    const char *eval_expr = nullptr;
    bool readonly = false;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-g") == 0 && i + 1 < argc)
        {
            gamedir_flag = argv[++i];
        }
        else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc)
        {
            conffile = argv[++i];
        }
        else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc)
        {
            eval_expr = argv[++i];
        }
        else if (strcmp(argv[i], "--readonly") == 0)
        {
            readonly = true;
        }
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
        {
            usage();
            return 0;
        }
        else
        {
            fprintf(stderr, "mux: unknown option '%s'\n", argv[i]);
            usage();
            return 1;
        }
    }

    // Resolve game directory.
    if (!resolve_gamedir(gamedir_flag))
    {
        return 1;
    }

    // chdir to game directory so config file paths resolve correctly.
    if (chdir(g_gamedir) != 0)
    {
        fprintf(stderr, "mux: cannot chdir to '%s': %s\n",
                g_gamedir, strerror(errno));
        return 1;
    }

    // Runtime initialization.
    SeedRandomNumberGenerator();
    // Caller-owned pools (libmux types).
    // Engine-owned pools (BOOL, QENTRY, PCACHE) are initialized
    // by engine.so during LoadGame().
    // POOL_DESC intentionally skipped — no network descriptors.
    //
    pool_init(POOL_LBUF, LBUF_SIZE);
    pool_init(POOL_MBUF, MBUF_SIZE);
    pool_init(POOL_SBUF, SBUF_SIZE);
    pool_init(POOL_LBUFREF, sizeof(lbuf_ref));
    pool_init(POOL_REGREF, sizeof(reg_ref));

    // Initialize COM and load engine.so.
    MUX_RESULT mr = init_com();
    if (MUX_FAILED(mr))
    {
        return 2;
    }

    // Load game database.
    mr = g_pEngine->LoadGame(reinterpret_cast<const UTF8 *>(conffile),
                              nullptr, false);
    if (MUX_FAILED(mr))
    {
        fprintf(stderr, "mux: LoadGame failed (%d)\n", mr);
        g_pEngine->Release();
        return 2;
    }

    // Set timing state.
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();
    g_pEngine->SetStartTime(ltaNow);
    g_pEngine->SetRestartTime(ltaNow);
    g_pEngine->SetRestartCount(0);
    g_pEngine->SetCpuCountFrom(ltaNow);

    // Post-load initialization.
    mr = g_pEngine->Startup();
    if (MUX_FAILED(mr))
    {
        fprintf(stderr, "mux: Startup failed (%d)\n", mr);
        g_pEngine->Release();
        return 2;
    }

    fprintf(stderr, "mux: loaded game from %s\n", g_gamedir);

    // Run script.
    if (eval_expr)
    {
        // Single expression mode.
        // TODO: process_command via COM.
        fprintf(stdout, "CMD: %s\n", eval_expr);
        fflush(stdout);
    }
    else
    {
        script_loop(stdin);
    }

    // Drain remaining queued commands.
    ltaNow.GetUTC();
    g_pEngine->RunTasks(ltaNow);

    // Save and shutdown.
    if (!readonly)
    {
        g_pEngine->DumpDatabase();
    }
    g_pEngine->Shutdown();
    g_pEngine->Release();
    return 0;
}
