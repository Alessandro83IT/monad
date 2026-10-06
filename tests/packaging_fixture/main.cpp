// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdio>
#include <cstring>

extern "C" int packaging_fixture_core();
int main(int argc, char** argv) {
    if (!packaging_fixture_core() || argc != 2) return 1;
    if (std::strcmp(argv[1], "--version") == 0) {
        std::puts("Athena packaging fixture");
    } else if (std::strcmp(argv[1], "--help") == 0) {
        std::puts("Athena packaging fixture: --version --help");
    } else {
        return 1;
    }
}
