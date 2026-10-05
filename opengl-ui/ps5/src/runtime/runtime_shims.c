// ProsperoTV - Process-level runtime shims for the OpenGL runtime.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Adapted from ps5-opengl native-app/runtime_shims.c: the app log receipt,
// the never-return main policy, and libc entry points the clean-room libc
// shim does not provide but the statically linked Mesa runtime references.

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern int sceKernelUsleep(uint32_t microseconds);

/* Until main has settled where the app's data is (tv_storage.cpp), the log
 * goes into the title's own storage, which is always there:
 * /mnt/sandbox/<TITLE_ID>_000/download0/prosperotv/app.log over FTP while the
 * title runs. */
#define TV_LOG_DIR "/download0/prosperotv"
#define TV_LOG_PATH TV_LOG_DIR "/app.log"

__attribute__((constructor)) static void tv_open_log(void)
{
    mkdir(TV_LOG_DIR, 0755);
    /* Keep the previous launch's log for post-close inspection. */
    rename(TV_LOG_PATH, TV_LOG_DIR "/app.prev.log");
    FILE *stream = freopen(TV_LOG_PATH, "w", stdout);
    /* Start a fresh receipt, then make both streams append-only and unbuffered
     * so the log survives a shell close or a GPU fail-stop. */
    if (stream != NULL)
        stream = freopen(TV_LOG_PATH, "a", stdout);
    if (stream != NULL)
        setvbuf(stream, NULL, _IONBF, 0);
    stream = freopen(TV_LOG_PATH, "a", stderr);
    if (stream != NULL)
        setvbuf(stream, NULL, _IONBF, 0);
}

/* With filesystem access the log is kept with the app's data
 * (/data/prosperotv/logs/app.log), where it can be read whether the title runs
 * or not. Every write to that drive takes tens of milliseconds, so a line is
 * written whole instead of piece by piece. */
void tv_log_move(const char *directory)
{
    static char path[200];
    static char previous[200];
    static char out_line[1024];
    static char err_line[1024];
    char started[200];

    if (directory == NULL || strcmp(directory, TV_LOG_DIR) == 0)
        return;
    snprintf(path, sizeof(path), "%s/app.log", directory);
    snprintf(previous, sizeof(previous), "%s/app.prev.log", directory);
    snprintf(started, sizeof(started), "%s/app.log", TV_LOG_DIR);
    rename(path, previous);
    /* What was written before the move comes along. */
    FILE *before = fopen(started, "rb");
    FILE *moved = fopen(path, "wb");
    if (before != NULL && moved != NULL)
    {
        char chunk[512];
        size_t count;
        fflush(NULL);
        while ((count = fread(chunk, 1, sizeof(chunk), before)) != 0)
            fwrite(chunk, 1, count, moved);
    }
    if (before != NULL)
        fclose(before);
    if (moved != NULL)
        fclose(moved);
    if (freopen(path, "a", stdout) != NULL)
        setvbuf(stdout, out_line, _IOLBF, sizeof(out_line));
    if (freopen(path, "a", stderr) != NULL)
        setvbuf(stderr, err_line, _IOLBF, sizeof(err_line));
}

/* Returning from main or calling exit() crashes a native title; stay alive
 * until the shell closes the title. */
__attribute__((noreturn)) void catchReturnFromMain(int status)
{
    printf("[TV] main returned status=%d\n", status);
    fflush(NULL);
    for (;;)
        sceKernelUsleep(100000);
}

void tv_glapi_tls_context_init(void) __asm__("_ZTH23_mesa_glapi_tls_Context");

void tv_glapi_tls_context_init(void)
{
}

__attribute__((noreturn)) void __assert(const char *function, const char *file, int line,
                                        const char *expression)
{
    fprintf(stderr, "[TV] assertion failed: %s (%s:%d, %s)\n", expression, file, line, function);
    abort();
}

int mkstemps(char *template_name, int suffix_length)
{
    (void)template_name;
    (void)suffix_length;
    errno = ENOSYS;
    return -1;
}

/* The console's launch picture stays up until the menu has presented its
 * first frame. The OpenGL runtime asks to hide it as soon as the display
 * opens, which would leave a black screen while programs build and fonts
 * load. The kit's build routes every such call here (--wrap), and its
 * sys::hide_splash_screen() lets them through after hui_release_splash(). */
extern int __real_sceSystemServiceHideSplashScreen(void);
static int tv_splash_released;

void hui_release_splash(void)
{
    tv_splash_released = 1;
}

int __wrap_sceSystemServiceHideSplashScreen(void)
{
    return tv_splash_released ? __real_sceSystemServiceHideSplashScreen() : 0;
}
