# Fails (exit 1) when code under core/{include,src}/lpc/audio uses blocking or allocating primitives.
# Usage: cmake -DCORE_DIR=<path to core> -P check_no_locks.cmake
if(NOT DEFINED CORE_DIR)
    message(FATAL_ERROR "CORE_DIR is not set")
endif()

file(GLOB_RECURSE AUDIO_FILES
    ${CORE_DIR}/include/lpc/audio/*
    ${CORE_DIR}/src/audio/*)

if(NOT AUDIO_FILES)
    message(FATAL_ERROR "no files found under ${CORE_DIR}/.../audio: wrong CORE_DIR?")
endif()

set(BANNED "std::mutex|std::recursive_mutex|std::shared_mutex|std::timed_mutex|std::lock_guard|std::unique_lock|std::scoped_lock|std::condition_variable|std::future|std::promise|std::function|std::async")

set(VIOLATIONS "")
foreach(file ${AUDIO_FILES})
    file(READ ${file} content)
    # Comments may mention the banned names; strip `// ...` to the end of the line before matching.
    string(REGEX REPLACE "//[^\n]*" "" code "${content}")
    if(code MATCHES "(${BANNED})")
        list(APPEND VIOLATIONS "${file}: uses ${CMAKE_MATCH_1}")
    endif()
endforeach()

if(VIOLATIONS)
    string(REPLACE ";" "\n  " REPORT "${VIOLATIONS}")
    message(FATAL_ERROR "audio-thread code must not use locks or std::function:\n  ${REPORT}")
endif()
message(STATUS "audio directories are free of locks")
