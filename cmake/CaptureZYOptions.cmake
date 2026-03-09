include(CheckIPOSupported)

function(capturezy_apply_defaults target_name)
  target_compile_features(${target_name} PRIVATE cxx_std_23)

  target_compile_definitions(
    ${target_name}
    PRIVATE
      UNICODE
      _UNICODE
      NOMINMAX
      WIN32_LEAN_AND_MEAN
  )

  if(MSVC)
    target_compile_options(
      ${target_name}
      PRIVATE
        /W4
        /permissive-
        /utf-8
        /EHsc
        /Zc:__cplusplus
    )

    if(CAPTUREZY_WARNINGS_AS_ERRORS)
      target_compile_options(${target_name} PRIVATE /WX)
    endif()
  else()
    target_compile_options(
      ${target_name}
      PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    if(CAPTUREZY_WARNINGS_AS_ERRORS)
      target_compile_options(${target_name} PRIVATE -Werror)
    endif()
  endif()

  if(CAPTUREZY_ENABLE_LTO)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output)
    if(ipo_supported)
      set_property(TARGET ${target_name} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
      message(WARNING "IPO/LTO not enabled for ${target_name}: ${ipo_output}")
    endif()
  endif()
endfunction()
