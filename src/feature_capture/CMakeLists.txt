add_library(
  capturezy_feature_capture
  STATIC
    capture_file_dialog.cpp
    capture_result.cpp
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
    comdlg32
    ole32
    user32
    gdi32
    windowscodecs
)
