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
import os
import shlex
import subprocess
import sys
from os import environ
from pathlib import Path

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext
from setuptools.command.build_py import build_py
from setuptools.command.install_scripts import install_scripts
from setuptools.command.sdist import sdist


passenvs = (
    "KOS_TRACE_FREELIST",
    "KOS_TRACE_EVENTS",
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

KOSKARI_APP_MAIN = Path("koskari/app/main.c")
KOSKARI_BIN_NAME = "koskari"
KOSKARI_BIN_SUBDIR = "koskari-bin"


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


def koskari_executable_path(build_base="build"):
    """
    Return the build output path for the native koskari executable
    """

    return Path(build_base) / KOSKARI_BIN_SUBDIR / KOSKARI_BIN_NAME


def _python_config_names(python=None):
    python = python or sys.executable
    prefix = os.path.splitext(python)[0]
    names = []
    for candidate in (prefix + "-config", "python3-config", "python-config"):
        if candidate not in names:
            names.append(candidate)
    return names


def _python_config_output(args, python=None):
    """
    Run python-config and return stripped stdout
    """

    errors = []
    for config_name in _python_config_names(python):
        try:
            return subprocess.check_output(
                [config_name] + list(args),
                stderr=subprocess.STDOUT,
                text=True,
            ).strip()
        except (OSError, subprocess.CalledProcessError) as exc:
            errors.append("%s: %s" % (config_name, exc))

    raise RuntimeError(
        "failed to query python-config (%s)"
        % ("; ".join(errors),))


def _python_embed_cflags(python=None):
    try:
        return _python_config_output(["--cflags", "--embed"], python)
    except RuntimeError:
        return _python_config_output(["--cflags"], python)


def _python_embed_ldflags(python=None):
    try:
        return _python_config_output(["--ldflags", "--embed"], python)
    except RuntimeError:
        return _python_config_output(["--ldflags"], python)


def build_koskari_executable(output_path, python=None):
    """
    Compile the embed koskari command into output_path
    """

    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    if not KOSKARI_APP_MAIN.is_file():
        raise FileNotFoundError("missing embed entry %s" % (KOSKARI_APP_MAIN,))

    cc = environ.get("CC", "gcc")
    cflags = shlex.split(_python_embed_cflags(python))
    ldflags = shlex.split(_python_embed_ldflags(python))
    compile_cmd = (
        [cc, str(KOSKARI_APP_MAIN), "-o", str(output_path)]
        + cflags
        + ldflags
    )
    subprocess.check_call(compile_cmd)


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


class kos_build_ext(build_ext):
    """
    Build koskari.lib and the native koskari executable
    """

    def run(self):
        super().run()
        build_cmd = self.get_finalized_command('build')
        output_path = koskari_executable_path(build_cmd.build_base)
        build_koskari_executable(output_path)
        self.koskari_executable = output_path


class kos_install_scripts(install_scripts):
    """
    Install the native koskari executable into bindir
    """

    def run(self):
        build_ext_cmd = self.get_finalized_command("build_ext")
        build_cmd = self.get_finalized_command("build")
        output_path = getattr(
            build_ext_cmd,
            "koskari_executable",
            koskari_executable_path(build_cmd.build_base),
        )
        if not output_path.is_file():
            build_koskari_executable(output_path)

        self.mkpath(self.install_dir)
        dest = os.path.join(self.install_dir, KOSKARI_BIN_NAME)
        self.copy_file(str(output_path), dest, preserve_mode=True)
        os.chmod(dest, 0o755)
        super().run()


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
    setup(
        cmdclass={
            'build_ext': kos_build_ext,
            'install_scripts': kos_install_scripts,
            'sdist': kos_sdist,
            'build_py': kos_build_py,
        },
        ext_modules=[extension()],
    )


# The end.
