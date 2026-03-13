add_library(
  capturezy_core
  STATIC
    crash_diagnostics.cpp
    app_settings.cpp
    app_settings_store.cpp
    app_state.cpp
    app_metadata.cpp
)

capturezy_apply_defaults(capturezy_core)

target_include_directories(
  capturezy_core
  PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
  capturezy_core
  PRIVATE
    dbghelp
    ole32
    shell32
)
