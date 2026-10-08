#!/usr/bin/env python3
"""Verify the JUST Wider presentation-only rename against the approved baseline."""
import json
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[2]
base = "0be3ed7f65ac84bdb58abf5ed425f3f712efcb8b"
def old(path):
    return subprocess.check_output(["git", "show", base + ":" + path], cwd=root, text=True)
registry_path = "common/vst3/plugin-identities.json"
previous = json.loads(old(registry_path))
current = json.loads((root / registry_path).read_text())
assert len(previous) == len(current) == 10
for before, after in zip(previous, current):
    expected = dict(before)
    if before["slug"] == "fake_stereo":
        expected["name"] = "JUST Wider"
    assert after == expected, "Non-display identity field changed: " + before["slug"]
header = "common/vst3/PluginIdentities.hpp"
assert (root / header).read_text() == old(header).replace('"JUST Fake Stereo"', '"JUST Wider"')
factory = (root / "common/vst3/Factory.cpp").read_text()
assert factory.count("product.name,0,") == 2, "Both host class names must use the updated display name"
assert (root / "common/vst3/Factory.cpp").read_text() == old("common/vst3/Factory.cpp")
print(json.dumps({"passed": True, "baseline": base, "hostProcessorDisplay": "JUST Wider", "hostControllerDisplay": "JUST Wider", "unchanged": ["all ten processorUIDs", "all ten controllerUIDs", "slug fake_stereo", "bypass keys and IDs", "slots", "factory UID construction"], "bundleIdentity": "audio.just.fake_stereo.vst3", "executable": "Just_fake_stereo"}, indent=2))
