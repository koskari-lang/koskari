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
from argparse import REMAINDER, Action, ArgumentParser
from os.path import basename

from ..exceptions import (CodeBoundary, CompilerError, ReaderEOF, ReaderError,
                          ReaderSyntaxError, UncaughtCompilerError,
                          apply_sys_last_exc, format_code_boundary_exception,
                          format_exception_with_koskari_trace)
from ..lexer.discovery import special_names_for_repl
from ..lexer.prompt_toolkit import (KoskariCompleter, KoskariLexer,
                                    koskari_style, load_koskari_key_bindings)
from ..lib import Domain, ParamRef, drive_async_loop, kos_repr
from ..module import build_module_load_facets, init_module, new_module
from ..reader import READ_EOF, LayoutReader, source_str


__all__ = (
    'CodeBoundary',
    'REPLCodeBoundary',
    '_await_user_form',
    'cli_async',
    'format_code_boundary_exception',
    'format_exception_with_koskari_trace',
    'main',
    'option_parser',
    'prepare_cli_async',
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
            return session.prompt(
                prompt,
                reserve_space_for_menu=False,
                **kwargs)

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


class REPLCodeBoundary(CodeBoundary):
    """
    REPL eval boundary between infrastructure and user expression evaluation
    """

    pass


# ---------------------------------------------------------------------------
# REPL
# ---------------------------------------------------------------------------


class REPLReaderError(ReaderError):
    """
    Reader error in the REPL
    """


_INCOMPLETE = object()


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
    try:
        source_expr = reader.read(stream)

    except ReaderEOF:
        return _INCOMPLETE

    except ReaderSyntaxError as e:
        raise REPLReaderError(e.message) from e

    if source_expr is READ_EOF:
        return _INCOMPLETE

    try:
        fragment = compiler.full_compile(source_expr)
        expr = fragment.as_expression(ns)
        return await expr()
    except (UncaughtCompilerError, CompilerError):
        raise
    except Exception as e:
        raise REPLCodeBoundary() from e


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
                     stderr=None, full_trace=False, quiet=False):
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
    :param full_trace: when True, show REPL boundary wrapper frames in errors
    :param quiet: when True, suppress REPL startup version banner
    """

    stdin = stdin or sys.stdin
    stdout = stdout or sys.stdout
    stderr = stderr or sys.stderr

    loop = asyncio.get_running_loop()

    # init_module registers compiler specials into the namespace.  When the
    # module already has __file__ set (e.g. after run_file_async), we preserve
    # it; otherwise use the sentinel '<stdin>' so the compiler has a filename.
    compiler = getattr(mod, '__compiler__', None)
    if compiler is None:
        init_module(mod, '<stdin>')
        compiler, _reader = build_module_load_facets(mod, filename="<stdin>")

    ns = vars(mod)
    if prompter is None:
        prompter = _make_prompter(histfile=histfile, get_namespace=lambda: ns)

    if not quiet:
        for line in startup_lines():
            print(line, file=stdout)
        print('', file=stdout)

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
            if buffer.strip():
                buffer = ''
                prompt = '>>> '
                continue
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
            result = await _eval_expression_async(compiler, buffer, ns)

            if result is _INCOMPLETE:
                prompt = '... '
                continue

            buffer = ''
            prompt = '>>> '

            ns['_'] = result
            if result is not None:
                print(kos_repr(result), file=stdout)

        except KeyboardInterrupt:
            print("", file=stderr)
            stderr.flush()
            buffer = ''
            prompt = '>>> '

        except EOFError:
            print('', file=stderr)
            stderr.flush()
            break

        except REPLReaderError as e:
            orig = e.__cause__ or e
            print(f"{type(orig).__name__}: {orig.message}", file=stderr)
            stderr.flush()
            buffer = ''
            prompt = '>>> '

        except REPLCodeBoundary as boundary:
            lines, last_type, last_val, last_tb = format_code_boundary_exception(
                boundary,
                nofold=full_trace,
                infrastructure_modules=REPL_INFRASTRUCTURE_MODULES)
            apply_sys_last_exc(last_type, last_val, last_tb)
            print(''.join(lines), end='', file=stderr)
            stderr.flush()
            buffer = ''
            prompt = '>>> '

        except UncaughtCompilerError as exc:
            lines = format_exception_with_koskari_trace(
                type(exc), exc, exc.__traceback__)
            apply_sys_last_exc(type(exc), exc, exc.__traceback__)
            print(''.join(lines), end='', file=stderr)
            stderr.flush()
            buffer = ''
            prompt = '>>> '

        except CompilerError as exc:
            apply_sys_last_exc(type(exc), exc, None)
            print(exc.explain(), end='', file=stderr)
            stderr.flush()
            buffer = ''
            prompt = '>>> '

        except Exception:
            exc_type, exc_val, tb = sys.exc_info()
            lines = format_exception_with_koskari_trace(exc_type, exc_val, tb)
            apply_sys_last_exc(exc_type, exc_val, tb)
            print(''.join(lines), end='', file=stderr)
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
    command = options.command

    if options.compile_kio:
        from koskari.serialize.incremental import compile_ki_file_to_kio
        compile_ki_file_to_kio(
            filename,
            options.compile_kio,
            source_filename=options.compile_kio_source_name)
        return

    if command is not None:
        sys.argv = ['-c'] + list(options.args)
    elif filename:
        sys.argv = [filename] + list(options.args)
    else:
        sys.argv = list(options.args)

    if options.tweakpath:
        sys.path.insert(0, '.')

    with Domain():
        mod = new_module('__main__')
        p = ParamRef("*interactive*")
        p.store(options.interactive)

        if command is not None:
            await run_command_async(
                mod,
                command,
                interactive=options.interactive)

            if not options.interactive:
                return

        elif filename:
            await run_file_async(
                mod,
                filename,
                execute_kio=options.execute_kio,
                interactive=options.interactive)

            if not options.interactive:
                return

        await repl_async(
            mod,
            histfile=options.histfile,
            full_trace=options.repl_full_trace,
            quiet=options.quiet)


# ---------------------------------------------------------------------------
# Argument parser
# ---------------------------------------------------------------------------


class _CompileKioSourceNameAction(Action):
    """
    Reject --compile-kio-source-name unless --compile-kio is also present
    """

    def __call__(self, parser, namespace, values, option_string=None):
        if namespace.compile_kio is None:
            parser.error('--compile-kio-source-name requires --compile-kio')
        setattr(namespace, self.dest, values)


def _reject_option_like_rest(parser, rest):
    """
    Error when pre-``--`` REMAINDER tokens look like misplaced options
    """

    for item in rest:
        if item == '--' or item.startswith('-'):
            parser.error('unrecognized arguments: %s' % (item,))


def _split_script_rest(parser, rest):
    """
    Split REMAINDER into script filename and forwarded script arguments

    When ``rest`` contains ``--``, only tokens before the first ``--`` are
    checked for misplaced koskari options; everything after is forwarded
    verbatim to the script.

    :param parser: ArgumentParser used for parse_args (for error reporting)
    :param rest: REMAINDER tokens after koskari CLI options
    :return: (filename, script_args) pair
    """

    if '--' not in rest:
        _reject_option_like_rest(parser, rest)
        return rest[0], rest[1:]

    idx = rest.index('--')
    pre = rest[:idx]
    post = rest[idx + 1:]

    if not pre:
        parser.error('expected script filename before --')

    _reject_option_like_rest(parser, pre)
    return pre[0], pre[1:] + post


def _resolve_cli_target(parser, options):
    """
    Flatten the exclusive run-target group into legacy option fields

    Script paths and forwarded arguments live in ``rest`` (REMAINDER) so
    ``-c`` can accept arguments without competing with an optional filename
    positional.

    :param parser: ArgumentParser used for parse_args (for error reporting)
    :param options: namespace returned by parse_args
    :return: the same namespace, with compile_kio, filename, and execute_kio set
    """

    rest = list(options.rest)

    if options.command is not None:
        options.filename = None
        if rest and rest[0] == '--':
            rest = rest[1:]
        options.args = rest
        options.execute_kio = False
        return options

    if options.compile_kio is not None:
        if rest:
            parser.error(
                'unexpected extra arguments: %s' % (' '.join(rest),))
        out, src = options.compile_kio
        options.compile_kio = out
        options.filename = src
        options.args = []
        options.execute_kio = False
        return options

    if options.execute_kio_file is not None:
        if rest:
            parser.error(
                'unexpected extra arguments: %s' % (' '.join(rest),))
        options.execute_kio = True
        options.filename = options.execute_kio_file[0]
        options.args = []
        return options

    if rest:
        options.filename, options.args = _split_script_rest(parser, rest)
    else:
        options.filename = None
        options.args = []

    options.execute_kio = False
    return options


def option_parser(name):
    """
    Build and return the ArgumentParser for the koskari CLI.

    :param name: the program name (typically argv[0])
    :return: configured ArgumentParser instance
    """

    parser = ArgumentParser(prog=basename(name))

    parser.add_argument(
        '--version',
        dest='show_version',
        action='store_true',
        default=False,
        help='Print Koskari and Python version information, then exit')

    parser.add_argument(
        '-q', '--quiet',
        dest='quiet',
        action='store_true',
        default=False,
        help='Suppress REPL startup version banner')

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
        '--repl-full-trace',
        dest='repl_full_trace',
        action='store_true',
        default=False,
        help='Show REPL infrastructure frames in runtime error tracebacks')

    parser.add_argument(
        '-i', '--interactive',
        dest='interactive',
        action='store_true',
        default=False,
        help="Enter interactive mode after executing the given script")

    target = parser.add_mutually_exclusive_group()

    target.add_argument(
        '-c', '--command',
        dest='command',
        metavar='CMD',
        default=None,
        help='Program passed in as a string, then exit')

    target.add_argument(
        '--compile-kio',
        dest='compile_kio',
        nargs=2,
        metavar=('OUT', 'SRC'),
        default=None,
        help='Compile SRC .ki to OUT as .kio, then exit')

    target.add_argument(
        '--execute-kio',
        dest='execute_kio_file',
        metavar='SRC',
        nargs=1,
        default=None,
        help='Run SRC as a precompiled .kio')

    parser.add_argument(
        '--compile-kio-source-name',
        dest='compile_kio_source_name',
        metavar='PATH',
        default=None,
        action=_CompileKioSourceNameAction,
        help='Logical .ki path stored in the .kio for tracebacks (default: input path)')

    parser.add_argument(
        'rest',
        nargs=REMAINDER,
        default=[],
        help="Script path and arguments forwarded to the script or REPL session")

    return parser


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def prepare_cli_async(argv=None):
    """
    Parse CLI arguments and return either a sync exit code or async coroutine

    :param argv: argument list; defaults to sys.argv when None
    :return: (exit_code, coroutine) pair; exactly one of exit_code or
        coroutine is non-None
    """

    if argv is None:
        argv = sys.argv

    name, *args = argv
    parser = option_parser(name)
    options = _resolve_cli_target(parser, parser.parse_args(args))

    if options.show_version:
        print(version_line())
        print('')
        print('Python %s' % (sys.version,))
        return 0, None

    return None, cli_async(options)


def main(argv=None):
    """
    Entry point for the koskari command-line interface.

    :param argv: argument list; defaults to sys.argv when None
    :return: integer exit code
    """

    try:
        exit_code, coro = prepare_cli_async(argv)
        if exit_code is not None:
            return exit_code

        drive_async_loop(coro)

    except KeyboardInterrupt:
        print('', file=sys.stderr)
        return 130

    return 0


# The end.
