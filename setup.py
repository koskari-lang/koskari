#! /usr/bin/env python3

# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.


"""
Koskari, a LISP dialect for Python

:author: Christopher O'Brien  <obriencj@gmail.com>
:license: GPL v.3
"""


import importlib.util
from os import environ
from pathlib import Path

from setuptools import Extension, setup
from setuptools.command.build_py import build_py
from setuptools.command.sdist import sdist


passenvs = (
    "KOS_TRACE_FREELIST",
    "KOS_TRACE_EVENTS",
    "KOS_TRACE_OPCACHE",
    "KOS_TRACE_TIMING",
    "KOS_TRACE_EVAL_SYNC",
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
        args.append("-fno-gcse")
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
    depends.extend(libdir.glob("dynchain/*.c"))
    depends.extend(libdir.glob("eval/*.c"))
    depends.extend(libdir.glob("opcode/*.c"))
    depends = sorted(map(str, depends))

    return Extension(
        "koskari.lib",
        sources=sources,
        depends=depends,
        extra_compile_args=extra_compile_args(platform, debug),
    )


class kos_build_py(build_py):
    """
    Copy koskari.pth into build_lib for site-packages installation
    """

    def run(self):
        maybe_regen_version()

        # ensure the parent class runs its own logic. This creates the
        # build_lib directory we'll use below
        super().run()

        # the fact that pyproject.toml cannot do this, even as the vaunted
        # next generation of python packaging, is a goddamned joke.
        self.copy_file('koskari.pth', self.build_lib, preserve_mode=False)


def maybe_regen_version(force=False):
    """
    Regenerate koskari/_version.py when the regen tool is available
    """

    root = Path(__file__).resolve().parent
    path = root / 'tools' / 'regen_version.py'
    if not path.exists():
        return False

    spec = importlib.util.spec_from_file_location('regen_version', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)

    if force or mod.should_regen():
        mod.regen_version_py()


class kos_sdist(sdist):
    """
    Bake koskari/_version.py into the sdist before packaging
    """

    def run(self):
        maybe_regen_version(force=True)
        super().run()


if __name__ == '__main__':
    Path('build').mkdir(exist_ok=True)
    setup(
        cmdclass={
            'sdist': kos_sdist,
            'build_py': kos_build_py,
        },
        ext_modules=[extension()],
        options={
            'egg_info': {
                'egg_base': 'build',
            },
        },
    )


# The end.
