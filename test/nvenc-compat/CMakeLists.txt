cmake_minimum_required(VERSION 3.28...3.30)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(nvenc-api-regressions LANGUAGES C)
  enable_testing()
endif()

include("../../plugins/obs-nvenc/cmake/headers.cmake")

add_executable(test-nvenc-api test-nvenc-api.c)
target_include_directories(test-nvenc-api PRIVATE ../../plugins/obs-nvenc)

foreach(api IN ITEMS 12_1 12_2 13_0)
  add_library(test-nvenc-abi-${api} OBJECT test-nvenc-abi.c)
  target_include_directories(test-nvenc-abi-${api} PRIVATE ../../plugins/obs-nvenc)
  nvenc_target_headers(test-nvenc-abi-${api} ${api})
  target_compile_definitions(test-nvenc-abi-${api} PRIVATE test_nvenc_abi=test_nvenc_abi_${api})
  target_link_libraries(test-nvenc-api PRIVATE test-nvenc-abi-${api})
endforeach()

if(MSVC)
  target_compile_options(test-nvenc-api PRIVATE /W4 /WX)
  foreach(api IN ITEMS 12_1 12_2 13_0)
    target_compile_options(test-nvenc-abi-${api} PRIVATE /W4 /WX)
  endforeach()
else()
  target_compile_options(test-nvenc-api PRIVATE -Wall -Wextra -Werror)
  foreach(api IN ITEMS 12_1 12_2 13_0)
    target_compile_options(test-nvenc-abi-${api} PRIVATE -Wall -Wextra -Werror)
  endforeach()
endif()

if(OS_WINDOWS)
  set_target_properties(
    test-nvenc-api PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/rundir/$<CONFIG>/bin/64bit"
  )
endif()

set_target_properties(test-nvenc-api PROPERTIES FOLDER "Tests and Examples")
add_test(NAME nvenc-api-compatibility COMMAND test-nvenc-api)
