cmake_minimum_required(VERSION 3.20)

project(athena
    VERSION 0.1.0
    DESCRIPTION "Athena Package Manager"
    LANGUAGES CXX
)

include(cmake/AthenaPackaging.cmake)

find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(LibArchive REQUIRED)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable CTest integration.
enable_testing()

file(GLOB_RECURSE ATHENA_CORE_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM ATHENA_CORE_SOURCES "${PROJECT_SOURCE_DIR}/src/main.cpp")

add_library(athena_core SHARED
    ${ATHENA_CORE_SOURCES}
)
add_library(Athena::Core ALIAS athena_core)
target_include_directories(athena_core
    PUBLIC "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/src"
)
target_compile_features(athena_core PUBLIC cxx_std_20)
target_link_libraries(athena_core PRIVATE OpenSSL::Crypto LibArchive::LibArchive Threads::Threads)

add_executable(athena src/main.cpp)
target_link_libraries(athena PRIVATE Athena::Core)

athena_install_targets("${PROJECT_SOURCE_DIR}/include")

add_custom_target(appimage
    COMMAND "${CMAKE_COMMAND}" -E env
        "ATHENA_APPIMAGE_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
        "ATHENA_APPIMAGE_CONFIG=$<CONFIG>"
        "${PROJECT_SOURCE_DIR}/packaging/build-appimage.sh"
    DEPENDS athena
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Building Athena Package Manager AppImage"
)

function(add_athena_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE Athena::Core)
    target_include_directories(${name} PRIVATE "${PROJECT_SOURCE_DIR}/src")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

set(ATHENA_TESTS
    operational_plan_test
    activation_reconciliation_test
    activation_selection_test
    activation_plan_retention_test
    activation_plan_persistence_test
    legacy_d3_test
    activation_projection_fs_test
    activation_current_test
    activation_schema_test
    activation_digest_test
    transaction_gc_fault_test
    transaction_gc_protocol_test
    transaction_gc_test
    immutable_publication_test
    locking_test
    durable_filesystem_test
    installed_semantics_regression_test
    installed_semantics_test
    installed_semantics_integration_test
    cli_install_test
    cli_install_integration_test
    install_transaction_test
    install_transaction_integration_test
    activation_projection_test
    generation_activation_test
    generation_membership_test
    realization_executor_test
    realization_executor_integration_test
    realization_record_test
    object_store_test
    object_store_scanner_test
    artifact_encoding_test
    artifact_identity_test
    artifact_identity_scanner_test
    artifact_model_test
    artifact_scanner_test
    artifact_contract_test
    arch_repository_test
    arch_repository_adapter_test
    arch_version_test
    cmake_backend_test
    version_test
    package_loader_test
    dependency_resolver_test
    install_plan_test
    generation_builder_test
    dependency_expression_test
    dependency_roles_test
    dependency_scope_precedence_test
    dependency_expression_resolution_test
    dependency_constraint_test
    dependency_parser_test
    dependency_serializer_test
    dependency_validator_test
    remove_plan_test
    removal_planner_test
    activation_test
    generation_test
    installer_e2e_test
    store_metadata_test
    gc_test
    repository_index_test
    repository_test
    repository_api_types_test
    metadata_schema_test
    repository_snapshot_test
    repository_selection_test
    resolver_candidate_test
    resolver_snapshot_adapter_test
    install_request_resolution_test
    local_repository_test
    removal_e2e_test
    removal_realization_test
    core_foundation_test
    public_planning_test
    public_install_plan_test
    resolution_result_test
)

foreach(test_name IN LISTS ATHENA_TESTS)
    add_athena_test(${test_name})
endforeach()
set_tests_properties(package_loader_test PROPERTIES
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
set_tests_properties(installed_semantics_integration_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
set_tests_properties(artifact_identity_scanner_test PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(artifact_scanner_test PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(object_store_scanner_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
set_tests_properties(object_store_test PROPERTIES TIMEOUT 90)

# Built for manual, opt-in integration checks; intentionally not registered
# with CTest because it requires network access and a live Arch mirror.
add_executable(arch_repository_live_test tests/arch_repository_live.cpp)
target_link_libraries(arch_repository_live_test PRIVATE Athena::Core)
target_include_directories(arch_repository_live_test PRIVATE "${PROJECT_SOURCE_DIR}/src")

# The planning acceptance consumer sees only exported headers.
set_property(TARGET public_planning_test PROPERTY INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/include")

set_tests_properties(generation_activation_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)

set_tests_properties(realization_executor_integration_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)

set_tests_properties(install_transaction_integration_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
set_tests_properties(cli_install_integration_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)

add_test(NAME removal_realization_integration_test COMMAND removal_realization_test integration)
set_tests_properties(removal_realization_integration_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    athena_add_packaging_smoke_test("${PROJECT_SOURCE_DIR}/include")
    configure_file(tests/packaging_regression_test.cmake.in
        packaging_regression_test.cmake @ONLY)
    add_test(NAME packaging_regression_test
        COMMAND "${CMAKE_COMMAND}" -DCONFIG=$<CONFIG>
            -P "${CMAKE_CURRENT_BINARY_DIR}/packaging_regression_test.cmake")
    set_tests_properties(packaging_regression_test PROPERTIES LABELS packaging TIMEOUT 180)
endif()

target_link_libraries(locking_test PRIVATE Threads::Threads)
set_tests_properties(locking_test durable_filesystem_test PROPERTIES TIMEOUT 30)

set_tests_properties(immutable_publication_test PROPERTIES TIMEOUT 60)

add_test(NAME immutable_object_publication_test COMMAND immutable_publication_test objects)
set_tests_properties(immutable_object_publication_test PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

add_test(NAME transaction_gc_crash_test COMMAND transaction_gc_test crash)
add_test(NAME transaction_gc_concurrency_test COMMAND transaction_gc_test concurrency)
add_test(NAME transaction_gc_scanner_test COMMAND transaction_gc_test scanner)
set_tests_properties(transaction_gc_test transaction_gc_crash_test transaction_gc_concurrency_test PROPERTIES TIMEOUT 60)
set_tests_properties(transaction_gc_scanner_test PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77)

set_tests_properties(transaction_gc_protocol_test PROPERTIES TIMEOUT 60)

set_tests_properties(transaction_gc_fault_test PROPERTIES TIMEOUT 60)

set_tests_properties(activation_current_test activation_digest_test PROPERTIES TIMEOUT 60)
# Schema stress coverage parses complete 65,536-entry documents in Debug too.
set_tests_properties(activation_schema_test PROPERTIES TIMEOUT 180)

add_executable(activation_execution_privileged_test tests/activation_execution_privileged_test.cpp)
target_link_libraries(activation_execution_privileged_test PRIVATE Athena::Core)
target_include_directories(activation_execution_privileged_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
foreach(case sources legacy owner bind nested ext4)
    add_test(NAME activation_execution_privileged_${case}_test COMMAND activation_execution_privileged_test ${case})
    set_tests_properties(activation_execution_privileged_${case}_test PROPERTIES LABELS "privileged" SKIP_RETURN_CODE 77 TIMEOUT 180)
endforeach()
set_tests_properties(activation_selection_test activation_reconciliation_test PROPERTIES TIMEOUT 180)
set_tests_properties(activation_selection_test PROPERTIES TIMEOUT 600)
add_test(NAME activation_selection_metadata_test COMMAND activation_selection_test metadata)
set_tests_properties(activation_selection_metadata_test PROPERTIES TIMEOUT 30)
# These purity checks snapshot their whole working directory. Other CTest cases
# create build fixtures there, so they must observe an otherwise idle test tree.
set_tests_properties(public_planning_test resolution_result_test PROPERTIES RUN_SERIAL TRUE)
set_tests_properties(activation_projection_fs_test PROPERTIES TIMEOUT 60)
set_tests_properties(activation_plan_persistence_test PROPERTIES TIMEOUT 120)
set_tests_properties(activation_plan_retention_test PROPERTIES TIMEOUT 180)
foreach(mode IN ITEMS limits budgets processes kills)
    add_test(NAME activation_plan_retention_${mode}_test COMMAND activation_plan_retention_test ${mode})
    set_tests_properties(activation_plan_retention_${mode}_test PROPERTIES TIMEOUT 300)
endforeach()
foreach(case IN ITEMS owner bind nested ext4)
    add_test(NAME activation_plan_retention_privileged_${case}_test
        COMMAND activation_plan_retention_test privileged-${case})
    set_tests_properties(activation_plan_retention_privileged_${case}_test
        PROPERTIES SKIP_RETURN_CODE 77 LABELS "privileged" TIMEOUT 180)
endforeach()
# Native locator adoption uses the default filesystem provider. Keep its fault
# provider confined to this one CTest process; it is never linked into Athena.
add_library(athena_d4_locator_fault SHARED tests/activation_plan_persistence_test.cpp)
target_compile_definitions(athena_d4_locator_fault PRIVATE ATHENA_D4_LOCATOR_FAULT_LIBRARY)
target_link_libraries(activation_plan_persistence_test PRIVATE ${CMAKE_DL_LIBS})
add_dependencies(activation_plan_persistence_test athena_d4_locator_fault)
add_test(NAME activation_plan_persistence_locator-cloexec_test
    COMMAND ${CMAKE_COMMAND} -E env "LD_PRELOAD=$<TARGET_FILE:athena_d4_locator_fault>"
        $<TARGET_FILE:activation_plan_persistence_test> locator-cloexec)
set_tests_properties(activation_plan_persistence_locator-cloexec_test PROPERTIES TIMEOUT 180)
add_test(NAME activation_plan_persistence_filesystem-errors_test
    COMMAND ${CMAKE_COMMAND} -E env "LD_PRELOAD=$<TARGET_FILE:athena_d4_locator_fault>"
        $<TARGET_FILE:activation_plan_persistence_test> filesystem-errors)
set_tests_properties(activation_plan_persistence_filesystem-errors_test PROPERTIES TIMEOUT 180)
foreach(mode IN ITEMS faults limits bounds kills processes privileged publication-capacity recovery-capacity recovery-cloexec recovery-kills)
    add_test(NAME activation_plan_persistence_${mode}_test COMMAND activation_plan_persistence_test ${mode})
    set_tests_properties(activation_plan_persistence_${mode}_test PROPERTIES TIMEOUT 180)
endforeach()
foreach(path IN ITEMS fresh bootstrap reused published)
    add_test(NAME activation_plan_persistence_boundaries_${path}_test COMMAND activation_plan_persistence_test boundaries-${path})
    set_tests_properties(activation_plan_persistence_boundaries_${path}_test PROPERTIES TIMEOUT 600)
endforeach()
set_tests_properties(activation_plan_persistence_bounds_test PROPERTIES TIMEOUT 600)
set_tests_properties(activation_plan_persistence_privileged_test PROPERTIES SKIP_RETURN_CODE 77 LABELS "privileged")
set_tests_properties(legacy_d3_test PROPERTIES TIMEOUT 60)
add_test(NAME legacy_d3_scanner_test COMMAND legacy_d3_test scanner)
set_tests_properties(legacy_d3_scanner_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)

# T17 cases have independent outcomes: a missing prerequisite is never a pass.
foreach(case IN ITEMS owner bind nested tmpfs ext4)
    add_test(NAME activation_plan_persistence_privileged_${case}_test
        COMMAND activation_plan_persistence_test privileged-${case})
    set_tests_properties(activation_plan_persistence_privileged_${case}_test
        PROPERTIES SKIP_RETURN_CODE 77 LABELS "privileged" TIMEOUT 180)
endforeach()
set_tests_properties(legacy_d3_scanner_test PROPERTIES LABELS "privileged")

# Cleanup regression uses simulated umount outcomes and needs no privileges.
add_test(NAME activation_plan_persistence_qualification_cleanup_test
    COMMAND activation_plan_persistence_test qualification-cleanup)
set_tests_properties(activation_plan_persistence_qualification_cleanup_test PROPERTIES TIMEOUT 30)
