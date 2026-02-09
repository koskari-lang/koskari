#! /usr/bin/env python3

"""
Koskari, a LISP dialect for Python

:author: Christopher O'Brien  <obriencj@gmail.com>
:license: GPL v.3
"""

from os import environ
from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext


class FixedBuildExt(build_ext):
    """
    Fix for sdist - base build_ext does not include depends lists,
    which makes subsequent compilation fail.
    """
    def get_source_files(self):
        self.check_extensions_list(self.extensions)
        filenames = []
        for ext in self.extensions:
            filenames.extend(ext.sources)
            filenames.extend(ext.depends)
        return filenames


def extra_compile_args(platform=None, debug=False):
    """
    Get compiler flags for C extensions
    """

    if platform is None:
        from sys import platform as sys_platform
        platform = sys_platform

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


def extension(platform=None, debug=False):
    """
    Create Extension object for native module
    """

    return Extension(
        "koskari.lib",
        sources=[
            # Python version compatibility
            "koskari/lib/compat.c",
            # Implementation units
            "koskari/lib/atom.c",
            "koskari/lib/pair.c",
            "koskari/lib/ref.c",
            "koskari/lib/namespace.c",
            "koskari/lib/bytecode.c",
            "koskari/lib/formals.c",
            "koskari/lib/values.c",
            "koskari/lib/operator.c",
            "koskari/lib/stack.c",
            "koskari/lib/dynchain.c",
            "koskari/lib/utils.c",
            "koskari/lib/parse.c",
            "koskari/lib/eval.c",
            "koskari/lib/func.c",
            # Module initialization
            "koskari/lib/lib.c",
            # TODO: add source files as units are implemented
            # From python-sibilant:
            #   tco.c
            # From sibilant-ceval:
            #   bytecode.c, ceval.c, frame.c, func.c
        ],
        depends=[
            # Headers
            "koskari/lib/lib.h",
            "koskari/lib/compat.h",
            "koskari/lib/atom.h",
            "koskari/lib/pair.h",
            "koskari/lib/ref.h",
            "koskari/lib/namespace.h",
            "koskari/lib/bytecode.h",
            "koskari/lib/formals.h",
            "koskari/lib/values.h",
            "koskari/lib/operator.h",
            "koskari/lib/stack.h",
            "koskari/lib/dynchain.h",
            "koskari/lib/utils.h",
            "koskari/lib/parse.h",
            "koskari/lib/eval.h",
            "koskari/lib/func.h",
            # Compatibility headers
            "koskari/lib/compat_36.h",
            "koskari/lib/compat_37.h",
            "koskari/lib/compat_38.h",
            "koskari/lib/compat_39.h",
            "koskari/lib/compat_311.h",
            # Include files
            "koskari/lib/opcodes.incl",
            # TODO: add headers as units are implemented
        ],
        extra_compile_args=extra_compile_args(platform, debug),
    )


def setup_extensions():
    """Get list of extensions to build."""
    debug = environ.get("DEBUG") == "1"
    return [extension(debug=debug)]


if __name__ == "__main__":
    setup(ext_modules=setup_extensions(),
          cmdclass={'build_ext': FixedBuildExt})


# The end.
