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
koskari.cli

Command-line interface for koskari.

Provides file execution, import-hook installation, and a REPL with a
pluggable input backend that supports both readline and prompt_toolkit.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


import asyncio
import platform
import sys

from ..exceptions import (CodeBoundary, CompilerError, UncaughtCompilerError,
                          format_code_boundary_exception,
                          format_exception_with_koskari_trace)
from ..lexer.discovery import special_names_for_repl
from ..lexer.prompt_toolkit import (KoskariCompleter, KoskariLexer,
                                    koskari_style, load_koskari_key_bindings)


__all__ = (
    'CodeBoundary',
    'REPLCodeBoundary',
    '_await_user_form',
    '_make_repl_prompter',
    'format_code_boundary_exception',
    'format_exception_with_koskari_trace',
    'main',
    'repl_async',
    'run_command_async',
    'run_file_async',
)


# ---------------------------------------------------------------------------
# Version display
# ---------------------------------------------------------------------------


def version_line():
    """
    One-line version string for --version output

    :return: version line beginning with koskari
    """

    import koskari
    version = koskari.__version__

    return 'koskari %s' % (version,)


def startup_lines():
    """
    REPL startup banner lines modeled on CPython interactive startup

    :return: list of banner lines
    """

    import koskari
    version = koskari.__version__

    return [
        'Koskari %s on %s' % (version, platform.system()),
        'Python %s' % (sys.version,),
    ]


# ---------------------------------------------------------------------------
# Prompt backend selection
# ---------------------------------------------------------------------------


def _make_repl_prompter(histfile, ns):
    """
    Build an async prompter coroutine for the Koskari REPL.

    Returns an async callable ``await prompter(prompt, default='')``.  The
    prompt_toolkit backend uses ``prompt_async``, so it runs cooperatively
    on the REPL's own event loop -- no worker thread and no nested
    ``asyncio.run``.  The plain ``input()`` fallback is blocking, so it is
    offloaded to a worker thread.  EOFError / KeyboardInterrupt propagate to
    the caller; the Koskari REPL loop catches them at the await point.

    :param histfile: history file path, or None
    :param ns: REPL module namespace, for completion
    :return: an async callable shaped like input(prompt)
    """

    try:
        from prompt_toolkit import PromptSession
        from prompt_toolkit.auto_suggest import AutoSuggestFromHistory
        from prompt_toolkit.history import FileHistory

        repl_special_names = special_names_for_repl()
        history = FileHistory(histfile) if histfile else None
        completer = KoskariCompleter(
            get_namespace=lambda: ns,
            special_names=repl_special_names)
        session = PromptSession(
            history=history,
            lexer=KoskariLexer(special_names=repl_special_names),
            completer=completer,
            style=koskari_style,
            auto_suggest=AutoSuggestFromHistory(),
            key_bindings=load_koskari_key_bindings())

        async def _prompt(prompt, default=''):
            return await session.prompt_async(
                prompt,
                default=default,
                reserve_space_for_menu=False)

        return _prompt

    except ImportError:
        pass

    try:
        import readline

        if histfile:
            try:
                readline.read_history_file(histfile)
            except OSError:
                pass

            import atexit
            atexit.register(readline.write_history_file, histfile)

    except ImportError:
        pass

    async def _input_prompt(prompt, default=''):
        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(None, lambda: input(prompt))

    return _input_prompt


# ---------------------------------------------------------------------------
# REPL code boundary
# ---------------------------------------------------------------------------


REPL_INFRASTRUCTURE_MODULES = frozenset((
    'koskari.cli',
    'asyncio',
    'asyncio.base_events',
    'asyncio.events',
    'asyncio.runners',
    'concurrent.futures',
    'concurrent.futures._base',
))


class REPLCodeBoundary(CodeBoundary):
    """
    REPL eval boundary between infrastructure and user expression evaluation
    """

    pass


# ---------------------------------------------------------------------------
# REPL
# ---------------------------------------------------------------------------


async def _await_user_form(expr):
    """
    Boundary shim that awaits a compiled user form.

    This is the single Python frame that awaits a compiled REPL form and
    is entered by a Python call from the (Koskari) REPL loop so that the
    ``await`` allocates a fresh coroutine context rooted at the user form.
    Keeping the eval step behind this Python boundary is what keeps REPL
    infrastructure frames off the user form's native dynchain; see
    ``design/cli-repl-rework.md`` section 3.

    Compiler errors are re-raised so the Koskari loop can render them via
    ``.explain()``; any other error is wrapped in ``REPLCodeBoundary`` so
    its cause carries the user-scoped traceback and dynchain.

    :param expr: zero-argument awaitable produced by
        ``fragment.as_expression(ns)``
    :return: the awaited result of the user form
    """

    try:
        return await expr()
    except (UncaughtCompilerError, CompilerError):
        raise
    except Exception as e:
        raise REPLCodeBoundary() from e


async def repl_async(mod, prompter=None, histfile=None, stdin=None, stdout=None,
                     stderr=None, full_trace=False, quiet=False):
    """
    Run the interactive read-eval-print loop for a koskari module.

    Thin wrapper delegating to the Koskari-native REPL in repl.ki, imported
    lazily so the compile cost is only paid when a session starts.  Retained
    as the public entry point (``koskari.cli.repl_async``); the loop, read
    step, and error dispatch now live in Koskari.

    :param mod: koskari Module to use as the execution context
    :param prompter: callable with the same signature as input(); built via
        _make_repl_prompter when None
    :param histfile: history file path used when prompter is None
    :param stdin: input stream, defaults to sys.stdin
    :param stdout: output stream, defaults to sys.stdout
    :param stderr: error stream, defaults to sys.stderr
    :param full_trace: when True, show REPL boundary wrapper frames in errors
    :param quiet: when True, suppress REPL startup version banner
    """

    # The REPL lives in repl.ki, so loading it needs the koskari import hook
    # on sys.meta_path.  Ensure it is present (idempotent) before importing.
    from ..importer import install
    install()

    from . import repl as _repl_ki

    return await _repl_ki.repl_async(
        mod,
        prompter=prompter,
        histfile=histfile,
        stdin=stdin,
        stdout=stdout,
        stderr=stderr,
        full_trace=full_trace,
        quiet=quiet)


# ---------------------------------------------------------------------------
# File execution
# ---------------------------------------------------------------------------


async def run_file_async(mod, filename, execute_kio=False, *, interactive=False):
    """
    Load and execute a koskari source file into mod.

    Calls init_module to attach file metadata and compiler specials, then
    awaits load_module_async (or load_module_kio_async when execute_kio).

    :param mod: koskari Module to execute into
    :param filename: path to the .ki source file or .kio when execute_kio
    :param execute_kio: when True, load precompiled .kio bodies
    :param interactive: passed to load_module_async; retain load facets when True
    """

    from koskari.module import (init_module, load_module_async,
                                load_module_kio_async)

    init_module(mod, filename)
    if execute_kio:
        await load_module_kio_async(mod)
    else:
        await load_module_async(mod, interactive=interactive)


async def run_command_async(mod, command, *, interactive=False):
    """
    Load and execute koskari source from a command-line string into mod.

    Sets ``__file__`` to ``'<string>'`` and runs each top-level expression
    in order, matching file execution semantics.

    :param mod: koskari Module to execute into
    :param command: koskari source text from ``-c`` / ``--command``
    :param interactive: passed to load_module_source_async; retain load
        facets when True
    """

    from koskari.module import init_module, load_module_source_async

    init_module(mod, '<string>')
    await load_module_source_async(mod, command, interactive=interactive)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def main(argv=None):
    """
    Entry point for the koskari command-line interface.

    Boots the Koskari launchpad (``koskari.cli.main:cli``) through the
    launcher, the same path a packaged koskari_scripts command uses: the
    target is loaded and run under Domain and the async host.  Option
    parsing and orchestration live in Koskari (``main.ki``); this stub only
    dispatches and maps a top-level Ctrl-C to exit code 130.

    :param argv: argument list; defaults to sys.argv when None
    :return: integer exit code
    """

    from koskari.importer import install
    from koskari.launcher import run_target

    # The launchpad and REPL live in .ki modules; ensure the import hook is
    # present before the launcher loads koskari.cli.main.
    install()

    if argv is None:
        argv = sys.argv

    try:
        code = run_target(
            "koskari.cli.main:cli",
            argv=list(argv[1:]),
            script_name=argv[0])

    except KeyboardInterrupt:
        print('', file=sys.stderr)
        return 130

    return 0 if code is None else code


# The end.
