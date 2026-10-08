if(APPLE)
    just_add_plugin(distortion 9 SOURCES Module.cpp EditorMac.mm)
    add_custom_command(TARGET Just_distortion POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/docs/reference/icons/JUST-icon-board-original.png" "$<TARGET_FILE_DIR:Just_distortion>/../Resources/JUST-icon-board-original.png"
        COMMAND codesign --force --sign - "$<TARGET_FILE_DIR:Just_distortion>/../..")
else()
    just_add_plugin(distortion 9 SOURCES Module.cpp EditorWindows.cpp LIBRARIES comctl32)
endif()
just_add_module_test(just_distortion_core_tests SOURCES tests/CoreTests.cpp)
just_add_module_test(just_distortion_preset_tests SOURCES tests/PresetTests.cpp Module.cpp)
add_executable(just_distortion_adaa_compare tests/AdaaComparison.cpp)
target_link_libraries(just_distortion_adaa_compare PRIVATE just_common)
if(APPLE)
    just_add_module_test(just_distortion_ui_tests SOURCES tests/EditorTestsMac.mm EditorMac.mm LIBRARIES "-framework Cocoa")
    target_sources(just_distortion_ui_tests PRIVATE ../../common/ui/AnalysisViewMac.mm ../../common/ui/ControlsMac.mm)
    target_compile_options(just_distortion_ui_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_compile_definitions(just_distortion_ui_tests PRIVATE JD_BOARD_PATH="${PROJECT_SOURCE_DIR}/docs/reference/icons/JUST-icon-board-original.png")
    add_executable(just_distortion_loaded_tests tests/LoadedTestsMac.mm
        "${JUST_VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm"
        "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp")
    target_link_libraries(just_distortion_loaded_tests PRIVATE just_common sdk_hosting "-framework Cocoa")
    target_compile_options(just_distortion_loaded_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    get_target_property(distortion_bundle Just_distortion SMTG_PLUGIN_PACKAGE_PATH)
    add_test(NAME distortion-loaded-vst3 COMMAND just_distortion_loaded_tests "${distortion_bundle}")
    # Replace the foundation-only zero-latency passthrough assertion with our
    # actual loaded effect/UI test. Do not modify the shared test source.
    cmake_language(DEFER DIRECTORY "${PROJECT_SOURCE_DIR}" CALL set_tests_properties editor-distortion PROPERTIES DISABLED TRUE)
endif()

if(APPLE)
    add_executable(distortion_feedback_host tests/FeedbackHostMac.mm
        "${JUST_VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm"
        "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp")
    target_link_libraries(distortion_feedback_host PRIVATE just_common sdk_hosting "-framework Cocoa")
    target_compile_options(distortion_feedback_host PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_compile_definitions(distortion_feedback_host PRIVATE JUST_FEEDBACK_INDEX=9)
    add_dependencies(distortion_feedback_host Just_distortion)
endif()

if(APPLE)
    just_add_module_test(distortion_analysis_tests SOURCES tests/AnalysisTests.cpp Module.cpp EditorMac.mm ../../common/ui/ControlsMac.mm ../../common/ui/AnalysisViewMac.mm LIBRARIES "-framework Cocoa")
    target_compile_options(distortion_analysis_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_compile_definitions(distortion_analysis_tests PRIVATE JUST_FEEDBACK_INDEX=9)
endif()
