#pragma once
#include <cstdio>
#include <cstdlib>
inline int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)
#define RUN(fn) do { std::printf("[ RUN  ] %s\n", #fn); int before = g_failures; fn(); std::printf("[ %s ] %s\n", g_failures == before ? " OK " : "FAIL", #fn); } while (0)
#define TEST_MAIN_END() do { std::printf(g_failures ? "\n%d check(s) FAILED\n" : "\nAll tests passed\n", g_failures); return g_failures ? 1 : 0; } while (0)
