#!/usr/bin/env python3
"""
Allow only a small git command set for agent shell execution.

Permitted: log, status, show, diff, and commit via heredoc (-F - <<'EOF').
"""

import json
import re
import shlex
import sys


ALLOWED_SUBCMDS = frozenset({
    'add',
    'diff',
    'mv',
    'rm',
    'log',
    'show',
    'status',
})

GLOBAL_OPTS_WITH_ARG = frozenset({
    '-C',
    '-c',
    '--git-dir',
    '--work-tree',
})

GLOBAL_OPTS = frozenset({
    '--no-pager',
    '--no-optional-locks',
})

SEGMENT_SPLIT_RE = re.compile(r'\s*(?:&&|\|\||;|\|)\s*')
COMMIT_HEREDOC_RE = re.compile(r"<<\s*'EOF'")
GIT_WORD_RE = re.compile(r'(^|[\s;&|])git([\s;&|]|$)')
FORBIDDEN_COMMIT_RE = re.compile(
    r'\b(-a|-m|-p|--all|--amend|--message|--no-edit|--only|--patch)\b')

FORBIDDEN_COMMIT_FLAGS = frozenset({
    '-a',
    '-m',
    '-p',
    '--all',
    '--amend',
    '--message',
    '--no-edit',
    '--only',
    '--patch',
})


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


def skip_git_globals(args):
    """
    Skip git global options and return the remaining argv
    """

    idx = 0
    while idx < len(args):
        token = args[idx]
        if token in GLOBAL_OPTS:
            idx += 1
            continue
        if token in GLOBAL_OPTS_WITH_ARG:
            idx += 2
            continue
        if token.startswith('-C'):
            idx += 1
            continue
        if '=' in token and token.split('=', 1)[0] in {'--git-dir', '--work-tree'}:
            idx += 1
            continue
        break
    return args[idx:]


def is_commit_heredoc_segment(segment):
    """
    Return True for git commit -F - <<'EOF' ... EOF on the raw shell text
    """

    if not GIT_WORD_RE.search(segment):
        return False
    if not re.search(r'\bcommit\b', segment):
        return False
    if FORBIDDEN_COMMIT_RE.search(segment):
        return False
    if not re.search(r'-F\s+-', segment):
        return False
    if not COMMIT_HEREDOC_RE.search(segment):
        return False

    return True


def is_allowed_commit(argv):
    """
    Return True only for git commit -F - argv (heredoc checked separately)
    """

    rest = skip_git_globals(argv[1:])
    if not rest or rest[0] != 'commit':
        return False

    tail = rest[1:]
    if any(token in FORBIDDEN_COMMIT_FLAGS for token in tail):
        return False
    if '-F' not in tail:
        return False

    f_idx = tail.index('-F')
    if f_idx + 1 >= len(tail) or tail[f_idx + 1] != '-':
        return False

    return True


def is_allowed_git(argv):
    """
    Return True when a git argv list is on the project allowlist
    """

    if not argv or argv[0] != 'git':
        return True

    rest = skip_git_globals(argv[1:])
    if not rest:
        return False

    subcmd = rest[0]
    if subcmd in ALLOWED_SUBCMDS:
        return True
    if subcmd == 'commit':
        return is_allowed_commit(argv)

    return False


def git_invocations(tokens):
    """
    Yield argv slices for each git invocation in a token list
    """

    idx = 0
    while idx < len(tokens):
        if tokens[idx] != 'git':
            idx += 1
            continue

        end = idx + 1
        while end < len(tokens):
            if tokens[end] in {'&&', '||', ';', '|'}:
                break
            end += 1

        yield tokens[idx:end]
        idx = end


def inspect_segment(segment):
    """
    Tokenize one shell segment and validate git usage
    """

    segment = segment.strip()
    if not segment or 'git' not in segment:
        return True

    if is_commit_heredoc_segment(segment):
        return True

    try:
        tokens = shlex.split(segment)
    except ValueError:
        deny(
            'Unparseable shell command containing git was blocked.',
            'Shell segment could not be parsed; git usage denied.',
        )

    for argv in git_invocations(tokens):
        if not is_allowed_git(argv):
            quoted = ' '.join(shlex.quote(token) for token in argv)
            deny(
                "Git commands are heavily restricted due to Agents demonstrable"
                "inability to follow project-level rules.",
                'Blocked git command: %s' % quoted,
            )

    return True


def main():
    """
    beforeShellExecution hook entry point
    """

    payload = json.load(sys.stdin)
    command = payload.get('command', '')
    if not command or 'git' not in command:
        allow()

    for segment in SEGMENT_SPLIT_RE.split(command):
        inspect_segment(segment)

    allow()


if __name__ == '__main__':
    main()
