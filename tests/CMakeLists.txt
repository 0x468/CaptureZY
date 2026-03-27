add_executable(
  capturezy_annotation_session_test
    feature_capture/annotation_session_test.cpp
    ${PROJECT_SOURCE_DIR}/src/feature_capture/capture_annotation.cpp
)

capturezy_apply_defaults(capturezy_annotation_session_test)

target_include_directories(
  capturezy_annotation_session_test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

add_test(
  NAME capturezy_annotation_session_test
  COMMAND capturezy_annotation_session_test
)
