include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/headers.cmake")

# Isolate all cross-file state, entry points and encoder descriptors. Only the
# selected ABI registers encoders; existing encoder IDs and settings stay intact.
set(
  nvenc_backend_symbols
  cu
  nv
  nv_create_instance
  load_cuda_lib
  init_cuda
  cuda_get_error_desc
  cuda_error_check
  obs_cuda_load
  obs_cuda_unload
  load_nvenc_lib
  init_nvenc
  nv_fail2
  nv_failed2
  nv_error_name
  get_encoder_caps
  num_encoder_devices
  is_codec_supported
  has_broken_split_encoding
  nvenc_supported
  obs_nvenc_load
  obs_nvenc_unload
  register_encoders
  register_compat_encoders
  nvenc_encode_base
  cuda_ctx_init
  cuda_ctx_free
  cuda_init_surfaces
  cuda_free_surfaces
  cuda_encode
  cuda_opengl_free
  cuda_opengl_encode
  d3d11_init
  d3d11_free
  d3d11_init_textures
  d3d11_free_textures
  d3d11_encode
  apply_user_args
  get_user_arg_int
  nvenc_properties_read
  nvenc_properties_internal
  h264_nvenc_defaults
  hevc_nvenc_defaults
  av1_nvenc_defaults
  h264_nvenc_properties
  hevc_nvenc_properties
  av1_nvenc_properties
  h264_nvenc_info
  hevc_nvenc_info
  av1_nvenc_info
  h264_nvenc_soft_info
  hevc_nvenc_soft_info
  av1_nvenc_soft_info
  compat_h264_nvenc_info
  compat_hevc_nvenc_info
  compat_av1_nvenc_info
  compat_h264_nvenc_soft_info
  compat_hevc_nvenc_soft_info
  compat_av1_nvenc_soft_info
)

function(nvenc_isolate_backend target api)
  nvenc_target_headers(${target} ${api})
  foreach(symbol IN LISTS nvenc_backend_symbols)
    target_compile_definitions(${target} PRIVATE ${symbol}=${symbol}_${api})
  endforeach()
  set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON FOLDER plugins/obs-nvenc/backends)
endfunction()
