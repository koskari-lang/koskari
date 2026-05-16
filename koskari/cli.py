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
import sys
import traceback
from argparse import REMAINDER, ArgumentParser
from os.path import basename
from traceback import format_exception_only

from .lib import kos_repr
from .exceptions import LayoutError, ReaderError
from .reader import READ_EOF, LayoutReader, source_str

__all__ = (
    'main',
    'option_parser',
    'cli_async',
    'run_file_async',
    'repl_async',
    'REPLCodeBoundary',
    'format_exception_with_koskari_trace',
)


# ---------------------------------------------------------------------------
# Prompt backend selection
# ---------------------------------------------------------------------------


def _make_prompter(histfile=None, get_namespace=None):
    """
    Return a callable suitable for prompting the user in the REPL.

    Tries prompt_toolkit first (with lexer, completer, style), then
    readline, then falls back to the built-in input().  When histfile
    is provided and readline is used, the history file is loaded on
    startup and saved on exit.

    :param histfile: path to the readline history file, or None
    :param get_namespace: callable() -> namespace or None; for REPL
        symbol completion when using prompt_toolkit
    :return: a callable with the same signature as input(prompt)
    """

    try:
        from prompt_toolkit import PromptSession
        from prompt_toolkit.auto_suggest import AutoSuggestFromHistory
        from prompt_toolkit.history import FileHistory

        from koskari.lexer.discovery import special_names_for_repl
        from koskari.lexer.prompt_toolkit import (KoskariCompleter,
                                                  KoskariLexer, koskari_style,
                                                  load_koskari_key_bindings)

        repl_special_names = special_names_for_repl()
        history = FileHistory(histfile) if histfile else None
        completer = KoskariCompleter(
            get_namespace=get_namespace,
            special_names=repl_special_names)
        session = PromptSession(
            history=history,
            lexer=KoskariLexer(special_names=repl_special_names),
            completer=completer,
            style=koskari_style,
            auto_suggest=AutoSuggestFromHistory(),
            key_bindings=load_koskari_key_bindings())

        def _prompt(prompt, **kwargs):
            return session.prompt(prompt, reserve_space_for_menu=False, **kwargs)

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

    def _input_prompt(prompt, **kwargs):
        return input(prompt)

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


def _render_koskari_dynchain(trace_data):
    """
    Render dynchain metadata attached to synthetic koskari native frames.

    trace_data is built innermost-first by the C runtime; it is reversed
    before rendering so output matches Python's root-first traceback order.
    Non-dict entries in trace_data are silently skipped.

    :param trace_data: iterable of dict-like frame metadata
    :return: list[str] of traceback-formatted lines
    """

    if not isinstance(trace_data, (list, tuple)) or not trace_data:
        return []

    frames = list(trace_data)
    frames.reverse()
    lines = ['  Koskari dynchain (native eval):\n']
    for frame in frames:
        if not isinstance(frame, dict):
            continue

        name = frame.get('name', '<anonymous>')
        filename = frame.get('filename', '<unknown>')
        line = frame.get('line', 0)
        col = frame.get('col', 0)
        pc = frame.get('pc', 0)
        lines.append(f'    at {name} ({filename}:{line}:{col}) [pc={pc}]\n')

    return lines


def _find_dynchain_in_chain(exc_val):
    """
    Walk the exception chain (exc_val, __cause__, __context__) and return
    __koskari_dynchain__ from the first exception that has it.

    :param exc_val: exception value to start from
    :return: dynchain data or None
    """
    seen = set()
    to_visit = [exc_val]
    while to_visit:
        current = to_visit.pop()
        if current is None or id(current) in seen:
            continue
        seen.add(id(current))
        if hasattr(current, '__koskari_dynchain__'):
            trace_data = getattr(current, '__koskari_dynchain__', None)
            if trace_data is not None:
                return trace_data
        for attr in ('__cause__', '__context__'):
            link = getattr(current, attr, None)
            if link is not None:
                to_visit.append(link)
    return None


def format_exception_with_koskari_trace(exc_type, exc_val, tb, chain_root=None):
    """
    Format an exception traceback, interleaving koskari native dynchain data
    after synthetic native frames that carry ``__koskari_trace__`` in globals.
    Falls back to exc_val.__koskari_dynchain__ when no such frame exists.
    When chain_root is given, searches that exception chain for dynchain.

    Falls back to normal traceback formatting when no traceback is present.

    :param chain_root: optional exception to walk (__cause__/__context__) for dynchain
    :return: list[str], matching traceback.format_exception style
    """

    if tb is None:
        fallback = None
        trace_data = _find_dynchain_in_chain(chain_root if chain_root is not None else exc_val)
        if trace_data is not None:
            fallback = _render_koskari_dynchain(trace_data)
        if fallback:
            out = ['Traceback (most recent call last):\n']
            out.extend(fallback)
            out.extend(format_exception_only(exc_type, exc_val))
            return out
        return list(format_exception_only(exc_type, exc_val))

    found_dynchain = False
    for frame, _lineno in traceback.walk_tb(tb):
        if frame.f_globals.get('__koskari_trace__') is not None:
            found_dynchain = True
            break

    fallback_dynchain = None
    trace_data = _find_dynchain_in_chain(chain_root if chain_root is not None else exc_val)
    if not found_dynchain and trace_data is not None:
        fallback_dynchain = _render_koskari_dynchain(trace_data)

    if not found_dynchain and not fallback_dynchain:
        return traceback.format_exception(exc_type, exc_val, tb)

    out = ['Traceback (most recent call last):\n']
    extracted = traceback.extract_tb(tb)
    walked = list(traceback.walk_tb(tb))

    for summary, (frame, _lineno) in zip(extracted, walked):
        out.extend(traceback.format_list([summary]))
        trace_data = frame.f_globals.get('__koskari_trace__')
        out.extend(_render_koskari_dynchain(trace_data))

    if fallback_dynchain:
        out.extend(fallback_dynchain)

    out.extend(format_exception_only(exc_type, exc_val))
    return out


def _filter_infrastructure_tb(tb):
    """
    Remove infrastructure frames from a traceback.

    Walks the chain, drops frames whose __name__ is in
    REPL_INFRASTRUCTURE_MODULES, and relinks.  Returns the filtered head.

    Mutates the traceback chain in place by rewriting tb_next on existing
    traceback objects.  Code that holds references to the same traceback
    (e.g. debuggers, logging) will see the modified chain.  Python does not
    provide a straightforward way to build new traceback chains without
    mutation.

    :param tb: traceback object (root of chain)
    :return: filtered traceback, or original if all frames would be removed
    """

    if tb is None:
        return tb
    filtered = []
    current = tb
    while current is not None:
        frame = current.tb_frame
        mod = frame.f_globals.get('__name__', '')
        if mod not in REPL_INFRASTRUCTURE_MODULES:
            filtered.append(current)
        current = current.tb_next
    if not filtered:
        return None
    for i in range(len(filtered) - 1):
        filtered[i].tb_next = filtered[i + 1]
    filtered[-1].tb_next = None
    return filtered[0]


class REPLCodeBoundary(Exception):
    """
    Internal marker for the boundary between REPL infrastructure and user
    expression evaluation.  Never shown to the user.
    """


async def _boundary_call_async(fn, /, *a, **kw):
    """
    Await fn with the given arguments.  If an exception is raised, re-raise
    REPLCodeBoundary from it so the cause's traceback can be used for display
    (omitting REPL infrastructure frames).
    """

    try:
        return await fn(*a, **kw)
    except Exception as e:
        raise REPLCodeBoundary() from e


# ---------------------------------------------------------------------------
# REPL
# ---------------------------------------------------------------------------


_INCOMPLETE = object()


async def _eval_expression_async(compiler, source, ns):
    """
    Run one REPL expression.

    :param compiler: Compiler instance
    :param source: source string (may span multiple lines)
    :param ns: module namespace
    :return: eval result or _INCOMPLETE if parse returned None
    """

    reader = ns.get("__reader__")
    if reader is None:
        reader = LayoutReader(ns)
        ns["__reader__"] = reader

    stream = source_str(source, '<stdin>')
    source_expr = reader.read(stream)
    if source_expr is READ_EOF:
        return _INCOMPLETE

    fragment = compiler.full_compile(source_expr)
    expr = fragment.as_expression(ns)
    return await expr()


def _in_block_body(buffer):
    """
    True if buffer appears to be inside a layout block body (user may add more).

    We have a block header (some line ends with :) and the last non-empty
    line has leading whitespace.  A blank line will terminate the block.
    """

    if not buffer or ':' not in buffer:
        return False
    lines = buffer.splitlines()
    has_header = any(ln.rstrip().endswith(':') for ln in lines)
    if not has_header:
        return False
    for i in range(len(lines) - 1, -1, -1):
        ln = lines[i]
        if ln.strip():
            return ln and ln[0] in ' \t'
    return False


def _continuation_indent(buffer):
    """
    Return leading whitespace for the next continuation line.

    When buffer ends with a block header (colon), return indent of that
    line plus 4 spaces.  When in block body, return indent of last
    non-empty line (same level).  Otherwise return empty string.
    """

    if not buffer:
        return ''
    lines = buffer.splitlines()
    if not lines:
        return ''
    rstripped = buffer.rstrip()
    if rstripped.endswith(':'):
        last = lines[-1]
        indent_len = len(last) - len(last.lstrip())
        return ' ' * (indent_len + 4)
    if _in_block_body(buffer):
        for i in range(len(lines) - 1, -1, -1):
            ln = lines[i]
            if ln.strip():
                indent_len = len(ln) - len(ln.lstrip())
                return ' ' * indent_len
    return ''


async def repl_async(mod, prompter=None, histfile=None, stdin=None, stdout=None,
                     stderr=None):
    """
    Run an interactive read-eval-print loop for a koskari module.

    Each form entered by the user is compiled and executed.  Multi-line
    input is supported via continuation prompt.  Non-None results are
    printed to stdout.  The last result is stored as '_' in the namespace.

    :param mod: koskari Module to use as the execution context
    :param prompter: callable with the same signature as input(); when
        None, _make_prompter(histfile, get_namespace) is called
    :param histfile: path to history file for _make_prompter when
        prompter is None
    :param stdin: input stream, defaults to sys.stdin
    :param stdout: output stream, defaults to sys.stdout
    :param stderr: error stream, defaults to sys.stderr
    """

    from koskari.compile import Compiler
    from koskari.importer.module import attach_module_load_facets, init_module
    from koskari.reader import LayoutReader

    stdin = stdin or sys.stdin
    stdout = stdout or sys.stdout
    stderr = stderr or sys.stderr

    loop = asyncio.get_running_loop()

    # init_module registers compiler specials into the namespace.  When the
    # module already has __file__ set (e.g. after run_file_async), we preserve
    # it; otherwise use the sentinel '<stdin>' so the compiler has a filename.
    if not getattr(mod, '__file__', None):
        init_module(mod, '<stdin>')

    ns = vars(mod)
    if prompter is None:
        prompter = _make_prompter(histfile=histfile, get_namespace=lambda: ns)

    compiler = Compiler(
        getattr(mod, '__name__', '__main__'),
        mod,
        filename='<stdin>')

    reader = LayoutReader(mod)
    attach_module_load_facets(mod, compiler, reader)

    buffer = ''
    prompt = '>>> '

    while True:
        try:
            indent = _continuation_indent(buffer) if prompt == '... ' else ''
            line = await loop.run_in_executor(
                None,
                lambda p=prompt, d=indent: prompter(p, default=d))

        except KeyboardInterrupt:
            print('', file=stderr)
            stderr.flush()
            break

        except EOFError:
            print('', file=stderr)
            stderr.flush()
            break

        buffer = buffer + ('\n' + line if buffer else line)

        if not buffer.strip():
            buffer = ''
            continue

        line_is_blank = not line.strip()
        if not line_is_blank:
            if buffer.rstrip().endswith(':'):
                prompt = '... '
                continue
            if _in_block_body(buffer):
                prompt = '... '
                continue

        try:
            result = await _boundary_call_async(
                _eval_expression_async, compiler, buffer, ns)

            if result is _INCOMPLETE:
                prompt = '... '
                continue

            buffer = ''
            prompt = '>>> '

            ns.store('_', result) if hasattr(ns, 'store') else ns.__setitem__('_', result)

            if result is not None:
                print(kos_repr(result), file=stdout)

        except KeyboardInterrupt:
            print('', file=stderr)
            stderr.flush()

        except EOFError:
            print('', file=stderr)
            stderr.flush()
            break

        except SyntaxError:
            # also catches LayoutError
            exc_type, exc_val, _ = sys.exc_info()
            sys.last_type = exc_type
            sys.last_value = exc_val
            sys.last_traceback = None
            msg = ''.join(format_exception_only(exc_type, exc_val))
            print(msg, end='', file=stderr)
            stderr.flush()
            buffer = ''
            prompt = '>>> '

        except Exception:
            exc_type, exc_val, tb = sys.exc_info()
            if isinstance(exc_val, REPLCodeBoundary) and exc_val.__cause__ is not None:
                cause = exc_val.__cause__
                if isinstance(cause, ReaderError):
                    msg_str = getattr(cause, 'message', str(cause))
                    if msg_str and 'unexpected' in msg_str.lower() and 'eof' in msg_str.lower():
                        prompt = '... '
                        stdout.flush()
                        continue
                sys.last_type = type(cause)
                sys.last_value = cause

                if isinstance(cause, LayoutError):
                    msg = ''.join(format_exception_only(type(cause), cause))
                    sys.last_traceback = None
                else:
                    tb_filtered = _filter_infrastructure_tb(cause.__traceback__)
                    sys.last_traceback = tb_filtered
                    tb_for_format = tb_filtered if tb_filtered is not None else cause.__traceback__
                    msg = ''.join(format_exception_with_koskari_trace(
                        type(cause), cause, tb_for_format, chain_root=exc_val))
            else:
                sys.last_type = exc_type
                sys.last_value = exc_val
                sys.last_traceback = tb
                msg = ''.join(format_exception_with_koskari_trace(exc_type, exc_val, tb))
            print(msg, end='', file=stderr)
            stderr.flush()
            buffer = ''
            prompt = '>>> '

        stdout.flush()

    print('', file=stdout)


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

    from koskari.importer.module import (init_module, load_module_async,
                                         load_module_kio_async)

    init_module(mod, filename)
    if execute_kio:
        await load_module_kio_async(mod)
    else:
        await load_module_async(mod, interactive=interactive)


# ---------------------------------------------------------------------------
# CLI orchestration
# ---------------------------------------------------------------------------


async def cli_async(options):
    """
    Execute the koskari command-line session described by options.

    Installs the import hook, optionally tweaks sys.path, runs any
    specified file, then drops into the REPL when --interactive is given
    or no filename was provided.

    :param options: namespace returned by option_parser().parse_args()
    """

    filename = options.filename

    if options.compile_kio:
        from koskari.serialize.incremental import compile_ki_file_to_kio
        compile_ki_file_to_kio(
            filename,
            options.compile_kio,
            source_filename=options.compile_kio_source_name)
        return

    from koskari.importer import install
    from koskari.importer.module import new_module

    install()

    if filename:
        sys.argv = [filename] + list(options.args)
    else:
        sys.argv = list(options.args)

    if options.tweakpath:
        sys.path.insert(0, '.')

    mod = await new_module('__main__')

    if filename:
        await run_file_async(
            mod,
            filename,
            execute_kio=options.execute_kio,
            interactive=options.interactive)

        if not options.interactive:
            return

    await repl_async(mod, histfile=options.histfile)


# ---------------------------------------------------------------------------
# Argument parser
# ---------------------------------------------------------------------------


def option_parser(name):
    """
    Build and return the ArgumentParser for the koskari CLI.

    :param name: the program name (typically argv[0])
    :return: configured ArgumentParser instance
    """

    parser = ArgumentParser(prog=basename(name))

    parser.add_argument(
        '--no-tweak-path',
        dest='tweakpath',
        action='store_false',
        default=True,
        help="Do not add the current directory to sys.path")

    parser.add_argument(
        '--histfile',
        dest='histfile',
        action='store',
        default=None,
        help="REPL history file path")

    parser.add_argument(
        '-i', '--interactive',
        dest='interactive',
        action='store_true',
        default=False,
        help="Enter interactive mode after executing the given script")

    parser.add_argument(
        '--compile-kio',
        dest='compile_kio',
        metavar='OUT',
        default=None,
        help='Compile the given .ki file to OUT as .kio, then exit')

    parser.add_argument(
        '--compile-kio-source-name',
        dest='compile_kio_source_name',
        metavar='PATH',
        default=None,
        help='Logical .ki path stored in the .kio for tracebacks (default: input path)')

    parser.add_argument(
        '--execute-kio',
        dest='execute_kio',
        action='store_true',
        default=False,
        help='Run filename as a precompiled .kio (instead of .ki source)')

    parser.add_argument(
        'filename',
        nargs='?',
        default=None,
        help="Run filename as a script")

    parser.add_argument(
        'args',
        nargs=REMAINDER,
        default=[],
        help="Arguments forwarded to the script or REPL session")

    return parser


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def main(argv=None):
    """
    Entry point for the koskari command-line interface.

    :param argv: argument list; defaults to sys.argv when None
    :return: integer exit code
    """

    if argv is None:
        argv = sys.argv

    name, *args = argv
    parser = option_parser(name)
    options = parser.parse_args(args)

    if options.compile_kio and not options.filename:
        parser.error('--compile-kio requires a .ki source file argument')
    if options.compile_kio_source_name and not options.compile_kio:
        parser.error(
            '--compile-kio-source-name requires --compile-kio')
    if options.execute_kio and not options.filename:
        parser.error('--execute-kio requires a script filename')

    try:
        asyncio.run(cli_async(options))

    except KeyboardInterrupt:
        print('', file=sys.stderr)
        return 130

    return 0


# The end.
