file(GLOB_RECURSE LPC_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(lpc_core STATIC ${LPC_CORE_SOURCES})
target_include_directories(lpc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(lpc_core PRIVATE ${CMAKE_SOURCE_DIR}/third_party)  # dr_mp3 and dr_flac (public domain / MIT-0)
target_link_libraries(lpc_core PUBLIC nlohmann_json::nlohmann_json)
find_package(Threads REQUIRED)
target_link_libraries(lpc_core PUBLIC Threads::Threads)
set_target_properties(lpc_core PROPERTIES LINKER_LANGUAGE CXX)
if(WIN32)
    target_link_libraries(lpc_core PUBLIC mf mfplat mfreadwrite mfuuid ole32)  # the MP3 and AAC encoders of Windows Media Foundation (bounce to MP3 and M4A)
endif()

# Neural models through onnxruntime (third_party/onnxruntime/onnxruntime-win-x64-*, not in git: see docs/logic-reference/ml-conversion.md).
# Without it lpc::ml reports "not available" and every caller keeps its classical algorithm.
option(LPC_WITH_ONNX "Use onnxruntime for Logic Pro's converted neural models" ON)
set(LPC_ORT_DLLS "" CACHE INTERNAL "onnxruntime DLLs to copy next to executables")
if(LPC_WITH_ONNX AND WIN32)
    file(GLOB LPC_ORT_DIRS ${CMAKE_SOURCE_DIR}/third_party/onnxruntime/onnxruntime-win-x64-*)
    if(LPC_ORT_DIRS)
        list(GET LPC_ORT_DIRS 0 LPC_ORT_DIR)
        target_compile_definitions(lpc_core PUBLIC LPC_WITH_ONNX=1)
        target_include_directories(lpc_core SYSTEM PUBLIC ${LPC_ORT_DIR}/include)
        target_link_libraries(lpc_core PUBLIC ${LPC_ORT_DIR}/lib/onnxruntime.lib)
        set(LPC_ORT_DLLS "${LPC_ORT_DIR}/lib/onnxruntime.dll;${LPC_ORT_DIR}/lib/onnxruntime_providers_shared.dll" CACHE INTERNAL "")
        message(STATUS "onnxruntime: ${LPC_ORT_DIR}")
    endif()
endif()
# copies onnxruntime.dll next to an executable that links lpc_core
function(lpc_copy_onnx_dlls target)
    if(LPC_ORT_DLLS)
        add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LPC_ORT_DLLS} $<TARGET_FILE_DIR:${target}>)
    endif()
endfunction()

# Neural models for an executable: copied from LPC_MODELS_SOURCE (default jad-daw-private/onnx, not in git) into <exe dir>/models after each build.
# Without the folder the app runs with its classical algorithms.
set(LPC_MODELS_SOURCE "${CMAKE_SOURCE_DIR}/jad-daw-private/onnx" CACHE PATH "Folder with the converted .onnx models")
function(lpc_copy_models target)
    if(LPC_WITH_ONNX AND IS_DIRECTORY "${LPC_MODELS_SOURCE}")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -DSRC=${LPC_MODELS_SOURCE} -DDST=$<TARGET_FILE_DIR:${target}>/models -P ${CMAKE_SOURCE_DIR}/cmake/copy_models.cmake
            COMMENT "neural models for ${target}")
    endif()
endfunction()
