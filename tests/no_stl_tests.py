"""Reject standard-library dependencies in shipped gkcore code."""
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
SEARCH_ROOTS = (ROOT / "include", ROOT / "src", ROOT / "examples")
BANNED_HEADER = re.compile(
    r"^\s*#\s*include\s*[<\"](?:algorithm|array|cassert|ccomplex|cctype|cerrno|cfenv|cfloat|cinttypes|climits|clocale|cmath|csetjmp|csignal|cstdarg|cstdbool|cstddef|cstdint|cstdio|cstdlib|cstring|ctime|cwchar|cwctype|deque|exception|functional|initializer_list|ios|iostream|iterator|limits|list|map|memory|new|numeric|optional|queue|random|regex|set|span|stdexcept|string|string_view|system_error|tuple|type_traits|typeindex|typeinfo|unordered_map|unordered_set|utility|valarray|variant|vector)(?:/[^>\"]*)?[>\"]",
    re.MULTILINE,
)


def scan_text(name: str, text: str) -> list[str]:
    found = []
    for number, line in enumerate(text.splitlines(), 1):
        if "std::" in line:
            found.append(f"{name}:{number}: std:: namespace usage")
    for match in BANNED_HEADER.finditer(text):
        number = text.count("\n", 0, match.start()) + 1
        found.append(f"{name}:{number}: standard-library header")
    return found


def violations():
    found = []
    for root in SEARCH_ROOTS:
        if not root.exists():
            continue
        for path in sorted(p for p in root.rglob("*") if p.suffix in {".h", ".hpp", ".cpp", ".cc", ".cxx"}):
            found.extend(scan_text(str(path.relative_to(ROOT)), path.read_text(encoding="utf-8")))
    return found


class ScannerUnitTests(unittest.TestCase):
    def test_rejects_cpp_namespace_and_wrapper_headers(self):
        found = scan_text("sample.cpp", "#include <cstdio>\nstd::vector<int> items;\n")
        self.assertEqual(len(found), 2)

    def test_allows_c_runtime_headers_and_global_c_apis(self):
        self.assertEqual(scan_text("sample.c", "#include <stdio.h>\n#include <stdint.h>\nprintf(\"ok\");\n"), [])


class NoStandardLibraryTests(unittest.TestCase):
    def test_runtime_and_examples_do_not_depend_on_cpp_standard_library(self):
        found = violations()
        if found:
            preview = "\n".join(found[:30])
            remaining = len(found) - min(30, len(found))
            if remaining:
                preview += f"\n... and {remaining} more"
            self.fail(f"{len(found)} no-STL violations:\n{preview}")


if __name__ == "__main__":
    unittest.main()
