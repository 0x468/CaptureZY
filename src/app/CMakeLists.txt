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

target_compile_definitions(
  CaptureZY
  PRIVATE
    $<$<OR:$<CONFIG:Debug>,$<BOOL:${CAPTUREZY_FORCE_DIAGNOSTIC_SELF_TEST}>>:CAPTUREZY_ENABLE_DIAGNOSTIC_SELF_TEST>
)

target_link_options(
  CaptureZY
  PRIVATE
    "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'"
)

target_link_libraries(
  CaptureZY
  PRIVATE
    capturezy_core
    capturezy_platform_win
    capturezy_render_d2d
)

install(
  TARGETS CaptureZY
  RUNTIME DESTINATION .
)
