add_library(
  capturezy_feature_pin
  STATIC
    pin_manager.cpp
    pin_window.cpp
)

capturezy_apply_defaults(capturezy_feature_pin)

target_include_directories(
  capturezy_feature_pin
  PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
  capturezy_feature_pin
  PUBLIC
    capturezy_feature_capture
  PRIVATE
    comdlg32
    user32
    gdi32
)
