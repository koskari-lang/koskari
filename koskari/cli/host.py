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
koskari.cli.host

Python host primitives for the Koskari CLI: file and command loaders,
REPL prompt backend, eval boundary shim, and banner helpers.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import platform
import sys

import koskari

# from . import term_graphics
from ..exceptions import CodeBoundary, CompilerError, UncaughtCompilerError
from ..lexer.discovery import special_names_for_repl
from ..module import find_module_reader


__all__ = (
    'REPLCodeBoundary',
    'REPL_INFRASTRUCTURE_MODULES',
    '_await_user_form',
    '_make_repl_prompter',
    # 'maybe_print_logo',
    'startup_lines',
    'version_line',
)


# ---------------------------------------------------------------------------
# Version display
# ---------------------------------------------------------------------------


def version_line():
    """
    One-line version string for --version output

    :return: version line beginning with koskari
    """

    return f'koskari {koskari.__version__}'


def startup_lines():
    """
    REPL startup banner lines modeled on CPython interactive startup

    :return: list of banner lines
    """

    return [
        f'Koskari {koskari.__version__} on {platform.system()}',
        f'Python {sys.version}',
    ]


# def maybe_print_logo(stream=None):
#     """
#     Print the packaged icon to stream via a terminal graphics protocol,
#     if one is detected. Silently does nothing otherwise.
#
#     :param stream: output stream, defaults to sys.stdout
#     :return: None
#     """
#
#     term_graphics.emit_icon(stream=stream)


# ---------------------------------------------------------------------------
# Prompt backend selection
# ---------------------------------------------------------------------------


def _make_repl_prompter(histfile, ns):
    """
    Build an async prompter coroutine for the Koskari REPL.

    Returns an async callable ``await prompter(prompt, default='')``.  The
    prompt_toolkit backend uses ``prompt_async``, so it runs cooperatively
    on the REPL's own event loop -- no worker thread and no nested
    ``asyncio.run``.  The plain ``input()`` fallback reads on the main
    thread, as Python's own REPL does: SIGINT raises KeyboardInterrupt
    inside the read and EOF raises EOFError, both at the REPL's await
    point, and nothing is left blocked in a thread at exit.  The loop does
    not advance while that prompt waits, so a background task started in
    a session runs only while a form evaluates until prompt_toolkit is
    installed.

    :param histfile: history file path, or None
    :param ns: REPL module namespace, for completion
    :return: an async callable shaped like input(prompt)
    """

    try:
        from prompt_toolkit import PromptSession
        from prompt_toolkit.auto_suggest import AutoSuggestFromHistory
        from prompt_toolkit.history import FileHistory

        from ..lexer.prompt_toolkit import (
            KoskariCompleter, KoskariLexer, koskari_style,
            load_koskari_key_bindings)

        repl_special_names = special_names_for_repl()
        history = FileHistory(histfile) if histfile else None
        completer = KoskariCompleter(
            get_namespace=lambda: ns,
            special_names=repl_special_names)
        session = PromptSession(
            history=history,
            lexer=KoskariLexer(
                special_names=repl_special_names,
                get_reader=lambda: find_module_reader(ns)),
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
        readline = None

    def _read_line(prompt, default):
        """
        input() with the default pre-filled on the line through
        readline's startup hook, when readline is present; without it
        nothing can pre-fill a plain input line and the default is not
        offered
        """

        if readline is None or not default:
            return input(prompt)
        readline.set_startup_hook(lambda: readline.insert_text(default))
        try:
            return input(prompt)
        finally:
            readline.set_startup_hook(None)

    async def _input_prompt(prompt, default=''):
        return _read_line(prompt, default)

    return _input_prompt


# ---------------------------------------------------------------------------
# REPL code boundary
# ---------------------------------------------------------------------------


REPL_INFRASTRUCTURE_MODULES = frozenset((
    'koskari.cli',
    'koskari.cli.host',
    'koskari.importer',
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


# Lazy adapters


async def run_file_async(
        mod,
        filename,
        execute_kio=False,
        interactive=False):
    """
    Load and execute a koskari source file into mod.

    Calls init_module to attach file metadata and compiler specials, then
    awaits load_module_async (or load_module_kio_async when execute_kio).

    :param mod: koskari Module to execute into
    :param filename: path to the .ki source file or .kio when execute_kio
    :param execute_kio: when True, load precompiled .kio bodies
    :param interactive: when True, retain load facets for the ongoing
        session (the module still seals on completion)
    """

    from .launch import run_file
    return await run_file(
        mod, filename,
        execute_kio=execute_kio,
        interactive=interactive)


async def run_command_async(mod, command, interactive=False):
    """
    Load and execute koskari source from a command-line string into mod.

    Sets ``__file__`` to ``'<string>'`` and runs each top-level expression
    in order, matching file execution semantics.

    :param mod: koskari Module to execute into
    :param command: koskari source text from ``-c`` / ``--command``
    :param interactive: when True, retain load facets for the ongoing
        session (the module still seals on completion)
    """

    from .launch import run_command
    return await run_command(
        mod, command,
        interactive=interactive)


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
    ``design/archive/cli-repl-rework.md`` section 3.

    Compiler errors are re-raised so the Koskari loop can render them via
    ``.explain()``; any other error is wrapped in ``REPLCodeBoundary`` so
    its cause carries the user-scoped traceback, Koskari frames included.

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


# The end.
