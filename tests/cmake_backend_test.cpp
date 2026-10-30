// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "temporary_directory.hpp"

#include "../src/build/cmake_backend.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main()
{
    athena::test::TemporaryDirectory fixture("athena-cmake-test");
    const auto build_directory = fixture.path() / "build";
    const auto staging_directory = fixture.path() / "staging";
    const auto marker = fixture.path() / "shell-injection-marker";

    std::cout << "Testing CMake backend...\n";

    /*
     * Create a minimal CMake project used exclusively by this test.
     */
    const auto hostile_source_directory =
        fixture.path() / ("source $(touch " + marker.string() + ")");
    std::filesystem::create_directories(hostile_source_directory);

    {
        std::ofstream cmake_lists(
            hostile_source_directory / "CMakeLists.txt"
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
            hostile_source_directory / "main.cpp"
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
        hostile_source_directory,
        build_directory
    );

    std::cout << "Build completed.\n";

    backend.install(
        hostile_source_directory,
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
    assert(!std::filesystem::exists(marker));

    std::cout
        << "CMake backend tests passed.\n";

    /*
     * Clean up all temporary files created by the test.
     */
    std::filesystem::remove_all(hostile_source_directory);
    std::filesystem::remove_all(build_directory);
    std::filesystem::remove_all(staging_directory);

    return 0;
}
