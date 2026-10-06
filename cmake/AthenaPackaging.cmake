include(GNUInstallDirs)

# Normalize lexical '..' components, retaining relative paths within the prefix.
foreach(dir BINDIR LIBDIR INCLUDEDIR)
    set(normalized "${CMAKE_INSTALL_${dir}}")
    if(IS_ABSOLUTE "${normalized}")
        message(FATAL_ERROR "CMAKE_INSTALL_${dir} must be relative to the prefix")
    endif()
    cmake_path(NORMAL_PATH normalized)
    string(REGEX REPLACE "/+$" "" normalized "${normalized}")
    if(normalized MATCHES "^\\.\\.(/|$)")
        message(FATAL_ERROR "CMAKE_INSTALL_${dir} escapes the prefix")
    endif()
    if(normalized STREQUAL "")
        set(normalized ".")
    endif()
    set(CMAKE_INSTALL_${dir} "${normalized}")
endforeach()

function(athena_install_targets header_dir)
    file(RELATIVE_PATH bin_to_lib "/${CMAKE_INSTALL_BINDIR}" "/${CMAKE_INSTALL_LIBDIR}")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_target_properties(athena PROPERTIES
            INSTALL_RPATH "$ORIGIN/${bin_to_lib}"
            INSTALL_RPATH_USE_LINK_PATH FALSE
            BUILD_WITH_INSTALL_RPATH FALSE)
        target_link_options(athena PRIVATE "LINKER:--enable-new-dtags")
        # Preserve core's normal INSTALL_RPATH initialization from
        # CMAKE_INSTALL_RPATH. The default is empty; packagers may explicitly
        # supply relocatable paths for deployed dependencies. Never add paths
        # automatically just because find_package discovered libraries there.
        set_target_properties(athena_core PROPERTIES INSTALL_RPATH_USE_LINK_PATH FALSE)
        target_link_options(athena_core PRIVATE "LINKER:--enable-new-dtags")
    endif()
    install(TARGETS athena RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(TARGETS athena_core
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(DIRECTORY "${header_dir}/athena" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../packaging/check-appimage-bindir.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/check-appimage-bindir.cmake" @ONLY)
endfunction()

function(athena_add_packaging_smoke_test header_dir)
    find_program(ATHENA_READELF readelf REQUIRED)
    find_program(ATHENA_LDD ldd REQUIRED)
    find_program(ATHENA_MKTEMP mktemp REQUIRED)
    set(ATHENA_HEADER_DIR "${header_dir}")
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tests/packaging_install_test.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/packaging_install_test.cmake" @ONLY)
    add_test(NAME packaging_install_test
        COMMAND "${CMAKE_COMMAND}" -DCONFIG=$<CONFIG>
            "-DCORE_NAME=$<TARGET_FILE_NAME:athena_core>"
            "-DCORE_SONAME=$<TARGET_SONAME_FILE_NAME:athena_core>"
            "-DCORE_LINK_NAME=$<TARGET_LINKER_FILE_NAME:athena_core>"
            "-DCORE_RPATH=$<TARGET_GENEX_EVAL:athena_core,$<TARGET_PROPERTY:athena_core,INSTALL_RPATH>>"
            -P "${CMAKE_CURRENT_BINARY_DIR}/packaging_install_test.cmake")
    set_tests_properties(packaging_install_test PROPERTIES LABELS packaging TIMEOUT 90)
endfunction()
