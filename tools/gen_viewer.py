#!/usr/bin/env python3
"""Build the self-contained log viewer page (datalogger.html).

Inlines the vendored uPlot build, the CSV parser and the app script into
tools/viewer/src/viewer.html, so the result opens from file:// with no network
and no other file next to it. Runs the viewer tests first (needs node; skip
with --skip-tests).

The default output is the copy the firmware embeds (component log_viewer) and
writes to the root of the data volume: commit it after regenerating.

Usage: python3 tools/gen_viewer.py [--skip-tests] [-o OUTPUT]
       (default output: components/log_viewer/assets/datalogger.html)
"""

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VIEWER = ROOT / "tools" / "viewer"
TEMPLATE = VIEWER / "src" / "viewer.html"
DEFAULT_OUTPUT = ROOT / "components" / "log_viewer" / "assets" / "datalogger.html"
TESTS = VIEWER / "test"

INLINE = re.compile(r"^[ \t]*(?:/\*|//)@@INLINE_(CSS|JS) (\S+)@@(?:\*/)?[ \t]*$", re.MULTILINE)


def read_inline(kind, rel_path):
    path = VIEWER / rel_path
    text = path.read_text(encoding="utf-8")
    closing = "</style" if kind == "CSS" else "</script"
    if closing in text.lower():
        sys.exit(f"{rel_path}: contains '{closing}', would break the inline block")
    return text.rstrip("\n")


def run_tests():
    node = shutil.which("node")
    if node is None:
        sys.exit("node not found: install it or pass --skip-tests")
    for test in sorted(TESTS.glob("*.test.js")):
        result = subprocess.run([node, str(test)], cwd=VIEWER)
        if result.returncode != 0:
            sys.exit(f"{test.name} failed")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--skip-tests", action="store_true")
    parser.add_argument("-o", "--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()

    if not args.skip_tests:
        run_tests()

    version = (ROOT / "version.txt").read_text(encoding="utf-8").strip()
    license_text = (VIEWER / "vendor" / "LICENSE").read_text(encoding="utf-8").strip()
    license_text = "\n".join("  " + line if line else "" for line in license_text.splitlines())

    html = TEMPLATE.read_text(encoding="utf-8")
    html = INLINE.sub(lambda m: read_inline(m.group(1), m.group(2)), html)
    html = html.replace("@@FW_VERSION@@", version).replace("@@UPLOT_LICENSE@@", license_text)
    leftover = re.search(r"@@[A-Z_]+", html)
    if leftover:
        sys.exit(f"unresolved placeholder: {leftover.group(0)}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    # LF only and no BOM: byte-identical output across hosts, so the firmware
    # can compare the file on the volume with the embedded copy.
    with open(args.output, "w", encoding="utf-8", newline="\n") as out:
        out.write(html)
    print(f"{args.output.relative_to(ROOT)}: {args.output.stat().st_size / 1024:.1f} KB")


if __name__ == "__main__":
    main()
