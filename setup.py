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
import re
import shlex
import subprocess
import sys
import sysconfig
from dataclasses import dataclass
from os import environ
from pathlib import Path
from shutil import which

from setuptools import Extension, setup
from setuptools.command.build_py import build_py
from setuptools.command.sdist import sdist


passenvs = (
    "KOS_TRACE_FREELIST",
    "KOS_TRACE_EVENTS",
    "KOS_TRACE_OPCACHE",
    "KOS_TRACE_TIMING",
    "KOS_TRACE_EVAL_SYNC",
    "KOS_STACK_TRACE",
    "KOS_DEBUG_PATCH_DYN_CACHES",
    "KOS_EVAL_SAFETYNET",
    "KOS_ALL_COLD",
    "KOS_NOFREELIST",
    "VERBOSE_DEBUG",
)


baseline_args = (
    "--std=c99",
    "-Wall",
    "-Werror",
    "-flto=auto",
    # Nothing outside the extension links against its symbols; the module
    # init keeps default visibility through PyMODINIT_FUNC. Hidden
    # visibility turns every intra-library call into a direct call and
    # lets LTO inline single-caller helpers.
    "-fvisibility=hidden",
    # gcc treats every default-visibility function as interposable and
    # will neither inline nor direct-call it; clang already assumes no
    # interposition.
    "-fno-semantic-interposition",
)


LINUX = sys.platform == "linux"


def compiler_command():
    """
    The C compiler the extension build uses. CC in the environment
    wins; otherwise clang when it is on the PATH, exported as CC so
    setuptools uses it for both compiling and linking; otherwise the
    interpreter's own configured compiler
    """

    cc = environ.get("CC")
    if cc:
        return cc

    if which("clang"):
        environ["CC"] = "clang"
        return "clang"

    return sysconfig.get_config_var("CC") or "cc"


def compiler_identity(cc):
    """
    Whether the compiler command identifies itself as clang, and its
    major version when it does

    :return: (is_clang, major); major is 0 when unknown
    """

    try:
        proc = subprocess.run(
            [*shlex.split(cc), "--version"],
            capture_output=True, text=True, check=False)
    except OSError:
        return False, 0

    banner = proc.stdout.lower()
    if "clang" not in banner:
        return False, 0

    match = re.search(r"clang version (\d+)", banner)
    return True, int(match.group(1)) if match else 0


def linker_takes_lto(cc, clang):
    """
    Whether the link step can consume LTO objects. gcc links through
    its own plugin; ld64 runs LTO itself; clang on Linux needs lld or
    the LLVMgold plugin for bfd and gold
    """

    if not clang or not LINUX:
        return True

    if which("ld.lld"):
        return True

    try:
        proc = subprocess.run(
            [*shlex.split(cc), "-print-file-name=LLVMgold.so"],
            capture_output=True, text=True, check=False)
    except OSError:
        return False

    plugin = proc.stdout.strip()
    return bool(plugin) and Path(plugin).is_file()


def optimisation_args(debug):
    """
    The optimisation level, given to the compile and the link alike
    since gcc takes the LTO level from the link line. The evaluator
    dispatches by tail call; omitting the frame pointer keeps every
    handler entry free of a push and pop and frees the register, and
    overrides the -fno-omit-frame-pointer some distributions build
    the interpreter with
    """

    if debug:
        return ["-g", "-O0"]
    return ["-O2", "-fomit-frame-pointer"]


@dataclass
class BuildFlags:
    """
    The build's toolchain facts, resolved once by resolve_build_flags
    """

    cc: str
    clang: bool
    major: int
    debug: bool
    asan: bool
    lto_link: bool
    fat_objects: bool


def resolve_build_flags():
    """
    Resolve the compiler, its identity, the build mode and what the
    link can do

    :return: a BuildFlags
    """

    cc = compiler_command()
    clang, major = compiler_identity(cc)
    debug = environ.get("DEBUG") == "1"
    asan = environ.get("KOS_ASAN") == "1"
    lto_link = linker_takes_lto(cc, clang)

    # Fat LTO objects carry native code beside the bitcode, so a linker
    # that cannot run LTO still links the extension, without it. They
    # are the fallback for that linker only; an LTO-aware link takes
    # slim objects. Under ASan the fat object's native sections carry
    # the sanitizer's module constructors, which lld discards after the
    # LTO link while the init array still refers to them, so the ASan
    # build never emits them.
    fat_objects = LINUX and not asan and not lto_link

    # With no fat objects every object is slim bitcode, which only an
    # LTO-aware linker can take: the ASan build emits none, and a clang
    # before 17 cannot emit them at all. Without lld or the gold plugin
    # the link would fail with bfd's message, so refuse here instead.
    old_clang = bool(major) and major < 17
    if LINUX and clang and not lto_link and (asan or old_clang):
        if asan:
            reason = "the ASan build emits no fat LTO objects"
            remedy = "install lld"
        else:
            reason = f"clang {major} cannot emit fat LTO objects"
            remedy = "install lld, or build with clang 17 or newer"
        raise SystemExit(
            f"koskari: {reason}, and no LTO-aware linker was found;"
            f" {remedy}")

    return BuildFlags(cc, clang, major, debug, asan, lto_link, fat_objects)


def extra_compile_args(flags):
    """
    Get compiler flags for C extensions
    """

    args = list(baseline_args)
    args.extend(optimisation_args(flags.debug))

    for env in passenvs:
        if environ.get(env) == "1":
            args.append(f"-D{env}=1")

    if flags.asan:
        # after the optimisation level: the sanitizer's traces need the
        # frame pointer back
        args.append("-DKOS_ASAN=1")
        args.append("-fsanitize=address")
        args.append("-fno-omit-frame-pointer")
        args.append("-g")

    if LINUX:
        # calls into libpython bind through the GOT directly instead of
        # a PLT stub
        args.append("-fno-plt")

    if flags.fat_objects:
        args.append("-ffat-lto-objects")

    return args


def extra_link_args(flags):
    """
    Get linker flags for C extensions
    """

    args = []

    if flags.asan:
        args.append("-fsanitize=address")

    if LINUX and flags.clang and which("ld.lld"):
        # clang's LTO objects need an LTO-aware linker; lld is that
        # everywhere the containers and wheel images build, and the gold
        # plugin is not
        args.append("-fuse-ld=lld")

    if LINUX:
        # bind every GOT entry at load: with -fno-plt the calls into
        # libpython go through the GOT, and binding now spares the
        # first-call resolver and lets the loader seal the table
        args.append("-Wl,-z,now")

    if flags.lto_link:
        # The link is where LTO happens; without -flto here the linker
        # takes the objects' native code (fat) or refuses them (slim).
        args.append("-flto=auto")
        args.extend(optimisation_args(flags.debug))

    return args


def extension():
    """
    Create Extension object for native module
    """

    libdir = Path("koskari/lib")
    sources = sorted(map(str, libdir.glob("*.c")))

    depends = list(libdir.glob("*.h"))
    depends.extend(libdir.glob("*.incl"))
    depends.extend(libdir.glob("dynchain/*.c"))
    depends.extend(libdir.glob("transform/*.c"))
    depends.extend(libdir.glob("eval/*.c"))
    depends.extend(libdir.glob("func/*.c"))
    depends.extend(libdir.glob("matcher/*.c"))
    depends.extend(libdir.glob("opcode/*.c"))
    depends = sorted(map(str, depends))

    flags = resolve_build_flags()

    return Extension(
        "koskari.lib",
        sources=sources,
        depends=depends,
        extra_compile_args=extra_compile_args(flags),
        extra_link_args=extra_link_args(flags),
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
