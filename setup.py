#! /usr/bin/env python3

"""
Koskari, a LISP dialect for Python

:author: Christopher O'Brien  <obriencj@gmail.com>
:license: GPL v.3
"""

from os.path import join
from pathlib import Path

from setuptools import Extension, setup
from setuptools.command.build_py import build_py as setuptools_build_py

from os import environ


passenvs = (
    "KOS_TRACE_FREELIST",
    "KOS_TRACE_INTERN",
    "KOS_TRACE_EVENTS",
    "KOS_TRACE_TIMING",
    "VERBOSE_DEBUG",
)


baseline_args = (
    "--std=c99",
    "-Wall",
    "-Werror",
    "-flto=auto",
)


def extra_compile_args(platform=None, debug=False):
    """
    Get compiler flags for C extensions
    """

    if platform is None:
        from sys import platform as sys_platform
        platform = sys_platform

    if debug is None:
        debug = environ.get("DEBUG") == "1"

    args = list(baseline_args)

    if debug:
        args.append("-g")
        args.append("-O0")
    else:
        args.append("-O2")

    for env in passenvs:
        if environ.get(env) == "1":
            args.append(f"-D{env}=1")

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
    depends.extend(libdir.glob("dynchain/*.c"))
    depends = sorted(map(str, depends))

    return Extension(
        "koskari.lib",
        sources=sources,
        depends=depends,
        extra_compile_args=extra_compile_args(platform, debug),
    )


class build_py(setuptools_build_py):
    """
    Copy koskari.pth into build_lib so wheels place it at site-packages root.
    """

    def run(self):
        super().run()
        root = Path(__file__).resolve().parent
        pth = root / "koskari.pth"
        if not self.build_lib or not pth.is_file():
            return
        from shutil import copyfile
        copyfile(str(pth), join(self.build_lib, "koskari.pth"))


if __name__ == '__main__':
    setup(
        cmdclass={'build_py': build_py},
        ext_modules=[extension()],
    )


# The end.
