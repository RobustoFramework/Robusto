#!/usr/bin/env python3
"""Validate workspace Kconfig definitions against sdkconfig files.

This is a lightweight sanity checker, not a replacement for ESP-IDF's
kconfig/confgen flow. It catches stale or malformed sdkconfig values early,
especially local Robusto options that should be defined by the Kconfig files in
this repository.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable


CONFIG_RE = re.compile(r"^\s*(?:menuconfig|config)\s+([A-Za-z0-9_]+)\b")
TYPE_RE = re.compile(r"^\s*(bool|boolean|tristate|int|hex|string)\b")
DEFAULT_RE = re.compile(r"^\s*default(?:\s+(.*))?$")
SDKCONFIG_SET_RE = re.compile(r"^CONFIG_([A-Za-z0-9_]+)=(.*)$")
SDKCONFIG_UNSET_RE = re.compile(r"^# CONFIG_([A-Za-z0-9_]+) is not set$")

DEFAULT_EXCLUDED_DIRS = {
    ".git",
    ".pio",
    ".vscode",
    "cache",
    "__pycache__",
}

DEFAULT_LOCAL_PREFIXES = (
    "ROBUSTO_",
    "ROB_",
)


@dataclass
class Definition:
    name: str
    source: Path
    line: int
    type_name: str | None = None
    defaults: list[str] = field(default_factory=list)


@dataclass
class SymbolInfo:
    name: str
    type_name: str | None = None
    definitions: list[Definition] = field(default_factory=list)
    from_generated_menu: bool = False


@dataclass
class SdkconfigEntry:
    name: str
    source: Path
    line: int
    value: str | None
    unset: bool


@dataclass
class Finding:
    severity: str
    source: Path
    line: int
    message: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate sdkconfig files against workspace Kconfig definitions."
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=Path.cwd(),
        help="Repository root to scan. Defaults to the current directory.",
    )
    parser.add_argument(
        "--sdkconfig",
        action="append",
        type=Path,
        help="Specific sdkconfig file to validate. May be passed more than once.",
    )
    parser.add_argument(
        "--include-build",
        action="store_true",
        help="Also scan generated/build directories such as .pio.",
    )
    parser.add_argument(
        "--show-unknown",
        choices=("local", "all", "none"),
        default="local",
        help="Report undefined sdkconfig symbols. Default reports only local prefixes.",
    )
    parser.add_argument(
        "--local-prefix",
        action="append",
        default=list(DEFAULT_LOCAL_PREFIXES),
        help="Prefix treated as local when reporting undefined symbols. May be repeated.",
    )
    parser.add_argument(
        "--quiet-ok",
        action="store_true",
        help="Do not print per-file OK lines."
    )
    return parser.parse_args()


def normalize_root(root: Path) -> Path:
    return root.resolve()


def is_under_excluded_dir(path: Path, root: Path, include_build: bool) -> bool:
    if include_build:
        return False
    try:
        relative = path.resolve().relative_to(root)
    except ValueError:
        relative = path
    return any(part in DEFAULT_EXCLUDED_DIRS for part in relative.parts[:-1])


def display_path(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root).as_posix()
    except ValueError:
        return path.as_posix()


def discover_kconfig_files(root: Path, include_build: bool) -> list[Path]:
    files: list[Path] = []
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        if is_under_excluded_dir(path, root, include_build):
            continue
        name = path.name.lower()
        if "kconfig" in name and not name.startswith("sdkconfig"):
            files.append(path)
    return sorted(files)


def discover_sdkconfig_files(root: Path, include_build: bool) -> list[Path]:
    files: list[Path] = []
    generated_suffixes = {".cmake", ".h", ".json"}
    for pattern in ("sdkconfig*", ".config*"):
        for path in root.rglob(pattern):
            if not path.is_file():
                continue
            if is_under_excluded_dir(path, root, include_build):
                continue
            if path.suffix.lower() in generated_suffixes:
                continue
            if path.name == "sdkconfig" or path.name.startswith("sdkconfig.") or path.name.startswith(".config"):
                files.append(path)
    return sorted(files)


def merge_symbol(symbols: dict[str, SymbolInfo], definition: Definition) -> None:
    symbol = symbols.setdefault(definition.name, SymbolInfo(name=definition.name))
    symbol.definitions.append(definition)
    if definition.type_name:
        if not symbol.type_name:
            symbol.type_name = definition.type_name
        elif symbol.type_name != definition.type_name:
            symbol.type_name = "conflict"


def parse_kconfig_file(path: Path) -> list[Definition]:
    definitions: list[Definition] = []
    current: Definition | None = None

    for line_number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        config_match = CONFIG_RE.match(line)
        if config_match:
            if current:
                definitions.append(current)
            current = Definition(name=config_match.group(1), source=path, line=line_number)
            continue

        if not current:
            continue

        type_match = TYPE_RE.match(line)
        if type_match and current.type_name is None:
            type_name = type_match.group(1)
            current.type_name = "bool" if type_name == "boolean" else type_name
            continue

        default_match = DEFAULT_RE.match(line)
        if default_match:
            current.defaults.append((default_match.group(1) or "").strip())

    if current:
        definitions.append(current)
    return definitions


def load_workspace_symbols(kconfig_files: Iterable[Path]) -> dict[str, SymbolInfo]:
    symbols: dict[str, SymbolInfo] = {}
    for path in kconfig_files:
        for definition in parse_kconfig_file(path):
            merge_symbol(symbols, definition)
    return symbols


def flatten_menu_nodes(node: object) -> Iterable[dict[str, object]]:
    if isinstance(node, dict):
        yield node
        children = node.get("children")
        if isinstance(children, list):
            for child in children:
                yield from flatten_menu_nodes(child)
    elif isinstance(node, list):
        for child in node:
            yield from flatten_menu_nodes(child)


def env_name_from_sdkconfig(path: Path) -> str | None:
    name = path.name
    if not name.startswith("sdkconfig."):
        return None
    suffix = name[len("sdkconfig."):]
    if suffix in {"old", "bak"}:
        return None
    for tail in (".old", ".bak"):
        if suffix.endswith(tail):
            return suffix[: -len(tail)]
    return suffix


def load_generated_menu_symbols(root: Path, sdkconfig_file: Path) -> dict[str, SymbolInfo]:
    env_name = env_name_from_sdkconfig(sdkconfig_file)
    if not env_name:
        return {}
    menu_path = root / ".pio" / "build" / env_name / "config" / "kconfig_menus.json"
    if not menu_path.is_file():
        return {}

    try:
        data = json.loads(menu_path.read_text(encoding="utf-8", errors="replace"))
    except json.JSONDecodeError:
        return {}

    symbols: dict[str, SymbolInfo] = {}
    for node in flatten_menu_nodes(data):
        name = node.get("id") or node.get("name")
        type_name = node.get("type")
        if not isinstance(name, str) or not isinstance(type_name, str):
            continue
        if type_name not in {"bool", "tristate", "int", "hex", "string"}:
            continue
        symbols[name] = SymbolInfo(name=name, type_name=type_name, from_generated_menu=True)
    return symbols


def merge_generated_symbols(base: dict[str, SymbolInfo], generated: dict[str, SymbolInfo]) -> dict[str, SymbolInfo]:
    merged = dict(base)
    for name, generated_symbol in generated.items():
        existing = merged.get(name)
        if existing:
            if not existing.type_name:
                existing.type_name = generated_symbol.type_name
            continue
        merged[name] = generated_symbol
    return merged


def parse_sdkconfig(path: Path) -> list[SdkconfigEntry]:
    entries: list[SdkconfigEntry] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        set_match = SDKCONFIG_SET_RE.match(line)
        if set_match:
            entries.append(
                SdkconfigEntry(
                    name=set_match.group(1),
                    source=path,
                    line=line_number,
                    value=set_match.group(2),
                    unset=False,
                )
            )
            continue

        unset_match = SDKCONFIG_UNSET_RE.match(line)
        if unset_match:
            entries.append(
                SdkconfigEntry(
                    name=unset_match.group(1),
                    source=path,
                    line=line_number,
                    value=None,
                    unset=True,
                )
            )
    return entries


def is_local_symbol(name: str, prefixes: Iterable[str]) -> bool:
    return any(name.startswith(prefix) for prefix in prefixes)


def validate_value(entry: SdkconfigEntry, symbol: SymbolInfo) -> str | None:
    type_name = symbol.type_name
    value = entry.value

    if type_name in {None, "conflict"}:
        return None

    if entry.unset:
        if type_name in {"bool", "tristate"}:
            return None
        return f"{entry.name} is {type_name}, but sdkconfig stores it as '# CONFIG_{entry.name} is not set'"

    if value is None:
        return None

    if type_name == "bool":
        if value not in {"y", "n"}:
            return f"{entry.name} is bool, but value is {value!r}; expected 'y', 'n', or an unset comment"
        return None

    if type_name == "tristate":
        if value not in {"y", "m", "n"}:
            return f"{entry.name} is tristate, but value is {value!r}; expected 'y', 'm', 'n', or an unset comment"
        return None

    if type_name == "int":
        if not value:
            return f"{entry.name} is int, but value is empty"
        if not re.fullmatch(r"[+-]?\d+", value):
            return f"{entry.name} is int, but value is {value!r}"
        return None

    if type_name == "hex":
        if not value:
            return f"{entry.name} is hex, but value is empty"
        if not re.fullmatch(r"(?:0x)?[0-9a-fA-F]+", value):
            return f"{entry.name} is hex, but value is {value!r}"
        return None

    if type_name == "string":
        if value == "":
            return f"{entry.name} is string, but value is empty; expected quoted string syntax"
        if len(value) < 2 or not (value.startswith('"') and value.endswith('"')):
            return f"{entry.name} is string, but value is {value!r}; expected quoted string syntax"

    return None


def validate_definitions(symbols: dict[str, SymbolInfo]) -> list[Finding]:
    findings: list[Finding] = []
    for symbol in symbols.values():
        workspace_definitions = [definition for definition in symbol.definitions if definition.source]
        if not workspace_definitions:
            continue

        type_names = {definition.type_name for definition in workspace_definitions if definition.type_name}
        if len(type_names) > 1:
            first = workspace_definitions[0]
            findings.append(
                Finding(
                    severity="ERROR",
                    source=first.source,
                    line=first.line,
                    message=f"{symbol.name} has conflicting Kconfig types: {', '.join(sorted(type_names))}",
                )
            )

        if symbol.type_name in {"int", "hex"}:
            for definition in workspace_definitions:
                if not definition.defaults:
                    findings.append(
                        Finding(
                            severity="WARN",
                            source=definition.source,
                            line=definition.line,
                            message=f"{symbol.name} is {symbol.type_name} and has no default",
                        )
                    )
    return findings


def validate_sdkconfig(
    entries: Iterable[SdkconfigEntry],
    symbols: dict[str, SymbolInfo],
    show_unknown: str,
    local_prefixes: Iterable[str],
) -> list[Finding]:
    findings: list[Finding] = []
    for entry in entries:
        symbol = symbols.get(entry.name)
        if not symbol:
            if show_unknown == "all" or (show_unknown == "local" and is_local_symbol(entry.name, local_prefixes)):
                findings.append(
                    Finding(
                        severity="ERROR",
                        source=entry.source,
                        line=entry.line,
                        message=f"CONFIG_{entry.name} has no matching Kconfig definition",
                    )
                )
            continue

        value_issue = validate_value(entry, symbol)
        if value_issue:
            findings.append(
                Finding(
                    severity="ERROR",
                    source=entry.source,
                    line=entry.line,
                    message=value_issue,
                )
            )
    return findings


def print_findings(findings: list[Finding], root: Path) -> None:
    for finding in findings:
        print(
            f"{finding.severity}: {display_path(finding.source, root)}:{finding.line}: {finding.message}"
        )


def main() -> int:
    args = parse_args()
    root = normalize_root(args.root)

    kconfig_files = discover_kconfig_files(root, args.include_build)
    workspace_symbols = load_workspace_symbols(kconfig_files)

    if args.sdkconfig:
        sdkconfig_files = [path if path.is_absolute() else root / path for path in args.sdkconfig]
    else:
        sdkconfig_files = discover_sdkconfig_files(root, args.include_build)

    print(f"Kconfig files: {len(kconfig_files)}")
    print(f"Workspace symbols: {len(workspace_symbols)}")
    print(f"sdkconfig files: {len(sdkconfig_files)}")

    all_findings = validate_definitions(workspace_symbols)

    for sdkconfig_file in sdkconfig_files:
        if not sdkconfig_file.is_file():
            all_findings.append(
                Finding("ERROR", sdkconfig_file, 1, "sdkconfig file does not exist")
            )
            continue

        generated_symbols = load_generated_menu_symbols(root, sdkconfig_file)
        symbols = merge_generated_symbols(workspace_symbols, generated_symbols)
        entries = parse_sdkconfig(sdkconfig_file)
        findings = validate_sdkconfig(entries, symbols, args.show_unknown, args.local_prefix)
        all_findings.extend(findings)

        if not findings and not args.quiet_ok:
            generated_note = f", {len(generated_symbols)} generated symbols" if generated_symbols else ""
            print(f"OK: {display_path(sdkconfig_file, root)} ({len(entries)} entries{generated_note})")

    if all_findings:
        print_findings(all_findings, root)
        if any(finding.severity == "ERROR" for finding in all_findings):
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())