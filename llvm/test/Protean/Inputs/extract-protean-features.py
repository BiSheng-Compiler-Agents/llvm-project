#!/usr/bin/env python3
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Print Protean's module metadata as one 'header = value' pair per line.

Read textual LLVM IR from a file or stdin, following the proteanFeatureHeaders
and proteanFeatureValues module flags. Preserves header order and the spelling
of the strings in the IR, including decimal values and hexadecimal escapes.

Example lit usage:
  ; RUN: opt -passes=protean-collect-features -S %s | \\
  ; RUN:   %python %S/Inputs/extract-protean-features.py | FileCheck %s --match-full-lines
  ; CHECK-DAG: block_freq = 17

For bitcode input, pipe llvm-dis's output into this script first.
"""

import argparse
import re
import sys

# Embedded quotes are written as \22, so only literal quotes delimit strings.
IR_STRING = r'"[^"]*"'
MD_STRING = r'!"([^"]*)"'

# Capture (node ID, tuple body) from !42 = distinct !{...} or
# !llvm.module.flags = !{...}. Match strings as units so an embedded '}'
# does not terminate the tuple.
METADATA_NODE = re.compile(
    r"^\s*!(\d+|llvm\.module\.flags)\s*=\s*(?:distinct\s+)?!\{"
    r'((?:"[^"]*"|[^"}])*)\}',
    re.MULTILINE,
)

# Capture (flag name, value node ID) from a Protean module flag, e.g.
# i32 5, !"proteanFeatureHeaders", !42.
FEATURE_FLAG = re.compile(
    r'i32\s+\d+\s*,\s*!"(proteanFeatureHeaders|proteanFeatureValues)"' r"\s*,\s*!(\d+)"
)


def extract_features(ir):
    # Ignore comments without treating semicolons inside strings as comments.
    ir = re.sub(
        rf"{IR_STRING}|;[^\n]*",
        lambda match: "" if match[0].startswith(";") else match[0],
        ir,
    )
    nodes = dict(METADATA_NODE.findall(ir))

    def get_node(node_id):
        if node_id not in nodes:
            raise ValueError(f"missing metadata node !{node_id}")
        return nodes[node_id].strip()

    module_flags = get_node("llvm.module.flags")

    flags = {}
    for node_id in re.findall(r"!(\d+)", module_flags):
        match = FEATURE_FLAG.fullmatch(get_node(node_id))
        if match:
            name, array_id = match.groups()
            if name in flags:
                raise ValueError(f"duplicate module flag '{name}'")
            flags[name] = array_id

    def get_strings(name):
        if name not in flags:
            raise ValueError(f"missing module flag '{name}'")
        body = get_node(flags[name])
        # Only strings, commas and whitespace should remain in a feature tuple.
        if re.sub(rf"{MD_STRING}|[\s,]", "", body):
            raise ValueError(f"expected metadata strings in module flag '{name}'")
        return re.findall(MD_STRING, body)

    headers = get_strings("proteanFeatureHeaders")
    values = get_strings("proteanFeatureValues")
    if len(headers) != len(values):
        raise ValueError(
            f"feature count mismatch: {len(headers)} headers, {len(values)} values"
        )
    return zip(headers, values)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "input",
        nargs="?",
        type=argparse.FileType("r", encoding="utf-8"),
        default=sys.stdin,
        help="textual LLVM IR file (default: stdin; use '-' for stdin)",
    )
    args = parser.parse_args()
    try:
        features = extract_features(args.input.read())
    except (OSError, ValueError) as error:
        print(f"{parser.prog}: error: {error}", file=sys.stderr)
        return 1
    for header, value in features:
        print(f"{header} = {value}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
