just_add_plugin(eq 0 SOURCES Module.cpp)
target_compile_definitions(Just_eq PRIVATE JUST_EQ_NATIVE_EDITOR=1)
if(APPLE)
    target_sources(Just_eq PRIVATE EditorMac.mm)
    target_link_libraries(Just_eq PRIVATE "-framework QuartzCore")
else()
    target_sources(Just_eq PRIVATE EditorWindows.cpp)
endif()
just_add_module_test(just_eq_tests SOURCES Tests.cpp Module.cpp)
target_compile_definitions(just_eq_tests PRIVATE JUST_EQ_TEST_OBSERVER=1)
just_add_module_test(just_eq_enum_tests SOURCES EnumGoldenTests.cpp)
just_add_module_test(just_eq_vst3_tests SOURCES Vst3Tests.cpp Module.cpp "${PROJECT_SOURCE_DIR}/common/vst3/Processor.cpp" "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp" LIBRARIES sdk sdk_hosting)
target_compile_definitions(just_eq_vst3_tests PRIVATE JUST_PLUGIN_INDEX=0)
if(APPLE)
    target_link_libraries(just_eq_vst3_tests PRIVATE "-framework CoreFoundation")
    add_executable(just_eq_editor_tests EditorTestsMac.mm "${JUST_VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm" "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp")
    target_link_libraries(just_eq_editor_tests PRIVATE just_common sdk_hosting "-framework Cocoa" "-framework QuartzCore")
    target_compile_options(just_eq_editor_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    add_dependencies(just_eq_editor_tests Just_eq)
    get_target_property(eq_bundle Just_eq SMTG_PLUGIN_PACKAGE_PATH)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ui-captures")
    add_test(NAME just_eq_editor_tests COMMAND just_eq_editor_tests "${eq_bundle}" "${CMAKE_CURRENT_BINARY_DIR}/ui-captures")
endif()

just_add_module_test(just_eq_audition_tests SOURCES AuditionTests.cpp)

if(APPLE)
    add_executable(just_eq_runtime_tests RuntimeIntegrationTests.cpp Module.cpp
        "${PROJECT_SOURCE_DIR}/common/vst3/Processor.cpp" "${PROJECT_SOURCE_DIR}/common/vst3/Controller.cpp"
        "${PROJECT_SOURCE_DIR}/common/ui/Editor.cpp" "${PROJECT_SOURCE_DIR}/common/ui/NativeEditorMac.mm"
        "${PROJECT_SOURCE_DIR}/common/ui/ControlsMac.mm" "${PROJECT_SOURCE_DIR}/common/ui/AnalysisViewMac.mm"
        "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp")
    target_compile_definitions(just_eq_runtime_tests PRIVATE JUST_PLUGIN_INDEX=0 )
    target_compile_options(just_eq_runtime_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_link_libraries(just_eq_runtime_tests PRIVATE just_common sdk sdk_hosting "-framework Cocoa")
    add_test(NAME just_eq_runtime_tests COMMAND just_eq_runtime_tests)
    get_target_property(transaction_sources just_eq_runtime_tests SOURCES)
    list(FILTER transaction_sources EXCLUDE REGEX "RuntimeIntegrationTests.cpp$")
    add_executable(just_eq_state_transaction_tests StateTransactionTests.cpp ${transaction_sources})
    target_compile_definitions(just_eq_state_transaction_tests PRIVATE JUST_PLUGIN_INDEX=0)
    target_compile_options(just_eq_state_transaction_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_link_libraries(just_eq_state_transaction_tests PRIVATE just_common sdk sdk_hosting "-framework Cocoa")
    add_test(NAME just_eq_state_transaction_tests COMMAND just_eq_state_transaction_tests)
endif()

# Manual native input QA is launched only by the serial QA owner, not CTest.
if(APPLE)
    add_executable(just_eq_native_qa NativeQAHostMac.mm
        "${JUST_VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm"
        "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp")
    target_link_libraries(just_eq_native_qa PRIVATE just_common sdk_hosting "-framework Cocoa" "-framework QuartzCore")
    target_compile_options(just_eq_native_qa PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    add_dependencies(just_eq_native_qa Just_eq)
endif()
just_add_module_test(just_eq_slope_extension_tests SOURCES SlopeExtensionTests.cpp)
just_add_module_test(just_eq_panel_presentation_tests SOURCES PanelPresentationTests.cpp)
just_add_module_test(just_eq_visual_pause_tests SOURCES VisualPauseTests.cpp)
just_add_module_test(just_eq_spectrum_tests SOURCES SpectrumTests.cpp)

if(APPLE)
    add_executable(just_eq_layout_tests LayoutTestsMac.mm
        "${JUST_VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm"
        "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp")
    target_link_libraries(just_eq_layout_tests PRIVATE just_common sdk_hosting "-framework Cocoa")
    target_compile_options(just_eq_layout_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    add_dependencies(just_eq_layout_tests Just_eq)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/layout-captures")
    add_test(NAME just_eq_layout_tests COMMAND just_eq_layout_tests "${eq_bundle}" "${CMAKE_CURRENT_BINARY_DIR}/layout-captures")
    add_executable(just_eq_analyzer_view_tests AnalyzerViewTestsMac.mm
        "${JUST_VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm"
        "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp")
    target_link_libraries(just_eq_analyzer_view_tests PRIVATE just_common sdk_hosting "-framework Cocoa")
    target_compile_options(just_eq_analyzer_view_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    add_dependencies(just_eq_analyzer_view_tests Just_eq)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/analyzer-captures")
    add_test(NAME just_eq_analyzer_view_tests COMMAND just_eq_analyzer_view_tests "${eq_bundle}" "${CMAKE_CURRENT_BINARY_DIR}/analyzer-captures")
endif()
