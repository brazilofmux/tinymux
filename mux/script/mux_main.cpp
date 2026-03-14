/*! \file mux_main.cpp
 * \brief Script-mode driver for TinyMUX.
 *
 * Minimal binary that loads engine.so via COM and runs softcode from
 * stdin or a file.  No networking, no telnet, no SSL, no descriptors.
 * Same engine, different front-end.
 *
 * Usage:
 *   mux -c config.conf < script.mux
 *   mux -c config.conf -e 'think add(2,3)'
 *   echo 'think sha1(hello)' | mux -c config.conf
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
// COM initialization: load engine.so, acquire IGameEngine.
// ---------------------------------------------------------------------------

static mux_IGameEngine *g_pEngine = nullptr;

static MUX_RESULT init_com(void)
{
    MUX_RESULT mr = mux_InitModuleLibrary(IsMainProcess);
    if (MUX_FAILED(mr))
    {
        fprintf(stderr, "mux: mux_InitModuleLibrary failed (%d)\n", mr);
        return mr;
    }

    // Load engine.so.
    mr = mux_AddModule(T("engine"), T("./bin/engine.so"));
    if (MUX_FAILED(mr))
    {
        fprintf(stderr, "mux: cannot load engine.so (%d)\n", mr);
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
// Script loop: read lines, process as commands, drain queue.
// ---------------------------------------------------------------------------

// Forward declaration — implemented in engine.so, resolved at link time
// via libmux.so's module system.  We call it through the COM interface
// indirectly by having the engine process commands via RunTasks.
//
// For now, the script loop feeds lines to the engine via a simple
// mechanism: write the line to a well-known attribute, then trigger
// processing.  But the real approach is to call process_command
// directly — which requires the engine to expose it via COM or a
// simpler function export.
//
// TEMPORARY: use IGameEngine::RunTasks as the only interface.
// A proper ICommandProcessor COM interface is needed for Phase 2.

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
        "  -c <config>   Configuration file (required)\n"
        "  -e <expr>     Evaluate single expression\n"
        "  --readonly    Don't save database on exit\n"
        "  --help        Show this help\n"
        "\n"
        "Reads softcode commands from stdin (or -e) and writes output to stdout.\n"
        "Loads engine.so via COM — no networking, no telnet, no descriptors.\n"
    );
}

int main(int argc, char *argv[])
{
    const UTF8 *conffile = nullptr;
    const char *eval_expr = nullptr;
    bool readonly = false;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc)
        {
            conffile = reinterpret_cast<const UTF8 *>(argv[++i]);
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

    if (!conffile)
    {
        fprintf(stderr, "mux: -c <config> is required\n");
        usage();
        return 1;
    }

    // Runtime initialization — same pools as driver.cpp.
    // Struct sizes are approximated since engine types aren't available
    // here.  Over-sizing wastes memory but is safe.
    SeedRandomNumberGenerator();
    pool_init(POOL_LBUF, LBUF_SIZE);
    pool_init(POOL_MBUF, MBUF_SIZE);
    pool_init(POOL_SBUF, SBUF_SIZE);
    pool_init(POOL_BOOL, 256);     // struct boolexp (~100 bytes)
    pool_init(POOL_QENTRY, 512);   // BQUE (~200 bytes)
    pool_init(POOL_LBUFREF, 64);   // lbuf_ref (~32 bytes)
    pool_init(POOL_REGREF, 64);    // reg_ref (~32 bytes)
    // POOL_DESC (4) intentionally skipped — no network descriptors.

    // Initialize COM and load engine.so.
    MUX_RESULT mr = init_com();
    if (MUX_FAILED(mr))
    {
        return 2;
    }

    // Load game database.
    mr = g_pEngine->LoadGame(conffile, nullptr, false);
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

    // Run script.
    if (eval_expr)
    {
        // Single expression mode.
        fprintf(stdout, "CMD: %s\n", eval_expr);
        fflush(stdout);
        // TODO: process_command via COM.
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
