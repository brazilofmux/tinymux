#!/usr/bin/env python3
"""Validate the Win32 GUI Visual Studio project from non-Windows hosts.

This is a lightweight Linux-side guardrail, not a replacement for a real
Windows build. It verifies that:

- every file referenced by `win32gui.vcxproj` exists
- key shared-source files for the Hydra/console integration are still wired in
- the expected project configurations are present
"""

from pathlib import Path
import sys
import xml.etree.ElementTree as ET


PROJECT = Path(__file__).resolve().parent / "win32gui.vcxproj"
MSBUILD_NS = {"msb": "http://schemas.microsoft.com/developer/msbuild/2003"}
REQUIRED_CONFIGS = {"Debug|x64", "Release|x64"}
REQUIRED_COMPILE_ENTRIES = {
    r"..\console\src\connection.cpp",
    r"..\console\src\hydra_connection.cpp",
    r"..\console\src\macro.cpp",
    r"src\outputbuffer.cpp",
}


def normalize_include(include: str) -> str:
    return include.replace("/", "\\")


def resolve_include(project_dir: Path, include: str) -> Path:
    return (project_dir / include.replace("\\", "/")).resolve()


def main() -> int:
    project_dir = PROJECT.parent
    tree = ET.parse(PROJECT)
    root = tree.getroot()

    configs = {
        node.attrib["Include"]
        for node in root.findall("msb:ItemGroup[@Label='ProjectConfigurations']/msb:ProjectConfiguration", MSBUILD_NS)
    }
    missing_configs = sorted(REQUIRED_CONFIGS - configs)
    if missing_configs:
        print("ERROR: missing project configurations:", ", ".join(missing_configs), file=sys.stderr)
        return 1

    missing_paths = []
    compile_entries = set()

    for tag in ("ClCompile", "ClInclude", "ResourceCompile"):
        for node in root.findall(f".//msb:{tag}", MSBUILD_NS):
            include = node.attrib.get("Include")
            if not include:
                continue
            if tag == "ClCompile":
                compile_entries.add(normalize_include(include))
            resolved = resolve_include(project_dir, include)
            if not resolved.exists():
                missing_paths.append(f"{tag}: {include}")

    if missing_paths:
        print("ERROR: missing project paths referenced by win32gui.vcxproj:", file=sys.stderr)
        for item in missing_paths:
            print(f"  {item}", file=sys.stderr)
        return 1

    missing_compile_entries = sorted(REQUIRED_COMPILE_ENTRIES - compile_entries)
    if missing_compile_entries:
        print("ERROR: missing required shared-source compile entries:", file=sys.stderr)
        for item in missing_compile_entries:
            print(f"  {item}", file=sys.stderr)
        return 1

    print("OK: win32gui.vcxproj references resolve and required shared sources are present.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
