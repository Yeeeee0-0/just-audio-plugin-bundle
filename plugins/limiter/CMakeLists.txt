if(APPLE)
    set(limiter_editor EditorMac.mm)
    set(limiter_libraries "-framework Cocoa")
else()
    set(limiter_editor EditorWindows.cpp)
    set(limiter_libraries comctl32)
endif()
just_add_plugin(limiter 5 SOURCES Module.cpp ${limiter_editor} LIBRARIES ${limiter_libraries})
just_add_module_test(just_limiter_tests SOURCES tests/DspTests.cpp)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME just_limiter_parameter_golden COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_parameters.py")
endif()
if(APPLE)
    add_executable(just_limiter_host tests/HostMac.mm
        "${JUST_VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm"
        "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp")
    target_link_libraries(just_limiter_host PRIVATE just_common sdk_hosting "-framework Cocoa")
    target_compile_definitions(just_limiter_host PRIVATE JUST_PLUGIN_INDEX=5)
    target_compile_options(just_limiter_host PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    get_target_property(limiter_bundle Just_limiter SMTG_PLUGIN_PACKAGE_PATH)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ui-captures")
    # Native acceptance is a manual, serialized OS-input run. The host requires
    # --serial-native-qa explicitly; --offscreen records pixels only.
    # Never register the interactive host in unattended CTest.
endif()

just_add_module_test(just_limiter_measurement_tests SOURCES tests/MeasurementTests.cpp)
just_add_module_test(just_limiter_legacy_state_tests SOURCES tests/LegacyStateTests.cpp)
just_add_module_test(just_limiter_transparency_tests SOURCES tests/TransparencyTests.cpp)
just_add_module_test(just_limiter_output_gain_tests SOURCES tests/OutputGainTests.cpp)

if(APPLE)
    add_executable(just_limiter_runtime_tests tests/RuntimeIntegrationTests.cpp Module.cpp EditorMac.mm
        "${PROJECT_SOURCE_DIR}/common/vst3/Processor.cpp" "${PROJECT_SOURCE_DIR}/common/vst3/Controller.cpp"
        "${PROJECT_SOURCE_DIR}/common/ui/Editor.cpp" "${PROJECT_SOURCE_DIR}/common/ui/NativeEditorMac.mm"
        "${PROJECT_SOURCE_DIR}/common/ui/ControlsMac.mm" "${PROJECT_SOURCE_DIR}/common/ui/AnalysisViewMac.mm"
        "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp")
    target_compile_definitions(just_limiter_runtime_tests PRIVATE JUST_PLUGIN_INDEX=5 JUST_TEST_LIMITER=1)
    target_compile_options(just_limiter_runtime_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_link_libraries(just_limiter_runtime_tests PRIVATE just_common sdk sdk_hosting "-framework Cocoa")
    add_test(NAME just_limiter_runtime_tests COMMAND just_limiter_runtime_tests)
    get_target_property(transaction_sources just_limiter_runtime_tests SOURCES)
    list(FILTER transaction_sources EXCLUDE REGEX "RuntimeIntegrationTests.cpp$")
    add_executable(just_limiter_state_transaction_tests tests/StateTransactionTests.cpp ${transaction_sources})
    target_compile_definitions(just_limiter_state_transaction_tests PRIVATE JUST_PLUGIN_INDEX=5)
    target_compile_options(just_limiter_state_transaction_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_link_libraries(just_limiter_state_transaction_tests PRIVATE just_common sdk sdk_hosting "-framework Cocoa")
    add_test(NAME just_limiter_state_transaction_tests COMMAND just_limiter_state_transaction_tests)
endif()

# Core tests supplement the actual-bundle and version-routing tests.
just_add_module_test(just_limiter_transparent_engine_tests SOURCES tests/TransparentEngineTests.cpp)
just_add_module_test(just_limiter_transparent_realtime_tests SOURCES tests/TransparentRealtimeTests.cpp)
just_add_module_test(just_limiter_transparent_boundary_tests SOURCES tests/TransparentBoundaryTests.cpp)
if(APPLE)
    add_executable(just_limiter_loaded_bundle_tests tests/LoadedBundleTests.cpp
        "${JUST_VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm"
        "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp")
    target_link_libraries(just_limiter_loaded_bundle_tests PRIVATE just_common sdk_hosting "-framework Cocoa")
    target_compile_options(just_limiter_loaded_bundle_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    # Explicit paths required; never assume an installed bundle is this candidate.
endif()
just_add_module_test(just_limiter_algorithm_version_tests SOURCES tests/AlgorithmVersionTests.cpp)
if(APPLE)
    add_executable(just_limiter_loaded_performance_tests tests/LoadedPerformanceTests.cpp
        "${JUST_VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm"
        "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp")
    target_link_libraries(just_limiter_loaded_performance_tests PRIVATE just_common sdk_hosting "-framework Cocoa")
    target_compile_options(just_limiter_loaded_performance_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    # Explicit serialized headless timing, never unattended/native CTest.
endif()
