#!/usr/bin/env python3
"""
Block common agent misuse of tox in shell commands.

Denied: python -c probes via tox (after ``--``), and -r/--recreate.
"""

import json
import re
import shlex
import sys


SEGMENT_SPLIT_RE = re.compile(r'\s*(?:&&|\|\||;|\|)\s*')


def deny(user_message, agent_message):
    """
    Emit a hook denial response and exit blocked
    """

    print(json.dumps({
        'permission': 'deny',
        'user_message': user_message,
        'agent_message': agent_message,
    }))
    sys.exit(2)


def allow():
    """
    Emit a hook allow response
    """

    print(json.dumps({'permission': 'allow'}))
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
            'Do not pass -r or --recreate to tox; fix the underlying issue.',
            'Blocked tox recreate: %s' % quoted,
        )

    if has_python_c_via_tox(tail):
        quoted = ' '.join(shlex.quote(token) for token in argv)
        deny(
            'Use uv run python3 or uv run koskari for short probes, not tox -c.',
            'Blocked tox used as python: %s' % quoted,
        )


def inspect_segment(segment):
    """
    Tokenize one shell segment and validate tox usage
    """

    segment = segment.strip()
    if not segment or 'tox' not in segment:
        return True

    try:
        tokens = shlex.split(segment)
    except ValueError:
        deny(
            'Unparseable shell command containing tox was blocked.',
            'Shell segment could not be parsed; tox usage denied.',
        )

    for argv in tox_invocations(tokens):
        inspect_argv(argv)

    return True


def main():
    """
    beforeShellExecution hook entry point
    """

    payload = json.load(sys.stdin)
    command = payload.get('command', '')
    if not command or 'tox' not in command:
        allow()

    for segment in SEGMENT_SPLIT_RE.split(command):
        inspect_segment(segment)

    allow()


if __name__ == '__main__':
    main()
