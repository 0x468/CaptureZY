add_library(
  capturezy_feature_capture
  STATIC
    capture_overlay.cpp
    screen_capture.cpp
)

capturezy_apply_defaults(capturezy_feature_capture)

target_include_directories(
  capturezy_feature_capture
  PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
  capturezy_feature_capture
  PUBLIC
    capturezy_core
  PRIVATE
    user32
    gdi32
)
