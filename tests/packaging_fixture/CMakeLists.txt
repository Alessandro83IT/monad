cmake_minimum_required(VERSION 3.20)
project(athena_packaging_fixture LANGUAGES CXX)
# Exercise production installation rules using inexpensive linked ELF targets.
# The main packaging_install_test independently exercises the real Athena binary.
include("${ATHENA_SOURCE_ROOT}/cmake/AthenaPackaging.cmake")
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(LibArchive REQUIRED)
enable_testing()
add_library(athena_core SHARED core.cpp)
target_link_libraries(athena_core PRIVATE OpenSSL::Crypto LibArchive::LibArchive)
if(DEFINED ATHENA_FIXTURE_EXTRA_RPATH)
    set_property(TARGET athena_core APPEND PROPERTY INSTALL_RPATH
        "${ATHENA_FIXTURE_EXTRA_RPATH}" "${ATHENA_FIXTURE_EXTRA_RPATH}")
endif()
add_executable(athena main.cpp)
target_link_libraries(athena PRIVATE athena_core)
add_test(NAME packaging_fixture_build_execution COMMAND athena --version)
athena_install_targets("${ATHENA_SOURCE_ROOT}/include")
athena_add_packaging_smoke_test("${ATHENA_SOURCE_ROOT}/include")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/effective-dirs.txt"
    "${CMAKE_INSTALL_BINDIR}|${CMAKE_INSTALL_LIBDIR}|${CMAKE_INSTALL_INCLUDEDIR}")
get_target_property(core_rpath athena_core INSTALL_RPATH)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/core-rpath.txt" "${core_rpath}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/core-file-$<CONFIG>.txt"
    CONTENT "$<TARGET_FILE:athena_core>")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/discovered-dependencies.txt"
    "${OPENSSL_CRYPTO_LIBRARY}|${LibArchive_LIBRARY}")
