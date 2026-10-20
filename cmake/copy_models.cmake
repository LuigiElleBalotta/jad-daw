# Copies the converted neural models the app uses from SRC to DST/models: the three analysis networks (Smart Tempo, Flex Pitch) and the
# Saturation families at the 256-sample block size, all sample rates. Run with cmake -DSRC=... -DDST=... -P copy_models.cmake.
file(MAKE_DIRECTORY "${DST}")
file(GLOB LPC_MODEL_FILES "${SRC}/*.model_256_transformed.onnx" "${SRC}/*.modelstreaming_transformed.onnx" "${SRC}/DownbeatTrackerCondMPS.model.onnx"
     "${SRC}/OnsetDetectionCNN.espresso.onnx" "${SRC}/RTPTM.model.onnx" "${SRC}/RTPT.espresso.onnx")
foreach(f ${LPC_MODEL_FILES})
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${f}" "${DST}")
endforeach()
