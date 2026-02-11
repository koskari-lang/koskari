#! /usr/bin/env python3

"""
Koskari, a LISP dialect for Python

:author: Christopher O'Brien  <obriencj@gmail.com>
:license: GPL v.3
"""

from os import environ
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
        "-O2",
        "-flto=auto",
    ]

    if debug:
        args.append("-g")
        args.append("-DVERBOSE_DEBUG=1")

    if platform == "linux":
        args.append("-ffat-lto-objects")

    return args


def extension(platform=None, debug=None):
    """
    Create Extension object for native module
    """

    from pathlib import Path

    libdir = Path("koskari/lib")
    sources = sorted(map(str, libdir.glob("*.c")))
    depends = sorted(map(str, libdir.glob("*.h")))
    depends.append(str(libdir / "opcodes.incl"))

    return Extension(
        "koskari.lib",
        sources=sources,
        depends=depends,
        extra_compile_args=extra_compile_args(platform, debug),
    )


setup(ext_modules=[extension()])


# The end.
