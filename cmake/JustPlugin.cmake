function(just_add_plugin slug index)
    if(NOT slug MATCHES "^[a-z][a-z0-9_]*$")
        message(FATAL_ERROR "Plugin slug must form a unique Objective-C namespace identifier")
    endif()
    cmake_parse_arguments(MODULE "" "" "SOURCES;LIBRARIES" ${ARGN})
    if(NOT MODULE_SOURCES)
        message(FATAL_ERROR "plugins/${slug} must provide a Module.cpp implementation")
    endif()
    set(target Just_${slug})
    if(APPLE)
        set(platform_ui "${PROJECT_SOURCE_DIR}/common/ui/NativeEditorMac.mm" "${PROJECT_SOURCE_DIR}/common/ui/ControlsMac.mm" "${PROJECT_SOURCE_DIR}/common/ui/AnalysisViewMac.mm")
        set(platform_entry "${JUST_VST3_SDK}/public.sdk/source/main/macmain.cpp")
    else()
        set(platform_ui "${PROJECT_SOURCE_DIR}/common/ui/NativeEditorWindows.cpp" "${PROJECT_SOURCE_DIR}/common/ui/ControlsWindows.cpp" "${PROJECT_SOURCE_DIR}/common/ui/AnalysisViewWindows.cpp")
        set(platform_entry "${JUST_VST3_SDK}/public.sdk/source/main/dllmain.cpp")
    endif()
    smtg_add_vst3plugin(${target}
        "${PROJECT_SOURCE_DIR}/common/vst3/Processor.cpp"
        "${PROJECT_SOURCE_DIR}/common/vst3/Controller.cpp"
        "${PROJECT_SOURCE_DIR}/common/vst3/Factory.cpp"
        "${PROJECT_SOURCE_DIR}/common/ui/Editor.cpp"
        ${platform_ui} ${platform_entry} ${MODULE_SOURCES})
    target_compile_definitions(${target} PRIVATE JUST_PLUGIN_INDEX=${index})
    target_link_libraries(${target} PRIVATE just_common sdk ${MODULE_LIBRARIES})
    smtg_target_set_bundle(${target} BUNDLE_IDENTIFIER "audio.just.${slug}.vst3" COMPANY_NAME "JUST")
    if(APPLE)
        target_compile_definitions(${target} PRIVATE JUST_OBJC_NAMESPACE=Just_${slug})
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
        target_link_libraries(${target} PRIVATE "-framework Cocoa")
        set_target_properties(${target} PROPERTIES
            MACOSX_BUNDLE_GUI_IDENTIFIER "audio.just.${slug}.vst3"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_INFO_STRING "${PROJECT_VERSION}")
        if(slug STREQUAL "fake_stereo")
            # Presentation only: retain the old target, executable, bundle path and ID.
            set_target_properties(${target} PROPERTIES
                MACOSX_BUNDLE_BUNDLE_NAME "JUST Wider"
                MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/cmake/WiderInfo.plist.in"
                XCODE_ATTRIBUTE_INFOPLIST_KEY_CFBundleName "JUST Wider"
                XCODE_ATTRIBUTE_INFOPLIST_KEY_CFBundleDisplayName "JUST Wider")
        endif()
    else()
        target_link_libraries(${target} PRIVATE user32 gdi32 comctl32)
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/../Resources/Licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/common/ui/resources" "$<TARGET_FILE_DIR:${target}>/../Resources/JustUI"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/third_party/licenses/VST3-SDK-MIT.txt" "$<TARGET_FILE_DIR:${target}>/../Resources/Licenses/VST3-SDK-MIT.txt")
    if(APPLE)
        # Resources must be present before the final development ad-hoc signature.
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND codesign --force --sign - "$<TARGET_FILE_DIR:${target}>/../..")
    endif()
endfunction()

# Module-owned tests can register from plugins/<slug>/CMakeLists.txt without
# editing root CTest. Include Module.cpp only once if the test uses that factory.
function(just_add_module_test target)
    cmake_parse_arguments(TEST "" "" "SOURCES;LIBRARIES" ${ARGN})
    add_executable(${target} ${TEST_SOURCES})
    target_link_libraries(${target} PRIVATE just_common ${TEST_LIBRARIES})
    add_test(NAME ${target} COMMAND ${target})
endfunction()
