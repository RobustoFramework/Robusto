"""Make PlatformIO's Kconfig command-line tools visible to extra scripts."""

import os
import re
import sys
from pathlib import Path

Import("env")


def prepend_path(directory):
    path_key = next((key for key in os.environ if key.upper() == "PATH"), "PATH")
    current_path = os.environ.get(path_key, "")
    entries = [entry for entry in current_path.split(os.pathsep) if entry]
    directory_text = str(directory)

    if any(Path(entry).resolve() == directory.resolve() for entry in entries if Path(entry).exists()):
        return

    os.environ[path_key] = directory_text + os.pathsep + current_path if current_path else directory_text
    env["ENV"][path_key] = os.environ[path_key]


def load_deprecated_symbols(idf_path):
    deprecated = {"BOOTLOADER_COMPILER_OPTIMIZATION_NONE"}
    rename_pattern = re.compile(r"^CONFIG_([A-Za-z0-9_]+)\s+CONFIG_[A-Za-z0-9_]+\b")

    for rename_file in [idf_path / "sdkconfig.rename", *idf_path.glob("components/**/sdkconfig.rename*")]:
        if not rename_file.is_file():
            continue
        for line in rename_file.read_text(encoding="utf-8", errors="replace").splitlines():
            match = rename_pattern.match(line)
            if match:
                deprecated.add(match.group(1))

    return deprecated


def sanitize_sdkconfig(sdkconfig_file, idf_path):
    if not sdkconfig_file.is_file() or not idf_path.is_dir():
        return

    deprecated_symbols = load_deprecated_symbols(idf_path)
    deprecated_line = re.compile(
        r"^(?:#\s*)?CONFIG_(?:{0})(?:=.*|\s+is not set)\s*$".format(
            "|".join(re.escape(symbol) for symbol in sorted(deprecated_symbols))
        )
    )
    stale_choice_lines = {
        "# CONFIG_APPTRACE_DEST_JTAG is not set",
    }

    lines = sdkconfig_file.read_text(encoding="utf-8", errors="replace").splitlines()
    sanitized = []
    removed = 0
    in_deprecated_block = False

    for line in lines:
        if line.strip() == "# Deprecated options for backward compatibility":
            in_deprecated_block = True
        if in_deprecated_block:
            removed += 1
            continue
        if line.strip() in stale_choice_lines or deprecated_line.match(line):
            removed += 1
            continue
        sanitized.append(line)

    if removed:
        sdkconfig_file.write_text("\n".join(sanitized) + "\n", encoding="utf-8")
        print("Sanitized {0}: removed {1} stale Kconfig lines".format(sdkconfig_file.name, removed))


scripts_dir = Path(sys.executable).resolve().parent
if (scripts_dir / "genconfig.exe").is_file() or (scripts_dir / "genconfig").is_file():
    prepend_path(scripts_dir)
    print("Kconfig tools path: {0}".format(scripts_dir))

project_dir = Path(env.subst("$PROJECT_DIR"))
pio_env = env.subst("$PIOENV")
idf_path = Path(os.environ.get("IDF_PATH", str(Path.home() / ".platformio" / "packages" / "framework-espidf")))
sdkconfig_file = project_dir / "sdkconfig.{0}".format(pio_env)


def sanitize_current_sdkconfig(source=None, target=None, env=None):
    sanitize_sdkconfig(sdkconfig_file, idf_path)


sanitize_current_sdkconfig()

for build_target in (
    "$BUILD_DIR/${PROGNAME}.elf",
    "$BUILD_DIR/${PROGNAME}.bin",
    "$BUILD_DIR/program",
):
    env.AddPostAction(build_target, sanitize_current_sdkconfig)