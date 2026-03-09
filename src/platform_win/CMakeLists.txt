add_library(
  capturezy_platform_win
  STATIC
    main_window.cpp
)

capturezy_apply_defaults(capturezy_platform_win)

target_include_directories(
  capturezy_platform_win
  PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
  capturezy_platform_win
  PUBLIC
    capturezy_core
    capturezy_feature_capture
  PRIVATE
    shell32
    user32
    gdi32
)
