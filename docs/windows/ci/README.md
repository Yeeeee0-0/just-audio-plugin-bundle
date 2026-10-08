# Inactive Windows CI candidate

`windows-x64.yml` is deliberately outside `.github/workflows`. Nothing in this task enables Actions, uploads code, publishes a release, runs remote builds, buys minutes, or creates credentials. The publication owner can review and move the file only after the repository/release route and any spending constraints are approved.

It has manual dispatch only and read-only repository permissions. It obtains the official Steinberg SDK at the existing lockfile commit, verifies its source hashes, builds all ten native AMD64 VST3s, runs automated checks, then uploads the passing candidate and diagnostic logs. It never uploads local REAPER settings, authorization, presets or user projects.

The two action commits correspond to official [checkout v4.2.2](https://github.com/actions/checkout/releases/tag/v4.2.2) and [upload-artifact v4.6.2](https://github.com/actions/upload-artifact/releases/tag/v4.6.2). These are reviewed fixed inputs, not a claim that they are the latest available releases. The hosted `windows-2022` image is not immutable; each build must retain the exact MSVC/SDK/CMake environment records. No CI run has been executed for this handoff.

Before enabling, review the repository source/license policy separately, check available Actions entitlement without buying capacity, and rerun security review of the pinned actions. Native CI success is distinct from real Windows REAPER visual/input/audio acceptance.
