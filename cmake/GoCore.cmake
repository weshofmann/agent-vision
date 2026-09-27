# PR1 keeps the local C++ desktop default. This executable is an independent
# inherited-stream backend, not a frontend cutover or installed service.
option(AGENTVISION_BUILD_CORE "Build the independently tested Go core" OFF)
set(AGENTVISION_GO_EXECUTABLE "" CACHE FILEPATH "Absolute qualified Go 1.27.0 executable")
set(_av_core_root "${CMAKE_CURRENT_LIST_DIR}/..")
if(AGENTVISION_BUILD_CORE)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR
       (CMAKE_SYSTEM_PROCESSOR AND NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64") OR
       (CMAKE_OSX_ARCHITECTURES AND NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64"))
        message(FATAL_ERROR "The Go core requires the qualified Darwin arm64 target")
    endif()
    if(NOT IS_ABSOLUTE "${AGENTVISION_GO_EXECUTABLE}" OR NOT EXISTS "${AGENTVISION_GO_EXECUTABLE}")
        message(FATAL_ERROR "Set an existing absolute AGENTVISION_GO_EXECUTABLE for Go 1.27.0")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env GOTOOLCHAIN=local GOENV=off
        "${AGENTVISION_GO_EXECUTABLE}" version
        RESULT_VARIABLE _av_go_result OUTPUT_VARIABLE _av_go_version OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _av_go_result EQUAL 0 OR NOT _av_go_version STREQUAL "go version go1.27.0 darwin/arm64")
        message(FATAL_ERROR "The Go core requires exactly Go 1.27.0 darwin/arm64")
    endif()
endif()
function(agentvision_configure_core)
    if(NOT AGENTVISION_BUILD_CORE)
        return()
    endif()
    set(core "${_av_core_root}/core")
    if(NOT Python3_EXECUTABLE)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
    endif()
    set(creack_guard "${_av_core_root}/cmake/verify_creack_source.py")
    set(creack_patch "${_av_core_root}/patches/creack-pty-darwin-master-boundary.patch")
    set(creack_inputs "${core}/third_party/creack-pty/go.mod"
        "${core}/third_party/creack-pty.pristine.json"
        "${core}/third_party/creack-pty.downstream.json" "${creack_patch}" "${creack_guard}")
    # Always guard the entire effective set, even additions after configuration
    # when an existing executable would otherwise appear up to date.
    add_custom_target(core-source-guard
        COMMAND "${Python3_EXECUTABLE}" "${creack_guard}" "${_av_core_root}"
        VERBATIM)

    set(go_env GOTOOLCHAIN=local GOENV=off GOWORK=off GOFLAGS=
        GOOS=darwin GOARCH=arm64 CGO_ENABLED=0
        "GOCACHE=${_av_core_root}/.probe/go-cache"
        "GOMODCACHE=${_av_core_root}/.probe/go-modcache")
    file(GLOB_RECURSE go_sources CONFIGURE_DEPENDS "${core}/*.go")
    set(binary "${CMAKE_CURRENT_BINARY_DIR}/agentvision-core")
    add_custom_command(OUTPUT "${binary}"
        COMMAND "${Python3_EXECUTABLE}" "${creack_guard}" "${_av_core_root}"
        COMMAND "${CMAKE_COMMAND}" -E env ${go_env}
            "${AGENTVISION_GO_EXECUTABLE}" build -mod=readonly -trimpath -buildvcs=false
            -o "${binary}" ./cmd/agentvision-core
        WORKING_DIRECTORY "${core}"
        DEPENDS core-source-guard ${go_sources} ${creack_inputs} "${core}/go.mod" "${core}/go.sum"
        COMMENT "Building pinned Go inherited-stream core" VERBATIM)
    add_custom_target(agentvision-core-build ALL DEPENDS "${binary}")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/licenses")
    foreach(notice IN ITEMS creack-pty.LICENSE Go.LICENSE Go.PATENTS)
        configure_file("${_av_core_root}/third_party/notices/${notice}"
            "${CMAKE_CURRENT_BINARY_DIR}/licenses/${notice}" COPYONLY)
    endforeach()
    if(BUILD_TESTING)
        add_test(NAME core_replacement_guard COMMAND "${Python3_EXECUTABLE}"
            "${_av_core_root}/tests/test_creack_source_guard.py")
        set_tests_properties(core_replacement_guard PROPERTIES TIMEOUT 30)
        add_test(NAME core_go COMMAND "${CMAKE_COMMAND}" -E env ${go_env}
            CGO_ENABLED=1 "${AGENTVISION_GO_EXECUTABLE}" test -mod=readonly -race ./... -timeout=120s)
        set_tests_properties(core_go PROPERTIES WORKING_DIRECTORY "${core}" TIMEOUT 150)
        add_test(NAME core_client COMMAND "${Python3_EXECUTABLE}"
            "${_av_core_root}/tests/core_client.py" "${binary}")
        set_tests_properties(core_client PROPERTIES TIMEOUT 60)
        add_test(NAME core_build_config COMMAND "${Python3_EXECUTABLE}"
            "${_av_core_root}/tests/core_build_config.py" "${CMAKE_COMMAND}" "${AGENTVISION_GO_EXECUTABLE}")
        set_tests_properties(core_build_config PROPERTIES TIMEOUT 90)
    endif()
endfunction()
