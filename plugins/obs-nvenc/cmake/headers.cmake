include_guard(GLOBAL)

# Keep each ABI in its own translation units. In particular, 12.1 config and
# picture structures cannot be passed to the driver using 13.0 layouts.
function(nvenc_target_headers target api)
  if(api STREQUAL "12_1")
    set(tag n12.1.14.0)
    set(hash dbdaa8bcbc2b9325bd24bc68b36170a8432411518040e8d4c8421608cf217425)
    set(version 0xc1)
  elseif(api STREQUAL "12_2")
    set(tag n12.2.72.0)
    set(hash 4677a397e3ec5300a6b38bf49cba42bb63a922ab26f24bc63a05ed08857cba16)
    set(version 0xc2)
  elseif(api STREQUAL "13_0")
    set(tag n13.0.19.0)
    set(hash 4fe4094541ef0f8a13249d97a8692dc5f835a6e9dd42eeadb3e2f7321d54dc7e)
    set(version 0xd0)
  else()
    message(FATAL_ERROR "Unsupported NVENC backend: ${api}")
  endif()

  set(include_dir "${CMAKE_BINARY_DIR}/_deps/nvenc-api-${api}/include")
  set(header "${include_dir}/ffnvcodec/nvEncodeAPI.h")
  set(cached_hash "")
  if(EXISTS "${header}")
    file(SHA256 "${header}" cached_hash)
  endif()
  if(NOT cached_hash STREQUAL hash)
    file(MAKE_DIRECTORY "${include_dir}/ffnvcodec")
    file(
      DOWNLOAD
      "https://raw.githubusercontent.com/FFmpeg/nv-codec-headers/${tag}/include/ffnvcodec/nvEncodeAPI.h"
      "${header}"
      EXPECTED_HASH SHA256=${hash}
      TLS_VERIFY ON
      TIMEOUT 120
    )
  endif()

  target_include_directories(${target} BEFORE PRIVATE "${include_dir}")
  target_compile_definitions(${target} PRIVATE OBS_NVENC_EXPECTED_VERSION=${version})
endfunction()
