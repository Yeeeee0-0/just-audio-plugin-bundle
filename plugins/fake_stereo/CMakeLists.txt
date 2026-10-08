if(APPLE)
    set(stereo_ui EditorMac.mm)
    set(stereo_libraries "-framework Cocoa")
else()
    set(stereo_ui EditorWindows.cpp)
    set(stereo_libraries user32 gdi32 comctl32)
endif()
just_add_plugin(fake_stereo 8 SOURCES Module.cpp ${stereo_ui} LIBRARIES ${stereo_libraries})
just_add_module_test(fake_stereo_core_tests SOURCES tests/CoreTests.cpp)
just_add_module_test(fake_stereo_parameter_tests SOURCES tests/ParameterTests.cpp)
if(APPLE)
    add_executable(fake_stereo_host_tests tests/HostTests.mm
        "${JUST_VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm"
        "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp")
    target_link_libraries(fake_stereo_host_tests PRIVATE just_common sdk_hosting "-framework Cocoa")
    target_compile_options(fake_stereo_host_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    get_target_property(stereo_bundle Just_fake_stereo SMTG_PLUGIN_PACKAGE_PATH)
    # The generic helper has no command-argument slot; the loaded host needs one.
    add_test(NAME fake-stereo-loaded-host COMMAND fake_stereo_host_tests "${stereo_bundle}")
endif()

if(APPLE)
    add_executable(fake_stereo_feedback_host tests/FeedbackHostMac.mm
        "${JUST_VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm"
        "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp")
    target_link_libraries(fake_stereo_feedback_host PRIVATE just_common sdk_hosting "-framework Cocoa")
    target_compile_options(fake_stereo_feedback_host PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_compile_definitions(fake_stereo_feedback_host PRIVATE JUST_FEEDBACK_INDEX=8)
    add_dependencies(fake_stereo_feedback_host Just_fake_stereo)
endif()

if(APPLE)
    just_add_module_test(fake_stereo_analysis_tests SOURCES tests/AnalysisTests.cpp Module.cpp EditorMac.mm ../../common/ui/ControlsMac.mm ../../common/ui/AnalysisViewMac.mm LIBRARIES "-framework Cocoa")
    target_compile_options(fake_stereo_analysis_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_compile_definitions(fake_stereo_analysis_tests PRIVATE JUST_FEEDBACK_INDEX=8)
endif()

if(APPLE)
    add_executable(fake_stereo_field_host tests/FieldGeometryHostMac.mm
        "${JUST_VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm"
        "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp")
    target_link_libraries(fake_stereo_field_host PRIVATE just_common sdk_hosting "-framework Cocoa")
    target_compile_options(fake_stereo_field_host PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_compile_definitions(fake_stereo_field_host PRIVATE JUST_FEEDBACK_INDEX=8)
    add_dependencies(fake_stereo_field_host Just_fake_stereo)
endif()

just_add_module_test(fake_stereo_field_model_tests SOURCES tests/FieldModelTests.cpp)
just_add_module_test(fake_stereo_field_transform_tests SOURCES tests/FieldTransformTests.cpp)

if(APPLE)
    add_executable(fake_stereo_interaction_host MACOSX_BUNDLE tests/InteractionHostMac.mm Module.cpp EditorMac.mm ../../common/ui/ControlsMac.mm)
    target_link_libraries(fake_stereo_interaction_host PRIVATE just_common "-framework Cocoa")
    target_compile_options(fake_stereo_interaction_host PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    set_target_properties(fake_stereo_interaction_host PROPERTIES MACOSX_BUNDLE_GUI_IDENTIFIER "audio.just.wider.interaction-qa")
    # Manual, explicitly serialized native input QA. Never part of unattended CTest.
endif()
