cmake_minimum_required(VERSION 3.20)

project(athena
    VERSION 0.1.0
    DESCRIPTION "Athena Package Manager"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable CTest integration.
enable_testing()

add_executable(athena
    src/main.cpp
    src/cli/cli.cpp
    src/cli/command_line.cpp
    src/package/package.cpp
    src/package/package_loader.cpp
    src/dependency/parser.cpp
    src/download/downloader.cpp
    src/verify/verifier.cpp
    src/install/installer.cpp
    src/install/remover.cpp
    src/install/removal_planner.cpp
    src/dependency/installed_environment.cpp
    src/dependency/validator.cpp
    src/install/install_plan.cpp
    src/install/generation_builder.cpp
    src/install/remove_plan.cpp
    src/paths/paths.cpp
    src/archive/archive.cpp
    src/build/builder.cpp
    src/build/autotools_backend.cpp
    src/build/cmake_backend.cpp
    src/store/store.cpp
    src/store/metadata.cpp
    src/dependency/serializer.cpp
    src/store/manifest.cpp
    src/activation/activation.cpp
    src/generation/generation.cpp
    src/version/version.cpp
    src/dependency/constraint.cpp
    src/dependency/serializer.cpp
)

target_include_directories(athena
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

# Standalone test for the CMake build backend.
add_executable(cmake_backend_test
    tests/cmake_backend_test.cpp
    src/build/cmake_backend.cpp
)

target_include_directories(cmake_backend_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/build
)

# Standalone test for version comparison.
add_executable(version_test
    tests/version_test.cpp
    src/version/version.cpp
)

target_include_directories(version_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/version
)

# Standalone test for package manifest loading.
add_executable(package_loader_test
    tests/package_loader_test.cpp
    src/package/package.cpp
    src/package/package_loader.cpp
    src/dependency/parser.cpp
)

target_include_directories(package_loader_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/package
)

# Standalone test for dependency resolution.
add_executable(dependency_resolver_test
    tests/dependency_resolver_test.cpp
    src/repository/repository.cpp
    src/repository/repository_index.cpp
    src/package/package_loader.cpp
    src/dependency/resolver.cpp
    src/dependency/parser.cpp
    src/dependency/constraint.cpp
    src/version/version.cpp
    src/install/install_plan.cpp
)

target_include_directories(dependency_resolver_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/repository
        ${PROJECT_SOURCE_DIR}/src/dependency
        ${PROJECT_SOURCE_DIR}/src/package
        ${PROJECT_SOURCE_DIR}/src/install
)

# Test the InstallPlan abstraction used to transfer the dependency
# resolver result to the installation layer without performing any
# installation work.
add_executable(install_plan_test
    tests/install_plan_test.cpp
    src/install/install_plan.cpp
)


# Test construction of the target generation produced by an installation plan.
add_executable(generation_builder_test
    tests/generation_builder_test.cpp
    src/install/generation_builder.cpp
    src/install/remove_plan.cpp
)

target_include_directories(generation_builder_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/install
        ${PROJECT_SOURCE_DIR}/src/generation
        ${PROJECT_SOURCE_DIR}/src/package
)

target_include_directories(install_plan_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/install
        ${PROJECT_SOURCE_DIR}/src/package
)

add_test(NAME install_plan_test COMMAND install_plan_test)

# Install the Athena package manager executable.
install(
    TARGETS athena
    RUNTIME DESTINATION usr/bin
)

# Build the Athena Package Manager AppImage.
add_custom_target(appimage
    COMMAND
        "${PROJECT_SOURCE_DIR}/packaging/build-appimage.sh"
    DEPENDS
        athena
    WORKING_DIRECTORY
        "${PROJECT_SOURCE_DIR}"
    COMMENT
        "Building Athena Package Manager AppImage"
)

# Standalone test for the universal dependency expression model.
add_executable(dependency_expression_test
    tests/dependency_expression_test.cpp
)

target_include_directories(dependency_expression_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/dependency
)

# Standalone test for version constraints.
add_executable(dependency_constraint_test
    tests/dependency_constraint_test.cpp
    src/dependency/constraint.cpp
    src/version/version.cpp
)

target_include_directories(dependency_constraint_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/dependency
)

# Register standalone tests with CTest.
add_test(NAME cmake_backend_test COMMAND cmake_backend_test)
add_test(NAME version_test COMMAND version_test)
add_test(NAME package_loader_test COMMAND package_loader_test WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
add_test(NAME dependency_resolver_test COMMAND dependency_resolver_test)
add_test(NAME dependency_expression_test COMMAND dependency_expression_test)
add_test(NAME dependency_constraint_test COMMAND dependency_constraint_test)

# Standalone test for dependency expression parsing.
add_executable(dependency_parser_test
    tests/dependency_parser_test.cpp
    src/dependency/parser.cpp
    src/dependency/constraint.cpp
    src/version/version.cpp
)

target_include_directories(dependency_parser_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/dependency
        ${PROJECT_SOURCE_DIR}/src/version
)

add_test(NAME dependency_parser_test COMMAND dependency_parser_test)

# Standalone test for dependency expression serialization and
# parser/serializer round-trip stability.
add_executable(dependency_serializer_test
    tests/dependency_serializer_test.cpp
    src/dependency/serializer.cpp
    src/dependency/parser.cpp
    src/dependency/constraint.cpp
    src/version/version.cpp
)

target_include_directories(dependency_serializer_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/dependency
        ${PROJECT_SOURCE_DIR}/src/version
)

add_test(NAME dependency_serializer_test COMMAND dependency_serializer_test)

# Validate dependency expressions against an already installed
# candidate environment, including removal-safety scenarios.
add_executable(dependency_validator_test
    tests/dependency_validator_test.cpp
    src/dependency/installed_environment.cpp
    src/dependency/validator.cpp
    src/dependency/parser.cpp
    src/dependency/constraint.cpp
    src/version/version.cpp
)

target_include_directories(dependency_validator_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/package
        ${PROJECT_SOURCE_DIR}/src/dependency
        ${PROJECT_SOURCE_DIR}/src/version
)

add_test(NAME dependency_validator_test COMMAND dependency_validator_test)

# Test the removal plan abstraction independently from generation
# mutation and filesystem operations.
add_executable(remove_plan_test
    tests/remove_plan_test.cpp
    src/install/remove_plan.cpp
)

target_include_directories(remove_plan_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/install
)

add_test(NAME remove_plan_test COMMAND remove_plan_test)

# Test removal planning against a candidate installed environment.
#
# RemovalPlanner validates the complete requested removal set through
# DependencyValidator without touching Generation, Store or filesystem
# state.
add_executable(removal_planner_test
    tests/removal_planner_test.cpp
    src/install/removal_planner.cpp
    src/install/remove_plan.cpp
    src/dependency/installed_environment.cpp
    src/dependency/validator.cpp
    src/dependency/parser.cpp
    src/dependency/constraint.cpp
    src/version/version.cpp
)

target_include_directories(removal_planner_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/package
        ${PROJECT_SOURCE_DIR}/src/dependency
        ${PROJECT_SOURCE_DIR}/src/install
        ${PROJECT_SOURCE_DIR}/src/version
)

add_test(NAME removal_planner_test COMMAND removal_planner_test)

# Standalone test for package activation and deactivation.
add_executable(activation_test
    tests/activation_test.cpp
    src/activation/activation.cpp
    src/store/manifest.cpp
)

target_include_directories(activation_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/activation
        ${PROJECT_SOURCE_DIR}/src/store
)

add_test(NAME activation_test COMMAND activation_test)

# Standalone test for immutable generation management.
add_executable(generation_test
    tests/generation_test.cpp
    src/generation/generation.cpp
    src/activation/activation.cpp
    src/store/manifest.cpp
)

target_include_directories(generation_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/generation
)

add_test(NAME generation_test COMMAND generation_test)

# End-to-end test for the complete InstallPlan -> Store -> Generation
# -> Activation pipeline using a local CMake fixture and temporary paths.
add_test(NAME generation_builder_test COMMAND generation_builder_test)

add_executable(installer_e2e_test
    tests/installer_e2e_test.cpp
    src/install/installer.cpp
    src/package/package.cpp
    src/package/package_loader.cpp
    src/dependency/parser.cpp
    src/dependency/constraint.cpp
    src/version/version.cpp
    src/install/install_plan.cpp
    src/install/generation_builder.cpp
    src/install/remove_plan.cpp
    src/download/downloader.cpp
    src/verify/verifier.cpp
    src/paths/paths.cpp
    src/archive/archive.cpp
    src/build/builder.cpp
    src/build/autotools_backend.cpp
    src/build/cmake_backend.cpp
    src/store/store.cpp
    src/store/metadata.cpp
    src/store/manifest.cpp
    src/dependency/serializer.cpp
    src/activation/activation.cpp
    src/generation/generation.cpp
)

target_include_directories(installer_e2e_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/install
        ${PROJECT_SOURCE_DIR}/src/package
        ${PROJECT_SOURCE_DIR}/src/dependency
        ${PROJECT_SOURCE_DIR}/src/paths
        ${PROJECT_SOURCE_DIR}/src/store
        ${PROJECT_SOURCE_DIR}/src/generation
        ${PROJECT_SOURCE_DIR}/src/activation
        ${PROJECT_SOURCE_DIR}/src/build
)

add_test(NAME installer_e2e_test COMMAND installer_e2e_test)

# Verify that package dependencies are persisted in Store metadata
# and can be restored, while preserving compatibility with legacy
# metadata files that do not contain the dependencies field.
add_executable(store_metadata_test
    tests/store_metadata_test.cpp
    src/package/package.cpp
    src/dependency/parser.cpp
    src/dependency/constraint.cpp
    src/dependency/serializer.cpp
    src/version/version.cpp
    src/paths/paths.cpp
    src/store/store.cpp
    src/store/metadata.cpp
    src/store/manifest.cpp
)

target_include_directories(store_metadata_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/package
        ${PROJECT_SOURCE_DIR}/src/dependency
        ${PROJECT_SOURCE_DIR}/src/paths
        ${PROJECT_SOURCE_DIR}/src/store
        ${PROJECT_SOURCE_DIR}/src/version
)

add_test(NAME store_metadata_test COMMAND store_metadata_test)

# Standalone test for immutable store garbage collection.
add_executable(gc_test
    tests/gc_test.cpp
    src/store/gc.cpp
    src/store/metadata.cpp
    src/generation/generation.cpp
    src/activation/activation.cpp
    src/store/manifest.cpp
)

target_include_directories(gc_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/store
        ${PROJECT_SOURCE_DIR}/src/generation
        ${PROJECT_SOURCE_DIR}/src/activation
)

add_test(NAME gc_test COMMAND gc_test)

# Standalone test for the in-memory repository index.
add_executable(repository_index_test
    tests/repository_index_test.cpp
    src/repository/repository_index.cpp
    src/package/package.cpp
    src/version/version.cpp
)

target_include_directories(repository_index_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/repository
        ${PROJECT_SOURCE_DIR}/src/package
        ${PROJECT_SOURCE_DIR}/src/version
)

add_test(NAME repository_index_test COMMAND repository_index_test)

# Test the high-level repository abstraction, including package loading
# from package.toml files and insertion into the repository index.
add_executable(repository_test
    tests/repository_test.cpp
    src/repository/repository.cpp
    src/repository/repository_index.cpp
    src/package/package.cpp
    src/package/package_loader.cpp
    src/dependency/parser.cpp
    src/dependency/constraint.cpp
    src/version/version.cpp
)

target_include_directories(repository_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/repository
        ${PROJECT_SOURCE_DIR}/src/package
        ${PROJECT_SOURCE_DIR}/src/dependency
        ${PROJECT_SOURCE_DIR}/src/version
)

add_test(NAME repository_test COMMAND repository_test)


add_executable(removal_e2e_test
    tests/removal_e2e_test.cpp
    src/install/remover.cpp
    src/install/removal_planner.cpp
    src/install/remove_plan.cpp
    src/install/generation_builder.cpp
    src/dependency/installed_environment.cpp
    src/dependency/validator.cpp
    src/dependency/parser.cpp
    src/dependency/constraint.cpp
    src/version/version.cpp
    src/store/metadata.cpp
    src/store/manifest.cpp
    src/paths/paths.cpp
    src/generation/generation.cpp
    src/activation/activation.cpp
)

target_include_directories(removal_e2e_test PRIVATE
    src
    src/install
    src/dependency
    src/version
    src/store
    src/paths
    src/generation
    src/activation
)

add_test(
    NAME removal_e2e_test
    COMMAND removal_e2e_test
)
