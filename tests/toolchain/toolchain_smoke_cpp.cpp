#include <iostream>

int main() {
#if !defined(__aarch64__) || !defined(__AARCH64EL__)
#error "This smoke test must be built for AArch64 little-endian"
#endif

#if !defined(__cplusplus) || __cplusplus < 201703L
#error "This smoke test requires C++17 or later"
#endif

    std::cout << "Manifold 3 vision detect - toolchain C++ smoke test" << '\n';
    std::cout << "__cplusplus = " << __cplusplus << '\n';

    if (sizeof(void *) != 8) {
        std::cout << "FAIL: unexpected pointer size " << sizeof(void *) << '\n';
        return 1;
    }

    std::cout << "PASS: toolchain C++ smoke test" << '\n';
    return 0;
}
