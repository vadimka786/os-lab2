#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "proclaunch.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
/* ======================= Windows ======================= */
#include <windows.h>


static char *build_cmdline(const char *const argv[])
{
    size_t total = 1;                       
    size_t i, k;
    for (i = 0; argv[i] != NULL; i++)
        total += 2 * strlen(argv[i]) + 3;   

    char *cmd = (char *)malloc(total);
    if (cmd == NULL)
        return NULL;

    char *p = cmd;
    for (i = 0; argv[i] != NULL; i++) {
        const char *a = argv[i];
        if (i > 0)
            *p++ = ' ';

        int need_quotes = (*a == '\0') || (strpbrk(a, " \t\n\v\"") != NULL);
        if (!need_quotes) {
            size_t n = strlen(a);
            memcpy(p, a, n);
            p += n;
            continue;
        }

        *p++ = '"';
        size_t bs = 0;                     
        for (; *a != '\0'; a++) {
            if (*a == '\\') {
                bs++;
            } else if (*a == '"') {
                for (k = 0; k < bs * 2 + 1; k++) *p++ = '\\';
                *p++ = '"';
                bs = 0;
            } else {
                for (k = 0; k < bs; k++) *p++ = '\\';
                *p++ = *a;
                bs = 0;
            }
        }
        for (k = 0; k < bs * 2; k++) *p++ = '\\';   
        *p++ = '"';
    }
    *p = '\0';
    return cmd;
}

int pl_start(const char *const argv[], pl_process *proc)
{
    if (argv == NULL || argv[0] == NULL || proc == NULL)
        return -1;

    char *cmd = build_cmdline(argv);
    if (cmd == NULL)
        return -1;

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    ZeroMemory(&pi, sizeof pi);

    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    free(cmd);
    if (!ok)
        return -1;                          

    CloseHandle(pi.hThread);                
    proc->handle = (intptr_t)pi.hProcess;
    proc->pid = (long)pi.dwProcessId;
    return 0;
}

int pl_wait(pl_process *proc, int *exit_code)
{
    if (proc == NULL || exit_code == NULL || proc->handle == 0)
        return -1;

    HANDLE h = (HANDLE)proc->handle;
    if (WaitForSingleObject(h, INFINITE) != WAIT_OBJECT_0)
        return -1;

    DWORD code;
    if (!GetExitCodeProcess(h, &code))
        return -1;

    CloseHandle(h);
    proc->handle = 0;
    *exit_code = (int)code;
    return 0;
}

int pl_poll(pl_process *proc, int *exit_code)
{
    if (proc == NULL || exit_code == NULL || proc->handle == 0)
        return -1;

    HANDLE h = (HANDLE)proc->handle;
    DWORD r = WaitForSingleObject(h, 0);
    if (r == WAIT_TIMEOUT)
        return 0;                           
    if (r != WAIT_OBJECT_0)
        return -1;

    DWORD code;
    if (!GetExitCodeProcess(h, &code))
        return -1;

    CloseHandle(h);
    proc->handle = 0;
    *exit_code = (int)code;
    return 1;
}

#else
/* ======================= POSIX ======================= */
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int pl_start(const char *const argv[], pl_process *proc)
{
    if (argv == NULL || argv[0] == NULL || proc == NULL)
        return -1;

    int fd[2];
    if (pipe(fd) != 0)
        return -1;
    fcntl(fd[0], F_SETFD, FD_CLOEXEC);
    fcntl(fd[1], F_SETFD, FD_CLOEXEC);

    pid_t pid = fork();
    if (pid < 0) {                     
        close(fd[0]);
        close(fd[1]);
        return -1;
    }

    if (pid == 0) {  
        close(fd[0]);
        execvp(argv[0], (char *const *)argv); 
        int err = errno;
        ssize_t w = write(fd[1], &err, sizeof err);
        (void)w;
        _exit(127);    
    }

    /* родитель */
    close(fd[1]);
    int err = 0;
    ssize_t n;
    do {
        n = read(fd[0], &err, sizeof err);
    } while (n < 0 && errno == EINTR);
    close(fd[0]);

    if (n > 0) {      
        int st;
        pid_t r;
        do {
            r = waitpid(pid, &st, 0);
        } while (r < 0 && errno == EINTR);
        return -1;
    }

    proc->pid = (long)pid;
    proc->handle = 0;
    return 0;
}

static int decode_status(int status)
{
    if (WIFEXITED(status))
        return WEXITSTATUS(status);         /* 0..255 */
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status); 
    return -1;
}

int pl_wait(pl_process *proc, int *exit_code)
{
    if (proc == NULL || exit_code == NULL || proc->pid <= 0)
        return -1;

    int status;
    pid_t r;
    do {
        r = waitpid((pid_t)proc->pid, &status, 0);
    } while (r < 0 && errno == EINTR);
    if (r < 0)
        return -1;

    proc->pid = 0;
    *exit_code = decode_status(status);
    return 0;
}

int pl_poll(pl_process *proc, int *exit_code)
{
    if (proc == NULL || exit_code == NULL || proc->pid <= 0)
        return -1;

    int status;
    pid_t r;
    do {
        r = waitpid((pid_t)proc->pid, &status, WNOHANG);
    } while (r < 0 && errno == EINTR);

    if (r < 0)
        return -1;
    if (r == 0)
        return 0; 

    proc->pid = 0;
    *exit_code = decode_status(status);
    return 1;
}

#endif