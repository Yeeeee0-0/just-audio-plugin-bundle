"""Numerical gate for CSV rendered through the actual DistortionEngine."""
import json
import sys
import numpy as np

data = np.genfromtxt(sys.argv[1], delimiter=",", names=True)
rows = []
for model, name in ((1, "Soft"), (2, "Hard"), (4, "Fold")):
    for frequency in np.unique(data["frequency"]):
        a = data[(data["model"] == model) & (data["frequency"] == frequency)]
        count = len(a)
        bin_index = round(frequency * count / 48000)
        mask = np.ones(count // 2 + 1, bool)
        mask[0] = False
        mask[np.arange(bin_index, len(mask), bin_index)] = False
        scores = {key: float(20 * np.log10(max(1e-15, np.linalg.norm(np.fft.rfft(a[key])[mask]) * 2 / count))) for key in ("oneX", "fourX")}
        rows.append({"model": name, "frequency": float(frequency), "alias_dbFS": scores,
                     "reduction_db": scores["oneX"] - scores["fourX"],
                     "above_floor": bool(scores["oneX"] > -120)})
failed = [r for r in rows if r["above_floor"] and r["reduction_db"] < 20]
report = {"sampleRate": 48000, "inputPeak": .2, "drive_db": 12, "boost": False,
          "bias": 0, "shape_percent": 50, "factor": 4, "FFT": 4096,
          "metric": "outside DC + in-band integer harmonics; L2 FFT energy dBFS; 1x<-120dBFS unscored",
          "scored": sum(r["above_floor"] for r in rows), "below20dB": len(failed), "results": rows}
with open(sys.argv[2], "w") as output:
    json.dump(report, output, indent=2)
    output.write("\n")
for r in rows:
    print(r["model"], r["frequency"], r["reduction_db"], "scored" if r["above_floor"] else "floor/unscored")
sys.exit(bool(failed))
