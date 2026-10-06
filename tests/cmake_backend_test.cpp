// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/build/cmake_backend.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main()
{
    const std::filesystem::path source_directory =
        "/tmp/athena-cmake-test";

    const std::filesystem::path build_directory =
        "/tmp/athena-cmake-test-build";

    const std::filesystem::path staging_directory =
        "/tmp/athena-cmake-test-staging";

    std::cout << "Testing CMake backend...\n";

    /*
     * Create a minimal CMake project used exclusively by this test.
     */
    std::filesystem::remove_all(source_directory);
    std::filesystem::create_directories(source_directory);

    {
        std::ofstream cmake_lists(
            source_directory / "CMakeLists.txt"
        );

        cmake_lists
            << "cmake_minimum_required(VERSION 3.20)\n"
            << "project(athena_cmake_test LANGUAGES CXX)\n"
            << "set(CMAKE_CXX_STANDARD 20)\n"
            << "add_executable(athena-cmake-test main.cpp)\n"
            << "install(TARGETS athena-cmake-test "
               "RUNTIME DESTINATION bin)\n";
    }

    {
        std::ofstream source_file(
            source_directory / "main.cpp"
        );

        source_file
            << "#include <iostream>\n"
            << "int main() {\n"
            << "    std::cout << \"Athena CMake test\\n\";\n"
            << "    return 0;\n"
            << "}\n";
    }

    athena::build::CMakeBackend backend;

    backend.build(
        source_directory,
        build_directory
    );

    std::cout << "Build completed.\n";

    backend.install(
        source_directory,
        build_directory,
        staging_directory
    );

    std::cout << "Installation into staging completed.\n";

    /*
     * Verify that the executable was installed into the staging tree.
     */
    const auto installed_binary =
        staging_directory /
        "usr" /
        "bin" /
        "athena-cmake-test";

    assert(std::filesystem::exists(installed_binary));

    std::cout
        << "CMake backend tests passed.\n";

    /*
     * Clean up all temporary files created by the test.
     */
    std::filesystem::remove_all(source_directory);
    std::filesystem::remove_all(build_directory);
    std::filesystem::remove_all(staging_directory);

    return 0;
}
