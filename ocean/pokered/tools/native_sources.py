#!/usr/bin/env python3
"""Prints the C sources of vendor/pokered-native's `pokered` library, one per line.

Reads them from upstream's own CMakeLists.txt files (the `target_sources(pokered ...)`
calls reached through add_subdirectory, skipping deps/), the way upstream's setup.py does,
so the list cannot fall behind a vendor update. Upstream's top-level CMake cannot be used
directly: it add_subdirectory()s its deps/ submodules (raylib, unity) for tests and the
visual build, none of which the pokered backend needs.

    usage: native_sources.py <path to vendor/pokered-native>
"""
import os
import re
import sys
from pathlib import Path

LINE_COMMENT = re.compile(r"#[^\n]*")
COMMAND = re.compile(r"\b(target_sources|add_subdirectory)\s*\(([^)]*)\)")
SCOPES = {"PRIVATE", "PUBLIC", "INTERFACE"}


def library_sources(root, directory, target="pokered"):
    text = LINE_COMMENT.sub("", (directory / "CMakeLists.txt").read_text())
    sources = []
    for command, arguments in COMMAND.findall(text):
        words = arguments.split()
        if not words:
            continue
        if command == "add_subdirectory":
            child = Path(os.path.normpath(directory / words[0]))
            if child.relative_to(root).parts[0] != "deps" and (child / "CMakeLists.txt").exists():
                sources += library_sources(root, child, target)
        elif words[0] == target:
            for word in words[1:]:
                if word in SCOPES:
                    continue
                path = Path(os.path.normpath(directory / word))
                if not path.is_file():
                    sys.exit(f"{directory / 'CMakeLists.txt'} lists missing source {word}")
                sources.append(path.as_posix())
    return sources


if __name__ == "__main__":
    root = Path(sys.argv[1]).resolve()
    print("\n".join(library_sources(root, root)))
