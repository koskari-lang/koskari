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
tox-guard - PreToolUse hook blocking agent misuse of tox

Denied: python -c probes routed through tox (after ``--``), and the
-r/--recreate options. Anything else falls through to the normal
permission flow.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import json
import re
import shlex
import sys


SEGMENT_SPLIT_RE = re.compile(r'[ \t]*(?:&&|\|\||;|\||\n)[ \t\n]*')


def deny(reason):
    """
    Emit a PreToolUse denial and exit
    """

    print(json.dumps({
        'hookSpecificOutput': {
            'hookEventName': 'PreToolUse',
            'permissionDecision': 'deny',
            'permissionDecisionReason': reason,
        },
    }))
    sys.exit(0)


def proceed():
    """
    Exit without a decision, deferring to the normal permission flow
    """

    sys.exit(0)


def tox_options_and_tail(argv):
    """
    Split a tox argv into option tokens and post-``--`` tail
    """

    rest = argv[1:]
    if '--' in rest:
        sep = rest.index('--')
        return rest[:sep], rest[sep + 1:]
    return rest, []


def has_recreate(options):
    """
    Return True when tox options request env recreation
    """

    for token in options:
        if token == '--recreate':
            return True
        if token.startswith('-') and not token.startswith('--'):
            if 'r' in token[1:]:
                return True
    return False


def has_python_c_via_tox(tail):
    """
    Return True when the post-``--`` tail is a python -c probe
    """

    if not tail:
        return False
    if tail[0] == '-c':
        return True
    if tail[0] in ('python', 'python2', 'python3') and len(tail) > 1:
        if tail[1] == '-c':
            return True
    return False


def tox_invocations(tokens):
    """
    Yield argv slices for each tox invocation in a token list
    """

    idx = 0
    while idx < len(tokens):
        if tokens[idx] != 'tox':
            idx += 1
            continue

        end = idx + 1
        while end < len(tokens):
            if tokens[end] in {'&&', '||', ';', '|'}:
                break
            end += 1

        yield tokens[idx:end]
        idx = end


def inspect_argv(argv):
    """
    Validate one tox argv list
    """

    options, tail = tox_options_and_tail(argv)

    if has_recreate(options):
        quoted = ' '.join(shlex.quote(token) for token in argv)
        deny(
            'Do not pass -r or --recreate to tox; diagnose the underlying'
            ' issue instead. Blocked: %s' % quoted)

    if has_python_c_via_tox(tail):
        quoted = ' '.join(shlex.quote(token) for token in argv)
        deny(
            'Use uv run python3 or uv run koskari for short probes, not tox'
            ' as a python launcher. Blocked: %s' % quoted)


def inspect_segment(segment):
    """
    Tokenize one shell segment and validate tox usage
    """

    segment = segment.strip()
    if not segment or 'tox' not in segment:
        return

    try:
        tokens = shlex.split(segment)
    except ValueError:
        deny(
            'Unparseable shell command containing tox was blocked. Split it'
            ' into separate calls.')

    for argv in tox_invocations(tokens):
        inspect_argv(argv)


def main():
    """
    PreToolUse hook entry point
    """

    try:
        payload = json.load(sys.stdin)
    except ValueError:
        proceed()

    if payload.get('tool_name') != 'Bash':
        proceed()

    command = payload.get('tool_input', {}).get('command', '')
    if not command or 'tox' not in command:
        proceed()

    for segment in SEGMENT_SPLIT_RE.split(command):
        inspect_segment(segment)

    proceed()


if __name__ == '__main__':
    main()


# The end.
