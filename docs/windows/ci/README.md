# Historical Windows CI example

`windows-x64.yml` remains an inactive, early manual example outside `.github/workflows`. The authorized preview pipeline now lives at `.github/workflows/windows-preview.yml`. It builds on the public standard Windows runner, tests the selectable installer in isolation, and creates a Windows prerelease only after every gate passes. No paid runner, signing service, user credential, or Codex credit is required by that workflow.

It has manual dispatch only and read-only repository permissions. It obtains the official Steinberg SDK at the existing lockfile commit, verifies its source hashes, builds all ten native AMD64 VST3s, runs automated checks, then uploads the passing candidate and diagnostic logs. It never uploads local REAPER settings, authorization, presets or user projects.

The two example action commits correspond to official [checkout v4.2.2](https://github.com/actions/checkout/releases/tag/v4.2.2) and [upload-artifact v4.6.2](https://github.com/actions/upload-artifact/releases/tag/v4.6.2). The active preview also pins download-artifact v4.3.0. These are fixed inputs, not a claim that they are the latest available releases. The hosted `windows-2022` image is not immutable; each preview retains the actual MSVC/SDK/CMake environment records and run URL.

Use the exact published preview's manifest and evidence as the result of its run. Native CI success is distinct from real Windows REAPER visual/input/audio acceptance. Stable macOS `main` and `v0.1.0` are outside this preview workflow.
