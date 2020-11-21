#!/usr/bin/env python
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) Google LLC, 2020
#
# Author: Nathan Huckleberry <nhuck@google.com>
#
"""A helper routine run clang-tidy and the clang static-analyzer on
compile_commands.json.
"""

import argparse
import json
import multiprocessing
import os
import subprocess
import sys


def parse_arguments():
    """Set up and parses command-line arguments.
    Returns:
        args: Dict of parsed args
        Has keys: [path, type]
    """
    usage = """Run clang-tidy or the clang static-analyzer on a
        compilation database."""
    parser = argparse.ArgumentParser(description=usage)

    type_help = "Type of analysis to be performed"
    parser.add_argument("type",
                        choices=["clang-tidy-fix", "clang-tidy", "clang-analyzer"],
                        help=type_help)
    path_help = "Path to the compilation database to parse"
    parser.add_argument("path", type=str, help=path_help)

    return parser.parse_args()


def init(l, a):
    global lock
    global args
    lock = l
    args = a


def run_analysis(entry):
    # Disable all checks, then re-enable the ones we want
    checks = "-checks=-*,"
    fix = ""
    header_filter = ""
    if args.type == "clang-tidy-fix":
        checks += "linuxkernel-macro-trailing-semi"
        #
        # Fix this
        # #define M(a) a++; <-- clang-tidy fixes the problem here
        # int f() {
        #   int v = 0;
        #   M(v);  <-- clang reports problem here
        #   return v;
        # }
        fix += "-fix"
        header_filter += "-header-filter=.*"
    elif args.type == "clang-tidy":
        checks += "linuxkernel-*"
    else:
        checks += "clang-analyzer-*"
    p = subprocess.run(["clang-tidy", "-p", args.path, checks, header_filter, fix, entry["file"]],
                       stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT,
                       cwd=entry["directory"])
    with lock:
        sys.stderr.buffer.write(p.stdout)


def main():
    args = parse_arguments()

    lock = multiprocessing.Lock()
    pool = multiprocessing.Pool(initializer=init, initargs=(lock, args))
    # Read JSON data into the datastore variable
    with open(args.path, "r") as f:
        datastore = json.load(f)
        pool.map(run_analysis, datastore)


if __name__ == "__main__":
    main()
