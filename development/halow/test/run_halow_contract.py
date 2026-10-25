from pathlib import Path
from collections import Counter
import os
import shlex
import shutil
import subprocess
import tempfile


def main():
    tests = Path(__file__).resolve().parent
    root = tests.parents[2]
    component = root / "components" / "robusto"
    system = component / "base" / "src" / "system"
    configured = os.environ.get("CC")
    compiler = shlex.split(configured, posix=os.name != "nt") if configured else []
    if not compiler:
        compiler = next(([candidate] for candidate in ("cc", "gcc", "clang")
                         if shutil.which(candidate)), [])
    if not compiler:
        raise RuntimeError("No native C compiler found; set CC (e.g. python -m ziglang cc)")
    with tempfile.TemporaryDirectory(prefix="robusto-halow-") as temporary:
        contracts = (
            ("halow_contract", "halow_contract", [], component / "network" / "src" / "media" / "halow" / "halow.c"),
            ("halow_reset_contract", "halow_contract", ["-DROBUSTO_HALOW_TEST_WITH_RESET"],
             component / "network" / "src" / "media" / "halow" / "halow.c"),
            ("halow_spi_binding_contract", "halow_contract",
             ["-DROBUSTO_HALOW_TEST_WITH_RESET", "-DCONFIG_ROBUSTO_HALOW_SPI"],
             component / "network" / "src" / "media" / "halow" / "halow.c"),
            ("halow_spi_conflict_contract", "halow_contract",
             ["-DROBUSTO_HALOW_TEST_WITH_RESET", "-DCONFIG_ROBUSTO_HALOW_SPI",
              "-DROBUSTO_HALOW_TEST_SPI_CONFLICT"],
             component / "network" / "src" / "media" / "halow" / "halow.c"),
            ("halow_fragment_contract", "halow_fragment_contract", [],
             component / "network" / "src" / "message" / "robusto_message_fragment.c"),
        )
        for name, contract, defines, source in contracts:
            executable = Path(temporary) / (name + (".exe" if os.name == "nt" else ""))
            # Keep pre-existing native-format/signedness warnings visible, but nonfatal.
            legacy_flags = ["-Wno-error=format", "-Wno-error=sign-compare"] if name == "halow_fragment_contract" else []
            compilation = subprocess.run([
                *compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-parameter", *legacy_flags, *defines,
                f"-I{tests / 'support'}", f"-I{component / 'include'}",
                f"-I{system}", str(tests / f"{contract}.c"), str(source),
                str(system / "compat_crc32.c"),
                "-o", str(executable),
            ], capture_output=True, text=True)
            if compilation.returncode:
                print(compilation.stdout)
                print(compilation.stderr)
                compilation.check_returncode()
            warnings = Counter(line.split("warning:", 1)[1].strip()
                               for line in compilation.stderr.splitlines() if "warning:" in line)
            for warning, count in warnings.items():
                print(f"{name}: {count} compiler warning(s): {warning}", flush=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
