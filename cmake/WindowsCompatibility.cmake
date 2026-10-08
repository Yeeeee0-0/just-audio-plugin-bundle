# Header-only build adapters for the fixed SDK and frozen Mac test sources.
# They supply declarations that were previously obtained through incidental
# includes. No DSP, fixtures, SDK source, or macOS compilation is changed.
if(WIN32 AND MSVC)
    # The frozen analyzer test places three 690384-byte readers plus snapshots
    # on the main stack. MSVC's executable default is only 1 MiB. This changes
    # the test executable reserve, never a plugin DLL or its host's stack.
    target_link_options(just_extended_spectrum_tests PRIVATE /STACK:8388608)
    if(TARGET editorhost)
        target_compile_options(editorhost PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/FIobjbase.h>)
    endif()
    foreach(target just_limiter_legacy_state_tests just_limiter_transparent_boundary_tests just_reverb_probe)
        if(TARGET ${target})
            target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/FIstring>)
        endif()
    endforeach()
endif()
