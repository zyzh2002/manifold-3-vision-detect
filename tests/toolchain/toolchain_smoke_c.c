#include <stdio.h>

int main(void) {
#if !defined(__aarch64__) || !defined(__AARCH64EL__)
#error "This smoke test must be built for AArch64 little-endian"
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ < 199901L
#error "This smoke test requires C99 or later"
#endif

    printf("Manifold 3 vision detect - toolchain C smoke test\n");
    printf("__aarch64__ defined: yes\n");
    printf("sizeof(void*) = %zu  (expected 8)\n", sizeof(void *));
    printf("sizeof(long)  = %zu  (expected 8)\n", sizeof(long));
    printf("sizeof(int)   = %zu  (expected 4)\n", sizeof(int));

    if (sizeof(void *) != 8 || sizeof(long) != 8 || sizeof(int) != 4) {
        printf("FAIL: unexpected type sizes\n");
        return 1;
    }

    printf("PASS: toolchain C smoke test\n");
    return 0;
}
