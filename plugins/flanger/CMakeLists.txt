if(APPLE)
    set(flanger_editor EditorMac.mm)
else()
    set(flanger_editor EditorWindows.cpp)
    set(flanger_ui_libraries comctl32)
endif()
just_add_plugin(flanger 7 SOURCES Module.cpp ${flanger_editor} LIBRARIES ${flanger_ui_libraries})
just_add_module_test(just_flanger_ui_model_tests SOURCES tests/UiModelTests.cpp)
just_add_module_test(just_flanger_tests SOURCES tests/FlangerTests.cpp Module.cpp ${flanger_editor} LIBRARIES ${flanger_ui_libraries})
if(APPLE)
    target_sources(just_flanger_tests PRIVATE ../../common/ui/ControlsMac.mm ../../common/ui/AnalysisViewMac.mm)
    target_compile_options(just_flanger_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_link_libraries(just_flanger_tests PRIVATE "-framework Cocoa")
endif()
if(APPLE)
    get_target_property(flanger_bundle Just_flanger SMTG_PLUGIN_PACKAGE_PATH)
    add_executable(just_flanger_editor_host
        tests/EditorHostMac.mm
        "${JUST_VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm"
        "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp"
        )
    target_link_libraries(just_flanger_editor_host PRIVATE just_common sdk_hosting "-framework Cocoa")
    target_compile_options(just_flanger_editor_host PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    add_test(NAME flanger-native-editor COMMAND just_flanger_editor_host "${flanger_bundle}")
    # Manual serial OS-input fixture. It is deliberately excluded from CTest.
    add_executable(just_flanger_native_interaction
        tests/NativeInteractionHostMac.mm
        "${JUST_VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm"
        "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp")
    target_link_libraries(just_flanger_native_interaction PRIVATE just_common sdk_hosting "-framework Cocoa")
    target_compile_options(just_flanger_native_interaction PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    set_target_properties(just_flanger_native_interaction PROPERTIES MACOSX_BUNDLE TRUE MACOSX_BUNDLE_GUI_IDENTIFIER "audio.just.flanger-native-qa")
    add_dependencies(just_flanger_native_interaction Just_flanger)
endif()

just_add_module_test(just_flanger_vst3_tests SOURCES
    tests/Vst3Tests.cpp Module.cpp ${flanger_editor}
    "${PROJECT_SOURCE_DIR}/common/vst3/Processor.cpp"
    "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp"
    LIBRARIES sdk sdk_hosting ${flanger_ui_libraries})
target_compile_definitions(just_flanger_vst3_tests PRIVATE JUST_PLUGIN_INDEX=7)
if(APPLE)
    target_sources(just_flanger_vst3_tests PRIVATE ../../common/ui/ControlsMac.mm ../../common/ui/AnalysisViewMac.mm)
    target_compile_options(just_flanger_vst3_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_link_libraries(just_flanger_vst3_tests PRIVATE "-framework Cocoa")
endif()

if(WIN32)
    foreach(target just_flanger_tests just_flanger_vst3_tests)
        target_sources(${target} PRIVATE ../../common/ui/ControlsWindows.cpp ../../common/ui/AnalysisViewWindows.cpp)
        target_link_libraries(${target} PRIVATE user32 gdi32)
    endforeach()
endif()

if(APPLE)
    add_executable(flanger_feedback_host tests/FeedbackHostMac.mm
        "${JUST_VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm"
        "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp")
    target_link_libraries(flanger_feedback_host PRIVATE just_common sdk_hosting "-framework Cocoa")
    target_compile_options(flanger_feedback_host PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_compile_definitions(flanger_feedback_host PRIVATE JUST_FEEDBACK_INDEX=7)
    add_dependencies(flanger_feedback_host Just_flanger)
endif()

if(APPLE)
    just_add_module_test(flanger_analysis_tests SOURCES tests/AnalysisTests.cpp Module.cpp EditorMac.mm ../../common/ui/ControlsMac.mm ../../common/ui/AnalysisViewMac.mm LIBRARIES "-framework Cocoa")
    target_compile_options(flanger_analysis_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_compile_definitions(flanger_analysis_tests PRIVATE JUST_FEEDBACK_INDEX=7)
endif()
