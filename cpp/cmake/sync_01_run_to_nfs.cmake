if(NOT DEFINED SRC_SCRIPT OR NOT DEFINED DST_DIR)
  message(WARNING "sync_01_run_to_nfs: missing SRC_SCRIPT or DST_DIR")
  return()
endif()

if(NOT EXISTS "${SRC_SCRIPT}")
  message(WARNING "sync_01_run_to_nfs: source script not found: ${SRC_SCRIPT}")
  return()
endif()

file(MAKE_DIRECTORY "${DST_DIR}")

set(DST_SCRIPT "${DST_DIR}/01_run.sh")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SRC_SCRIPT}" "${DST_SCRIPT}"
  RESULT_VARIABLE COPY_RET
  ERROR_VARIABLE COPY_ERR
)

if(NOT COPY_RET EQUAL 0)
  string(STRIP "${COPY_ERR}" COPY_ERR_STRIPPED)
  message(WARNING "sync_01_run_to_nfs: copy failed: ${COPY_ERR_STRIPPED}")
endif()

if(COPY_RET EQUAL 0)
  message(STATUS "sync_01_run_to_nfs: copied to ${DST_SCRIPT}")
endif()
