if(APPLE)
    set(gate_editor EditorMac.mm)
else()
    set(gate_editor EditorWindows.cpp)
endif()
just_add_plugin(gate 6 SOURCES Module.cpp GateEngine.cpp ${gate_editor})
just_add_module_test(just_gate_core_tests SOURCES tests/CoreTests.cpp GateEngine.cpp)
just_add_module_test(gate_cross_threshold_tests SOURCES tests/CrossThresholdTests.cpp GateEngine.cpp)
if(APPLE)
    just_add_module_test(just_gate_host_tests SOURCES tests/HostTestsMac.mm
        "${JUST_VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm"
        "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp"
        LIBRARIES sdk_hosting "-framework Cocoa")
    target_compile_definitions(just_gate_host_tests PRIVATE JUST_PLUGIN_INDEX=6)
    get_target_property(gate_bundle Just_gate SMTG_PLUGIN_PACKAGE_PATH)
    target_compile_definitions(just_gate_host_tests PRIVATE JUST_GATE_BUNDLE_PATH="${gate_bundle}")
    target_compile_options(just_gate_host_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    add_dependencies(just_gate_host_tests Just_gate)
endif()

if(APPLE)
    add_executable(gate_feedback_host tests/FeedbackHostMac.mm
        "${JUST_VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm"
        "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp")
    target_link_libraries(gate_feedback_host PRIVATE just_common sdk_hosting "-framework Cocoa")
    target_compile_options(gate_feedback_host PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_compile_definitions(gate_feedback_host PRIVATE JUST_FEEDBACK_INDEX=6)
    add_dependencies(gate_feedback_host Just_gate)
endif()

if(APPLE)
    just_add_module_test(gate_analysis_tests SOURCES tests/AnalysisTests.cpp Module.cpp GateEngine.cpp EditorMac.mm ../../common/ui/ControlsMac.mm ../../common/ui/AnalysisViewMac.mm LIBRARIES "-framework Cocoa")
    target_compile_options(gate_analysis_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_compile_definitions(gate_analysis_tests PRIVATE JUST_FEEDBACK_INDEX=6)
endif()
