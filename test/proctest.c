#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "proclaunch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#include <time.h>
static void sleep_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}
#endif

#define TRICKY_ARG "hello \"big\" world\\"

static int failed = 0;

static void report(const char *name, int ok)
{
    printf("[%s] %s\n", ok ? " OK " : "FAIL", name);
    if (!ok)
        failed++;
}

static void test_background(const char *self)
{
    const char *argv[] = { self, "--child", "42", "1000", NULL };
    pl_process p;
    int code = -1, iters = 0, r;

    if (pl_start(argv, &p) != 0) {
        report("1. фоновый запуск и код 42", 0);
        return;
    }
    while ((r = pl_poll(&p, &code)) == 0) {
        iters++;
        sleep_ms(100);
    }
    printf("     итераций в родителе: %d, код: %d\n", iters, code);
    report("1. фоновый запуск и код 42", r == 1 && code == 42 && iters > 3);
}

static void test_wait(const char *self)
{
    const char *argv[] = { self, "--child", "7", "300", NULL };
    pl_process p;
    int code = -1;

    int ok = pl_start(argv, &p) == 0 && pl_wait(&p, &code) == 0 && code == 7;
    printf("     код: %d\n", code);
    report("2. pl_wait возвращает код 7", ok);
}


static void test_args(const char *self)
{
    const char *argv[] = { self, "--arg", TRICKY_ARG, NULL };
    pl_process p;
    int code = -1;

    int ok = pl_start(argv, &p) == 0 && pl_wait(&p, &code) == 0 && code == 0;
    report("3. аргумент с пробелами и кавычками", ok);
}


static void test_no_program(void)
{
    const char *argv[] = { "no_such_program_12345", NULL };
    pl_process p;

    report("4. несуществующая программа -> ошибка", pl_start(argv, &p) == -1);
}

/* Тест 5: код 300 (Linux: 8 бит -> 44, Windows: 300) */
static void test_big_code(const char *self)
{
    const char *argv[] = { self, "--child", "300", "0", NULL };
    pl_process p;
    int code = -1;

#ifdef _WIN32
    const int expected = 300;
#else
    const int expected = 300 % 256;     /* 44 */
#endif

    int ok = pl_start(argv, &p) == 0 && pl_wait(&p, &code) == 0 && code == expected;
    printf("     код: %d (ожидали %d)\n", code, expected);
    report("5. код 300", ok);
}

int main(int argc, char *argv[])
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    if (argc >= 4 && strcmp(argv[1], "--child") == 0) {
        sleep_ms(atoi(argv[3]));
        return atoi(argv[2]);
    }
    /* Режим проверки аргумента: 0, если аргумент пришёл без искажений */
    if (argc >= 3 && strcmp(argv[1], "--arg") == 0)
        return strcmp(argv[2], TRICKY_ARG) == 0 ? 0 : 1;

    const char *self = argv[0];

    test_background(self);
    test_wait(self);
    test_args(self);
    test_no_program();
    test_big_code(self);

    printf(failed ? "\nПровалено тестов: %d\n" : "\nВсе тесты пройдены\n", failed);
    return failed ? 1 : 0;
}