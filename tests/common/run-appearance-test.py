#!/usr/bin/env python3
"""Run only our built QA app through LaunchServices and check its own result."""
import pathlib, subprocess, sys, tempfile

app, output = (pathlib.Path(p).resolve() for p in sys.argv[1:3])
with tempfile.TemporaryDirectory(prefix="appearance-run-", dir=output) as folder:
    report = pathlib.Path(folder) / "result.txt"
    log = pathlib.Path(folder) / "output.log"
    subprocess.run(["/usr/bin/open", "-n", "-W", "--stdout", str(log), "--stderr", str(log),
                    str(app), "--args", str(output), str(report)], check=True, timeout=30)
    if log.exists():
        print(log.read_text(), end="")
    if not report.exists() or report.read_text().strip() != "PASS":
        sys.exit("Activated-window app did not report a pass")
