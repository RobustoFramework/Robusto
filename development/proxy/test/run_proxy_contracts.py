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
PROXY_DIRECTORY = ROBUSTO_DIRECTORY / "proxy"
PROXY_SOURCE_DIRECTORY = PROXY_DIRECTORY / "src"
RSD1_DIRECTORY = PROXY_DIRECTORY / "transports" / "rsd1"

COMMON_INCLUDES = (
    ROBUSTO_DIRECTORY / "include",
    PROXY_DIRECTORY / "include",
    RSD1_DIRECTORY / "include",
)

CONTRACTS = (
    (
        "robusto_proxy_step1_contract",
        (
            "robusto_proxy_crc32.c",
            "robusto_proxy_frame.c",
            "robusto_proxy_control.c",
            "robusto_proxy_pubsub.c",
            "robusto_proxy_session.c",
            "robusto_proxy_inflight.c",
            "robusto_proxy_service.c",
            "robusto_proxy_pubsub_adapter.c",
            "robusto_proxy_client.c",
            "robusto_proxy_pubsub_client.c",
        ),
        (),
    ),
    (
        "robusto_proxy_lifecycle_contract",
        ("robusto_proxy_lifecycle.c",),
        (TEST_DIRECTORY / "support",),
    ),
    (
        "robusto_rsd1_protocol_contract",
        (),
        (),
    ),
    (
        "robusto_c6_update_protocol_contract",
        (),
        (),
    ),
    (
        "robusto_c6_recovery_protocol_contract",
        (),
        (),
    ),
)


def find_compiler() -> str:
    configured = os.environ.get("CC")
    candidates = (configured,) if configured else ("cc", "gcc", "clang")
    for candidate in candidates:
        if candidate and shutil.which(candidate):
            return candidate
    raise RuntimeError("No C compiler found; set CC or install cc, gcc, or clang")


def run_contract(
    compiler: str,
    name: str,
    proxy_sources: tuple[str, ...],
    extra_includes: tuple[Path, ...],
    output_directory: Path,
) -> None:
    executable = output_directory / (name + (".exe" if os.name == "nt" else ""))
    includes = extra_includes + COMMON_INCLUDES
    command = [
        compiler,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        *(f"-I{include}" for include in includes),
        str(TEST_DIRECTORY / f"{name}.c"),
        *(str(PROXY_SOURCE_DIRECTORY / source) for source in proxy_sources),
    ]
    if not proxy_sources:
        command.extend(
            (
                str(RSD1_DIRECTORY / "src" / "robusto_rsd1_protocol.c"),
                str(PROXY_SOURCE_DIRECTORY / "robusto_proxy_crc32.c"),
            )
        )
    command.extend(("-o", str(executable)))
    subprocess.run(command, cwd=REPOSITORY_ROOT, check=True)
    subprocess.run([str(executable)], cwd=REPOSITORY_ROOT, check=True)


def main() -> int:
    try:
        compiler = find_compiler()
        with tempfile.TemporaryDirectory(prefix="robusto-proxy-contracts-") as temporary:
            output_directory = Path(temporary)
            for name, proxy_sources, extra_includes in CONTRACTS:
                run_contract(
                    compiler,
                    name,
                    proxy_sources,
                    extra_includes,
                    output_directory,
                )
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Proxy contract failure: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())