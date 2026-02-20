#! /usr/bin/env python3

"""
Koskari, a LISP dialect for Python

:author: Christopher O'Brien  <obriencj@gmail.com>
:license: GPL v.3
"""

from os import environ
from pathlib import Path
from setuptools import Extension, setup


def extra_compile_args(platform=None, debug=False):
    """
    Get compiler flags for C extensions
    """

    if platform is None:
        from sys import platform as sys_platform
        platform = sys_platform

    if debug is None:
        debug = environ.get("DEBUG") == "1"

    args = [
        "--std=c99",
        "-Wall",
        "-Werror",
        "-flto=auto",
    ]

    if debug:
        args.append("-g")
        args.append("-O0")
    else:
        args.append("-O2")

    if environ.get("VERBOSE_DEBUG") == "1":
        args.append("-DVERBOSE_DEBUG=1")
    if environ.get("KOS_TRACE_FREELIST") == "1":
        args.append("-DKOS_TRACE_FREELIST=1")
    if environ.get("KOS_TRACE_INTERN") == "1":
        args.append("-DKOS_TRACE_INTERN=1")
    if environ.get("KOS_TRACE_EVENTS") == "1":
        args.append("-DKOS_TRACE_EVENTS=1")
    if environ.get("KOS_TRACE_TIMING") == "1":
        args.append("-DKOS_TRACE_TIMING=1")

    if platform == "linux":
        args.append("-ffat-lto-objects")

    return args


def extension(platform=None, debug=None):
    """
    Create Extension object for native module
    """

    libdir = Path("koskari/lib")
    sources = sorted(map(str, libdir.glob("*.c")))

    depends = list(libdir.glob("*.h"))
    depends.extend(libdir.glob("*.incl"))
    depends.extend(libdir.glob("opcode/*.c"))
    depends = sorted(map(str, depends))

    return Extension(
        "koskari.lib",
        sources=sources,
        depends=depends,
        extra_compile_args=extra_compile_args(platform, debug),
    )


if __name__ == '__main__':
    setup(ext_modules=[extension()])


# The end.
