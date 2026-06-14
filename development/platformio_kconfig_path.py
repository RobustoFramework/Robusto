"""Make PlatformIO's Kconfig command-line tools visible to extra scripts."""

import os
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


scripts_dir = Path(sys.executable).resolve().parent
if (scripts_dir / "genconfig.exe").is_file() or (scripts_dir / "genconfig").is_file():
    prepend_path(scripts_dir)
    print("Kconfig tools path: {0}".format(scripts_dir))