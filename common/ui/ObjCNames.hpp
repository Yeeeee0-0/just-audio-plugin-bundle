#pragma once
// Objective-C's class registry is process-global, regardless of C++ namespaces
// or Mach-O symbol visibility. Product targets supply a unique identifier token.
// Standalone test executables deliberately use a different namespace.
#ifndef JUST_OBJC_NAMESPACE
#define JUST_OBJC_NAMESPACE JustStandalone
#endif
#define JUST_OBJC_JOIN_IMPL(prefix,name) prefix##_##name
#define JUST_OBJC_JOIN(prefix,name) JUST_OBJC_JOIN_IMPL(prefix,name)
#define JUST_OBJC_CLASS(name) JUST_OBJC_JOIN(JUST_OBJC_NAMESPACE,name)
#define JUST_OBJC_STRING_IMPL(name) #name
#define JUST_OBJC_STRING(name) JUST_OBJC_STRING_IMPL(name)
#define JUST_OBJC_CLASS_NAME(name) JUST_OBJC_STRING(JUST_OBJC_CLASS(name))
namespace just {
// Stable view locators, not globally registered classes. These work across
// bundles, direct test fixtures and target-specific private runtime names.
inline constexpr const char* foundationViewIdentifier="just.common.foundation";
inline constexpr const char* rotaryViewIdentifier="just.common.rotary";
inline constexpr const char* analysisViewIdentifier="just.common.analysis";
}
