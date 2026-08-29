add_executable(spmv main.cc)
target_link_libraries(spmv PRIVATE OpenCL::OpenCL)
target_compile_definitions(spmv PRIVATE
  ${OCL_DEFAULT_OPTS}
  KERNEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/kernel.cl")

# --- SpMV Benchmark Data Download (Configure Time) ---
# Matrices are fetched at configuration time to ensure they are available for tests.
# These files are intentionally not committed to the repository.
set(SPMV_DATA_DIR "${CMAKE_CURRENT_BINARY_DIR}/data")
file(MAKE_DIRECTORY "${SPMV_DATA_DIR}")

function(fetch_spmv_matrix NAME URL HASH ARCHIVE MTX_FILE)
  if(EXISTS "${MTX_FILE}")
    return()
  endif()

  if(NOT EXISTS "${ARCHIVE}")
    message(STATUS "Downloading ${NAME} benchmark matrix...")
    file(DOWNLOAD "${URL}" "${ARCHIVE}"
         EXPECTED_HASH "SHA256=${HASH}"
         TLS_VERIFY ON
         SHOW_PROGRESS
         STATUS status)
    list(GET status 0 error_code)
    if(NOT error_code EQUAL 0)
      file(REMOVE "${ARCHIVE}") # Clean up partial download
      message(FATAL_ERROR "Download failed for ${NAME}: ${status}")
    endif()
  endif()

  message(STATUS "Extracting ${NAME}...")
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xzf "${ARCHIVE}"
    WORKING_DIRECTORY "${SPMV_DATA_DIR}"
    RESULT_VARIABLE tar_result
  )
  if(NOT tar_result EQUAL 0)
    message(FATAL_ERROR "Extraction failed for ${NAME}")
  endif()
endfunction()

# 1. MSC00726 (Regular/Small) - Group: Boeing
# The legacy collection host only serves HTTP; use its public S3 HTTPS endpoint.
set(MSC_URL "https://s3.us-east-2.amazonaws.com/sparse-files.engr.tamu.edu/MM/Boeing/msc00726.tar.gz")
set(MSC_HASH "3b5cf6684c8f6f264ff5d67988aa07c327ff5be6286bfe11b00f482e2c45578c")
set(MSC_ARCHIVE "${SPMV_DATA_DIR}/msc00726.tar.gz")
set(MSC_MTX "${SPMV_DATA_DIR}/msc00726/msc00726.mtx")
fetch_spmv_matrix("msc00726" "${MSC_URL}" "${MSC_HASH}" "${MSC_ARCHIVE}" "${MSC_MTX}")

# 2. DC2 (Irregular/Large) - Group: IBM_EDA
set(DC2_URL "https://s3.us-east-2.amazonaws.com/sparse-files.engr.tamu.edu/MM/IBM_EDA/dc2.tar.gz")
set(DC2_HASH "14e039beb453b1d4c9c94471f3a699b18acafb5b0e508e3cb7cf5cba1f4288d1")
set(DC2_ARCHIVE "${SPMV_DATA_DIR}/dc2.tar.gz")
set(DC2_MTX "${SPMV_DATA_DIR}/dc2/dc2.mtx")
fetch_spmv_matrix("dc2" "${DC2_URL}" "${DC2_HASH}" "${DC2_ARCHIVE}" "${DC2_MTX}")

# Project-consistent integration tests
add_lv_opencl_integration_test(spmv SUFFIX regular ARGS ${MSC_MTX})
add_lv_opencl_integration_test(spmv SUFFIX irregular ARGS ${DC2_MTX})
