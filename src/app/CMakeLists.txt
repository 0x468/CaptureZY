add_executable(
  CaptureZY
  WIN32
    application.cpp
    main.cpp
)

capturezy_apply_defaults(CaptureZY)

target_include_directories(
  CaptureZY
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
  CaptureZY
  PRIVATE
    capturezy_core
    capturezy_platform_win
    capturezy_render_d2d
)
