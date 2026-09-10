option(ENABLE_REPLAY_TESTS "Build disk replay and NVENC upload regression tests" OFF)
if(NOT ENABLE_REPLAY_TESTS)
  return()
endif()

add_executable(test-replay-storage test-replay-storage.c ../../plugins/obs-ffmpeg/replay-disk-storage.c)
target_link_libraries(test-replay-storage PRIVATE OBS::libobs $<$<PLATFORM_ID:Windows>:OBS::w32-pthreads>)
target_include_directories(test-replay-storage PRIVATE ../../plugins/obs-ffmpeg ../../plugins/obs-nvenc)
set_target_properties(test-replay-storage PROPERTIES FOLDER "Tests and Examples")

find_package(FFmpeg REQUIRED COMPONENTS avcodec avutil avformat)
add_executable(test-replay-mux test-replay-mux.c)
target_link_libraries(
  test-replay-mux
  PRIVATE OBS::libobs FFmpeg::avcodec FFmpeg::avutil FFmpeg::avformat $<$<PLATFORM_ID:Windows>:OBS::w32-pthreads>
)
set_target_properties(test-replay-mux PROPERTIES FOLDER "Tests and Examples")

if(OS_WINDOWS)
  add_executable(test-replay-encoding test-replay-encoding.c ../test-input/test-sinewave.c ../test-input/test-random.c)
  target_link_libraries(test-replay-encoding PRIVATE OBS::libobs OBS::w32-pthreads)
  # Run beside the freshly built libobs and its matching runtime dependencies.
  set_target_properties(
    test-replay-storage test-replay-encoding test-replay-mux
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/rundir/$<CONFIG>/bin/64bit"
  )
endif()
