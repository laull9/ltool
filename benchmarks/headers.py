"""Measure three fresh GCC/Clang syntax checks per public entry point on Linux."""
import os
from pathlib import Path
from statistics import median
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parents[1]
compiler = os.environ.get("CXX", "c++")
print("header,median_seconds,peak_MiB")
with tempfile.TemporaryDirectory() as tmp:
    source = Path(tmp) / "main.cpp"
    for header in ("", "LResult", "LString", "LFile", "LJson", "LTool"):
        include = f'#include "{header}.hpp"\n' if header else ""
        source.write_text(include + "int main() {}\n")
        seconds, peaks = [], []
        for _ in range(3):
            start = time.monotonic()
            with subprocess.Popen([compiler, "-std=c++20", "-I", str(root),
                                   "-fsyntax-only", str(source)]) as proc:
                _, status, usage = os.wait4(proc.pid, 0)
                proc.returncode = os.waitstatus_to_exitcode(status)
                if proc.returncode:
                    raise SystemExit(proc.returncode)
            seconds.append(time.monotonic() - start)
            peaks.append(usage.ru_maxrss / 1024)
        print(f"{header or 'empty'},{median(seconds):.3f},{max(peaks):.1f}", flush=True)
