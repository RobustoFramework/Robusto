r"""Host contract for the production presentation wire format (v0 legacy / v1 HaLow high mask byte).

Usage (PowerShell):  $env:CC='python -m ziglang cc'; python development\halow\test\run_presentation_contract.py
Build outputs are written to a unique temporary directory and removed afterwards.
"""
from pathlib import Path
import os
import shlex
import shutil
import subprocess
import tempfile


def main():
    tests = Path(__file__).resolve().parent
    root = tests.parents[2]
    component = root / "components" / "robusto"
    configured = os.environ.get("CC")
    compiler = shlex.split(configured, posix=os.name != "nt") if configured else []
    if not compiler:
        compiler = next(([candidate] for candidate in ("cc", "gcc", "clang")
                         if shutil.which(candidate)), [])
    if not compiler:
        raise RuntimeError("No native C compiler found; set CC (e.g. python -m ziglang cc)")
    build = Path(tempfile.mkdtemp(prefix="robusto-presentation-"))
    try:
        variants = (("halow", []), ("legacy", ["-DPRESENTATION_CONTRACT_NO_HALOW"]))
        for name, defines in variants:
            executable = build / ("presentation_contract_" + name + (".exe" if os.name == "nt" else ""))
            compilation = subprocess.run([
                *compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-parameter",
                # Pre-existing ESP32 uint32_t/%lu log formats are host-only warnings; keep them visible, nonfatal.
                "-Wno-error=format", *defines,
                f"-I{tests / 'presentation_support'}", f"-I{component / 'include'}",
                f"-I{component / 'base' / 'src' / 'system'}",
                str(tests / "presentation_contract.c"),
                str(component / "network" / "src" / "peer" / "robusto_presentation.c"),
                "-o", str(executable),
            ], capture_output=True, text=True)
            if compilation.returncode:
                print(compilation.stdout)
                print(compilation.stderr)
                compilation.check_returncode()
            warnings = sorted({line.split("warning:", 1)[1].strip()
                               for line in compilation.stderr.splitlines() if "warning:" in line})
            for warning in warnings:
                print(f"{name}: compiler warning: {warning}", flush=True)
            subprocess.run([str(executable)], check=True)
    finally:
        shutil.rmtree(build)


if __name__ == "__main__":
    main()
