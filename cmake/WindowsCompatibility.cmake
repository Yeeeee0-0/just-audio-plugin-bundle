# Header-only build adapters for the fixed SDK and frozen Mac test sources.
# They supply declarations that were previously obtained through incidental
# includes. No DSP, fixtures, SDK source, or macOS compilation is changed.
if(WIN32 AND MSVC)
    if(TARGET editorhost)
        target_compile_options(editorhost PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/FIobjbase.h>)
    endif()
    foreach(target just_limiter_legacy_state_tests just_limiter_transparent_boundary_tests just_reverb_probe)
        if(TARGET ${target})
            target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/FIstring>)
        endif()
    endforeach()
endif()
