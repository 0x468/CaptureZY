add_library(
  capturezy_core
  STATIC
    app_state.cpp
    app_metadata.cpp
)

capturezy_apply_defaults(capturezy_core)

target_include_directories(
  capturezy_core
  PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)
