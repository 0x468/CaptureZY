add_executable(CaptureZY WIN32 main.cpp)

capturezy_apply_defaults(CaptureZY)

target_link_libraries(
  CaptureZY
  PRIVATE
    user32
    gdi32
)
