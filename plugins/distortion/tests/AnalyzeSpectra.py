"""Offline experiment analysis; numpy is optional tooling, never an audio dependency."""
import json
import sys
import numpy as np

data = np.genfromtxt(sys.argv[1], delimiter=",", names=True)
rows = []
for fs in np.unique(data["sampleRate"]):
    for shape in (0, 50, 100):
        for drive in (12, 24, 36):
            for frequency in np.unique(data[data["sampleRate"] == fs]["frequency"]):
                samples = data[(data["sampleRate"] == fs) & (data["softness"] == shape)
                               & (data["drive"] == drive) & (data["frequency"] == frequency)]
                count = len(samples)
                bin_index = round(frequency * count / fs)
                alias = np.ones(count // 2 + 1, dtype=bool)
                alias[0] = False
                alias[np.arange(bin_index, len(alias), bin_index)] = False
                scores = {field: float(20 * np.log10(max(1e-15,
                          np.linalg.norm(np.fft.rfft(samples[field])[alias]) * 2 / count)))
                          for field in ("oneX", "fourX", "adaa", "compensated", "equalized", "eightX")}
                rows.append({"Fs": float(fs), "softness": shape, "drive": drive,
                             "frequency": float(frequency), "alias_dbFS": scores,
                             "reduction_db": {field: scores["oneX"] - scores[field]
                                              for field in scores if field != "oneX"},
                             "comparison_above_floor": bool(scores["oneX"] > -120)})
applicable = [r for r in rows if r["comparison_above_floor"]]
failed = [r for r in applicable if r["reduction_db"]["equalized"] < 20]
report = {"status": "experimental-not-approved-for-integration", "FFT": 4096,
          "window": "none; coherent bins; discard6periods; align32samples",
          "metric": "L2 FFT residual outside DC/in-band integer harmonics, absolute dBFS; -120dBFS1x floor is unscored",
          "inputPeak": .2, "quality": "fixed4x; FIR129; Hard ADAA+half-sample average+5tap correction; decimation phase3",
          "cases": len(rows), "casesAboveFloor": len(applicable), "below20dB": len(failed), "results": rows}
with open(sys.argv[2], "w") as output:
    json.dump(report, output, indent=2)
    output.write("\n")
for row in rows:
    if row["Fs"] == 48000 and row["softness"] == 50 and row["drive"] == 12:
        print(row["frequency"], "old4x", row["reduction_db"]["fourX"],
              "candidate4x", row["reduction_db"]["equalized"], "alias", row["alias_dbFS"]["equalized"])
print("experimental above-floor cases:", len(applicable), "under20dB:", len(failed))
sys.exit(bool(failed))
