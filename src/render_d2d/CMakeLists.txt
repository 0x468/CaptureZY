add_library(
  capturezy_render_d2d
  STATIC
    render_backend.cpp
)

capturezy_apply_defaults(capturezy_render_d2d)

target_include_directories(
  capturezy_render_d2d
  PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
  capturezy_render_d2d
  PRIVATE
    d2d1
    dwrite
)
