if(APPLE)
    set(tremolo_editor EditorMac.mm)
else()
    set(tremolo_editor EditorWindows.cpp)
endif()
just_add_plugin(tremolo 3 SOURCES Module.cpp ${tremolo_editor})
if(WIN32)
    target_link_libraries(Just_tremolo PRIVATE comctl32)
endif()
just_add_module_test(tremolo_tests SOURCES tests/TremoloTests.cpp)
if(APPLE)
    just_add_module_test(tremolo_editor_tests SOURCES tests/EditorTestsMac.mm EditorMac.mm LIBRARIES "-framework Cocoa")
    target_sources(tremolo_editor_tests PRIVATE ../../common/ui/ControlsMac.mm)
    target_compile_options(tremolo_editor_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    # Development-only host test; this is never a standalone product target.
    add_executable(tremolo_vst3_host_tests tests/Vst3HostTestsMac.mm
        "${JUST_VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm"
        "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp")
    target_link_libraries(tremolo_vst3_host_tests PRIVATE just_common sdk_hosting "-framework Cocoa")
    target_compile_options(tremolo_vst3_host_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    add_dependencies(tremolo_vst3_host_tests Just_tremolo)
    get_target_property(tremolo_bundle Just_tremolo SMTG_PLUGIN_PACKAGE_PATH)
    add_test(NAME tremolo_vst3_host_tests COMMAND tremolo_vst3_host_tests "${tremolo_bundle}")
endif()

# Built only on request by the serial QA owner; no CTest launch/installation.
if(APPLE)
    add_executable(tremolo_native_input_host EXCLUDE_FROM_ALL tests/NativeInputHostMac.mm
        "${JUST_VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm"
        "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp")
    target_link_libraries(tremolo_native_input_host PRIVATE just_common sdk_hosting "-framework Cocoa")
    target_compile_options(tremolo_native_input_host PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    set_target_properties(tremolo_native_input_host PROPERTIES MACOSX_BUNDLE TRUE)
    add_dependencies(tremolo_native_input_host Just_tremolo)
endif()

if(APPLE)
    add_executable(tremolo_feedback_host tests/FeedbackHostMac.mm
        "${JUST_VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm"
        "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp")
    target_link_libraries(tremolo_feedback_host PRIVATE just_common sdk_hosting "-framework Cocoa")
    target_compile_options(tremolo_feedback_host PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_compile_definitions(tremolo_feedback_host PRIVATE JUST_FEEDBACK_INDEX=3)
    add_dependencies(tremolo_feedback_host Just_tremolo)
endif()

if(APPLE)
    just_add_module_test(tremolo_analysis_tests SOURCES tests/AnalysisTests.cpp Module.cpp EditorMac.mm ../../common/ui/ControlsMac.mm ../../common/ui/AnalysisViewMac.mm LIBRARIES "-framework Cocoa")
    target_compile_options(tremolo_analysis_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_compile_definitions(tremolo_analysis_tests PRIVATE JUST_FEEDBACK_INDEX=3)
endif()
