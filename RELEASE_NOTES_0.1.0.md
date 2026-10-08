# JUST 0.1.0

Original source snapshot: `b99abd3be4ad89b21e3a0c3caf1f12904503f168`. The public source preparation derives from this snapshot and starts a new repository history; the snapshot hash is provenance, not a public checkout ref.

Ten audio effects for macOS Apple silicon, released under the MIT License.

## Included effects

EQ, Reverb, Delay, Tremolo, Compressor, Limiter, Gate, Flanger, Wider and Distortion. Each is a separate VST3 plugin. Wider retains the internal slug `fake_stereo` and `Just_fake_stereo.vst3` bundle identity.

The suite includes native editors and a shared parameter, state, preset and analysis framework. See [README.md](README.md) and the module pages for effect features.

## Final changes in the 0.1.0 baseline

- All ten plugins report version 0.1.0 in processor/controller metadata, macOS bundle version fields and visible About/Settings views.
- EQ's main spectrum graph no longer shows the explanatory hover tooltip. Its selected-band floating editor remains.
- Fresh EQ instances use a right-hand spectrum scale from 0 to -120 dBFS. Older UI-state fallback defaults migrate to 120 dB; explicitly saved current-format ranges of 60, 90 or 120 dB remain as saved. The EQ gain axis is unchanged.
- These final changes preserve processor/controller UIDs, parameter IDs and sound-state mappings. They do not change the DSP, presets or the English default.

## Release assets and evidence

The release contains a selectable macOS arm64 installer, an optional manual-install VST3 ZIP, a source ZIP, installation instructions and SHA-256 checksums. See [INSTALLING.md](INSTALLING.md).

The installer has ten independent plugin choices, all selected by default, and installs for the current user. Its SHA-256 is `c1359f53d5307ff994c4d8fd1e8930e0f8a5996c3140dc31a09fea69cc4f56cc`. Native GUI checks passed for default-all, single/mixed selections, restoring all choices, current-user scope and the pre-install confirmation page. This check stopped before installation.

The public source tree rebuilt all ten arm64 Release targets and passed eight shared CLI contract tests plus 32 identity/resource audit regressions. The packaged plugin bytes remain the original frozen binaries.

The freeze record reports targeted visual/spectrum contracts, a loaded EQ analyzer fixture, ten-plugin runtime/version checks, bundle signature and architecture checks, archive equality and independent source reconstruction checks. These are recorded results for that frozen set; no new build or host acceptance is asserted by this documentation work.

The PKG is unsigned and not notarized. The frozen bundles carry ad-hoc development signatures, which do not establish Developer ID signing or notarization.

## Platform and behavior limits

- Only macOS arm64 has binaries in this release. Windows work is unaccepted; Intel macOS and universal builds are unverified.
- The macOS 11.0 deployment target is a compiler setting, not an accepted macOS 11.0 runtime test.
- The freeze record leaves actual native input and original DAW-project preservation checks for separate acceptance. Passive views and fixtures do not prove those results.
- Compressor and Gate retain lookahead requests but use zero actual lookahead/latency.
- Distortion uses fixed 4x processing with 32 samples of latency. Other saved quality requests remain pending; selectable 1x/2x/8x processing is not a released capability.
- Limiter includes sample-peak and reconstructed-peak processing; no ITU/EBU compliance claim is made. The loudness guide is a display reference.
- Project code and the authorized runtime artwork are distributed under MIT. Third-party notices remain intact; see LICENSE, ARTWORK.md and THIRD_PARTY_NOTICES.md.
