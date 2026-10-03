#!/usr/bin/env python3

from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
TEST_DIRECTORY = Path(__file__).resolve().parent
ROBUSTO_DIRECTORY = REPOSITORY_ROOT / "components" / "robusto"


def find_compiler() -> str:
    configured = os.environ.get("CC")
    candidates = (configured,) if configured else ("cc", "gcc", "clang")
    for candidate in candidates:
        if candidate and shutil.which(candidate):
            return candidate
    raise RuntimeError("No C compiler found; set CC or install cc, gcc, or clang")


def main() -> int:
    try:
        compiler = find_compiler()
        with tempfile.TemporaryDirectory(prefix="robusto-pubsub-client-") as temporary:
            executable = Path(temporary) / (
                "robusto_pubsub_client_contract.exe"
                if os.name == "nt"
                else "robusto_pubsub_client_contract"
            )
            command = [
                compiler,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-D_GNU_SOURCE",
                "-DCONFIG_ROBUSTO_PUBSUB_CLIENT=1",
                "-DCONFIG_ROB_LOG_MAXIMUM_LEVEL=0",
                f"-I{TEST_DIRECTORY / 'support'}",
                f"-I{ROBUSTO_DIRECTORY / 'include'}",
                str(TEST_DIRECTORY / "robusto_pubsub_client_contract.c"),
                str(ROBUSTO_DIRECTORY / "misc" / "src" / "pubsub" / "robusto_pubsub_client.c"),
                "-o",
                str(executable),
            ]
            subprocess.run(command, cwd=REPOSITORY_ROOT, check=True)
            subprocess.run([str(executable)], cwd=REPOSITORY_ROOT, check=True)
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(f"PubSub client contract failure: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
