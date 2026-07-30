#include <stdio.h>

int main(void) {
#if !defined(__aarch64__) || !defined(__AARCH64EL__)
#error "This smoke test must be built for AArch64 little-endian"
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ < 199901L
#error "This smoke test requires C99 or later"
#endif

    puts("Manifold 3 vision detect - toolchain C smoke test");
    puts("__aarch64__ defined: yes");
    printf("sizeof(void *) = %zu  (expected 8)\n", sizeof(void *));
    printf("sizeof(long)    = %zu  (expected 8)\n", sizeof(long));
    printf("sizeof(int)     = %zu  (expected 4)\n", sizeof(int));

    if (sizeof(void *) != 8 || sizeof(long) != 8 || sizeof(int) != 4) {
        puts("FAIL: unexpected type sizes");
        return 1;
    }

    puts("PASS: toolchain C smoke test");
    return 0;
}
