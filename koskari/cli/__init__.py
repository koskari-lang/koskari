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
"""

__all__ = (
    'main',
)


def main(argv=None):
    """
    Entry point for the koskari command-line interface.

    Boots the Koskari launchpad (``koskari.cli.launch:run``) through the
    launcher, the same path a packaged koskari_scripts command uses: the
    target is loaded and run under Domain and the async host.  Option
    parsing and orchestration live in Koskari (``launch.ki``); this stub
    only dispatches and maps a top-level Ctrl-C to exit code 130.

    :param argv: argument list; defaults to sys.argv when None
    :return: integer exit code
    """

    import sys
    from koskari.importer import install
    from koskari.launcher import drive_async_loop, hosted_run
    from koskari.exceptions import apply_sys_last_exc, format_run_exception

    install()

    if argv is None:
        argv = sys.argv

    try:
        res = drive_async_loop(
            hosted_run(
                "koskari.cli.launch",
                "run",
                argv=list(argv[1:]),
                script_name=argv[0]))

    except KeyboardInterrupt:
        print('', file=sys.stderr)
        return 130

    except Exception as e:
        # Boundary cleaning for a top-level run: strip koskari machinery
        # frames and interleave the native dynchain, the same way the REPL
        # renders a failed form.  SystemExit and other BaseExceptions
        # propagate so scripts control their own exit code.
        lines, tb = format_run_exception(e)
        apply_sys_last_exc(type(e), e, tb)
        sys.stderr.write(''.join(lines))
        return 1

    return res or 0


# The end.
