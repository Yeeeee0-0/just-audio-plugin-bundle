# Third-party notices

## Steinberg VST3 SDK

JUST Audio Plugin is built with Steinberg VST3 SDK 3.8.1 at commit
`3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`. The exact SDK and submodule
revisions are recorded in `third_party/dependency-lock.json`.

Copyright (c) 2026, Steinberg Media Technologies GmbH.

The SDK code used by the plugins is licensed under the MIT License. Its full,
unchanged notice is included in `third_party/licenses/VST3-SDK-MIT.txt` and in
each distributed VST3 bundle at `Contents/Resources/Licenses/VST3-SDK-MIT.txt`.
Keep that notice when redistributing copies or substantial portions of the SDK
code, including plugin binaries containing it.

SDK source is obtained separately from the official upstream repository by
`scripts/bootstrap-sdk.py`; it is not vendored into this repository. The fetched
SDK retains its own license files and file-level notices. Some optional SDK
samples include separately licensed dependencies; those samples and SDK tools
are not part of the JUST plugin release assets.

- [Pinned SDK license](https://github.com/steinbergmedia/vst3sdk/blob/3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96/LICENSE.txt)
- [SDK license and trademark guidance](https://github.com/steinbergmedia/vst3sdk/blob/3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96/README.md#license--usage-guidelines)

VST is a trademark of Steinberg Media Technologies GmbH. References to the VST3
format do not imply endorsement by Steinberg.

## Algorithm references

JUST EQ implements biquad filter equations published in Robert Bristow-Johnson's
Audio EQ Cookbook, as presented in the [W3C Audio EQ Cookbook](https://www.w3.org/TR/2021/NOTE-audio-eq-cookbook-20210608/).
The implementation is project source; the referenced W3C document is not
included in the distribution.

JUST Distortion's antiderivative antialiasing work cites [Bilbao et al. (2017)](https://doi.org/10.1109/LSP.2017.2675541)
and [Holters (2019)](https://www.dafx.de/paper-archive/2019/DAFx2019_paper_4.pdf).
The implementation and its project-specific corrections are described in
`plugins/distortion/README.md`; these papers are references and are not
redistributed with the project.

JUST Reverb preset values are project-authored tuning. References to third-party
manuals describe design research and do not include those vendors' code,
factory preset files or impulse responses.

## Platform APIs and project materials

Native macOS and Windows implementations use the respective platform APIs and
system libraries. Apple and Microsoft SDKs are not included in this repository
or its release assets. No JUCE, VSTGUI, WebView2 or libebur128 code is included in
the JUST plugin sources.

This document records third-party notices. JUST project code is covered by
LICENSE; runtime artwork terms are documented in ARTWORK.md. Third-party
licenses and trademark rights remain with their respective rights holders.
