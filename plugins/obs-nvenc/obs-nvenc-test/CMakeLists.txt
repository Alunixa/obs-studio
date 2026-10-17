cmake_minimum_required(VERSION 3.28...3.30)

find_package(FFnvcodec 12 REQUIRED)

add_executable(obs-nvenc-test)

target_sources(obs-nvenc-test PRIVATE obs-nvenc-test-main.cpp)
target_link_libraries(obs-nvenc-test PRIVATE FFnvcodec::FFnvcodec)

foreach(api IN ITEMS 12_1 12_2 13_0)
  add_library(obs-nvenc-probe-${api} OBJECT obs-nvenc-test.cpp)
  nvenc_target_headers(obs-nvenc-probe-${api} ${api})
  target_compile_definitions(obs-nvenc-probe-${api} PRIVATE nvenc_probe_run=nvenc_probe_${api})
  target_link_libraries(obs-nvenc-probe-${api} PRIVATE FFnvcodec::FFnvcodec)
  target_link_libraries(obs-nvenc-test PRIVATE obs-nvenc-probe-${api})
  set_target_properties(obs-nvenc-probe-${api} PROPERTIES FOLDER plugins/obs-nvenc/backends)
endforeach()

set_target_properties_obs(obs-nvenc-test PROPERTIES FOLDER plugins/obs-nvenc)
