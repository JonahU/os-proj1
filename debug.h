#pragma once

#ifdef DEBUG
    #include <stdio.h>
    #define printf_debug(fmt, ...) do { fprintf(stderr, fmt, ##__VA_ARGS__); } while (0)
#else
    #define printf_debug(...) do {} while (0)
#endif
