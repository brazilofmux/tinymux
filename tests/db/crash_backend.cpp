/*! \file crash_backend.cpp
 * \brief Stage 5b: Crash recovery test for IStorageBackend.
 *
 * Forks a child that writes to the backend, then kills it with SIGKILL.
 * The parent re-opens the backend and verifies that synced data survived
 * and no corruption occurred.
 *
 * Usage:
 *   ./crash_backend sqlite [objects=1000] [attrs=5] [inflight=500]
 *   ./crash_backend mdbx   [objects=1000] [attrs=5] [inflight=500]
 */

#include "storage_backend.h"
#include "sqlite_backend.h"
#include "mdbx_backend.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int parse_param(int argc, char *argv[], const char *name, int def)
{
    size_t nlen = strlen(name);
    for (int i = 2; i < argc; i++)
    {
        if (strncmp(argv[i], name, nlen) == 0 && argv[i][nlen] == '=')
        {
            return atoi(argv[i] + nlen + 1);
        }
    }
    return def;
}

// Known value for a given (object, attr) — deterministic so verifier
// can check without the journal.
//
static void make_value(int obj, int attr, UTF8 *buf, size_t len)
{
    memset(buf, 0, len);
    snprintf(reinterpret_cast<char *>(buf), len,
             "val_obj%d_attr%d_padding", obj, attr);
    // Pad to full length for consistency.
    size_t slen = strlen(reinterpret_cast<char *>(buf));
    if (slen < len - 1)
    {
        memset(buf + slen, 'P', len - 1 - slen);
        buf[len - 1] = '\0';
    }
}

static std::unique_ptr<IStorageBackend> open_backend(
    const char *name, const char *path)
{
    std::unique_ptr<IStorageBackend> be;
    if (strcmp(name, "sqlite") == 0)
    {
        be = std::make_unique<CSQLiteBackend>();
    }
    else
    {
        be = std::make_unique<CMdbxBackend>();
    }
    if (!be->Open(path))
    {
        fprintf(stderr, "Failed to open %s backend at %s\n", name, path);
        return nullptr;
    }
    return be;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: crash_backend <sqlite|mdbx>"
                        " [objects=N] [attrs=N] [inflight=N]\n");
        return 1;
    }

    const char *backend_name = argv[1];
    int n_objects  = parse_param(argc, argv, "objects",  1000);
    int n_attrs    = parse_param(argc, argv, "attrs",    5);
    int n_inflight = parse_param(argc, argv, "inflight", 500);

    if (strcmp(backend_name, "sqlite") != 0 && strcmp(backend_name, "mdbx") != 0)
    {
        fprintf(stderr, "Unknown backend: %s (use sqlite or mdbx)\n",
                backend_name);
        return 1;
    }

    bool is_mdbx = (strcmp(backend_name, "mdbx") == 0);

    // Persistent path that survives the fork+kill.
    //
    char dbpath[256];
    char journal_path[256];
    pid_t mypid = getpid();
    snprintf(dbpath, sizeof(dbpath),
             is_mdbx ? "/tmp/crash_mdbx_%d" : "/tmp/crash_sqlite_%d.db",
             mypid);
    snprintf(journal_path, sizeof(journal_path),
             "/tmp/crash_journal_%d.txt", mypid);

    int synced_count = n_objects * n_attrs;

    printf("=== Crash Recovery Test: %s backend ===\n", backend_name);
    printf("  objects=%d  attrs=%d  synced=%d  inflight=%d\n\n",
           n_objects, n_attrs, synced_count, n_inflight);

    // ---------------------------------------------------------------
    // Phase 1: Writer (child process)
    // ---------------------------------------------------------------

    pid_t child = fork();
    if (child < 0)
    {
        perror("fork");
        return 1;
    }

    if (child == 0)
    {
        // --- Child process ---

        auto be = open_backend(backend_name, dbpath);
        if (!be) _exit(1);

        UTF8 value[256];

        // Step 1: Populate and sync.
        //
        for (int obj = 0; obj < n_objects; obj++)
        {
            for (int attr = 1; attr <= n_attrs; attr++)
            {
                make_value(obj, attr, value, sizeof(value));
                be->Put(obj, attr, value, sizeof(value), 1, 0);
            }
        }
        be->Sync();

        // Step 2: Write in-flight entries (no sync) and journal them.
        //
        FILE *jf = fopen(journal_path, "w");
        if (!jf)
        {
            perror("fopen journal");
            _exit(1);
        }

        // Use object range above the synced population.
        //
        int inflight_base = n_objects;
        for (int i = 0; i < n_inflight; i++)
        {
            int obj  = inflight_base + (i / n_attrs);
            int attr = (i % n_attrs) + 1;
            make_value(obj, attr, value, sizeof(value));
            be->Put(obj, attr, value, sizeof(value), 1, 0);
            fprintf(jf, "%d %d\n", obj, attr);
            fflush(jf);
        }
        fclose(jf);

        // Signal parent we're done writing (but not synced).
        // Then spin until killed.
        //
        kill(getppid(), SIGUSR1);
        for (;;) pause();

        _exit(0);  // Not reached.
    }

    // --- Parent process ---

    // Wait for child to signal it has finished writing.
    //
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    int sig;
    sigwait(&mask, &sig);

    // Small delay to ensure last fflush lands on disk.
    //
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Kill the child with SIGKILL — worst case, no cleanup.
    //
    printf("Phase 1: Child %d wrote %d synced + %d inflight entries.\n",
           child, synced_count, n_inflight);
    printf("Phase 1: Sending SIGKILL to child %d...\n", child);
    kill(child, SIGKILL);

    int status;
    waitpid(child, &status, 0);
    printf("Phase 1: Child terminated (signal %d).\n\n",
           WIFSIGNALED(status) ? WTERMSIG(status) : 0);

    // ---------------------------------------------------------------
    // Phase 2: Verifier (parent process)
    // ---------------------------------------------------------------

    printf("Phase 2: Re-opening %s backend for verification...\n",
           backend_name);

    auto be = open_backend(backend_name, dbpath);
    if (!be)
    {
        printf("CRASH_RESULT:     %-8s FAILED — could not re-open backend\n",
               backend_name);
        return 1;
    }

    UTF8 readbuf[8192];
    UTF8 expected[256];
    size_t rlen;

    // Step 1: Verify synced entries.
    //
    int synced_verified = 0;
    int synced_missing  = 0;
    int synced_corrupt  = 0;

    for (int obj = 0; obj < n_objects; obj++)
    {
        for (int attr = 1; attr <= n_attrs; attr++)
        {
            make_value(obj, attr, expected, sizeof(expected));
            if (be->Get(obj, attr, readbuf, sizeof(readbuf), &rlen,
                        nullptr, nullptr))
            {
                if (rlen != sizeof(expected) ||
                    memcmp(readbuf, expected, sizeof(expected)) != 0)
                {
                    synced_corrupt++;
                }
                else
                {
                    synced_verified++;
                }
            }
            else
            {
                synced_missing++;
            }
        }
    }

    printf("CRASH_SYNCED:     %-8s %5d verified  %5d expected  %d missing  %d corrupt\n",
           backend_name, synced_verified, synced_count,
           synced_missing, synced_corrupt);

    // Step 2: Verify in-flight entries from journal.
    //
    int inflight_attempted = 0;
    int inflight_survived  = 0;
    int inflight_corrupt   = 0;

    FILE *jf = fopen(journal_path, "r");
    if (jf)
    {
        int obj, attr;
        while (fscanf(jf, "%d %d", &obj, &attr) == 2)
        {
            inflight_attempted++;
            make_value(obj, attr, expected, sizeof(expected));
            if (be->Get(obj, attr, readbuf, sizeof(readbuf), &rlen,
                        nullptr, nullptr))
            {
                if (rlen != sizeof(expected) ||
                    memcmp(readbuf, expected, sizeof(expected)) != 0)
                {
                    inflight_corrupt++;
                }
                else
                {
                    inflight_survived++;
                }
            }
        }
        fclose(jf);
    }

    printf("CRASH_INFLIGHT:   %-8s %5d survived  %5d attempted  %d corrupt\n",
           backend_name, inflight_survived, inflight_attempted,
           inflight_corrupt);

    int total_corrupt = synced_corrupt + inflight_corrupt;
    printf("CRASH_CORRUPT:    %-8s %5d total corrupt entries\n",
           backend_name, total_corrupt);

    // ---------------------------------------------------------------
    // Cleanup
    // ---------------------------------------------------------------

    be->Close();
    unlink(journal_path);

    if (is_mdbx)
    {
        char buf[300];
        snprintf(buf, sizeof(buf), "rm -rf %s", dbpath);
        (void)system(buf);
    }
    else
    {
        // SQLite may have -wal and -shm files.
        char buf[300];
        unlink(dbpath);
        snprintf(buf, sizeof(buf), "%s-wal", dbpath);
        unlink(buf);
        snprintf(buf, sizeof(buf), "%s-shm", dbpath);
        unlink(buf);
    }

    // ---------------------------------------------------------------
    // Result
    // ---------------------------------------------------------------

    bool pass = (synced_missing == 0 && total_corrupt == 0);
    printf("\n=== Crash Recovery: %s %s ===\n",
           backend_name, pass ? "PASSED" : "FAILED");
    return pass ? 0 : 1;
}
