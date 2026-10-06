cmake_minimum_required(VERSION 3.20)

project(athena
    VERSION 0.1.0
    DESCRIPTION "Athena Package Manager"
    LANGUAGES CXX
)

find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(LibArchive REQUIRED)

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
target_link_libraries(athena_core PRIVATE OpenSSL::Crypto LibArchive::LibArchive)

add_executable(athena src/main.cpp)
target_link_libraries(athena PRIVATE Athena::Core)

# Install the executable, shared core library, and public source-level headers.
install(TARGETS athena RUNTIME DESTINATION usr/bin)
install(TARGETS athena_core
    LIBRARY DESTINATION usr/lib
    ARCHIVE DESTINATION usr/lib
    RUNTIME DESTINATION usr/bin
)
install(DIRECTORY include/athena DESTINATION usr/include)

add_custom_target(appimage
    COMMAND "${PROJECT_SOURCE_DIR}/packaging/build-appimage.sh"
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
