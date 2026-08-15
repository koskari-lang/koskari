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
git-readonly - PreToolUse hook restricting agent git usage

Permits log, status, show, diff, add, mv, rm, commit via heredoc
(-F - <<'EOF'), and guarded worktree add/list/remove/prune beneath a
Claude worktree root. Everything else, push above all, is denied.
Permitted commands fall through to the normal permission flow rather
than being auto-approved here.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import json
import os
import re
import shlex
import sys


ALLOWED_SUBCMDS = frozenset({
    'add',
    'blame',
    'cat-file',
    'describe',
    'diff',
    'grep',
    'log',
    'ls-files',
    'mv',
    'rev-parse',
    'rm',
    'shortlog',
    'show',
    'status',
})

WORKTREE_ROOTS = (
    os.path.join(
        os.environ.get('CLAUDE_PROJECT_DIR', os.getcwd()),
        '.claude', 'worktrees'),
    os.path.expanduser('~/.claude/worktrees'),
)

WORKTREE_ADD_FORBIDDEN_FLAGS = frozenset({
    '-b',
    '-B',
    '--track',
    '-f',
    '--force',
    '--checkout',
    '--lock',
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

SEGMENT_SPLIT_RE = re.compile(r'[ \t]*(?:&&|\|\||;|\||\n)[ \t\n]*')
COMMIT_HEREDOC_RE = re.compile(r"<<\s*'EOF'")
HEREDOC_OPEN_RE = re.compile(r"<<-?\s*'([A-Za-z_][A-Za-z0-9_]*)'")
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


def strip_heredoc_bodies(command):
    """
    Remove quoted heredoc bodies, leaving the shell text around them

    A heredoc body is data, not shell. Splitting it into segments makes
    prose in a commit message look like chained commands, so it is cut
    away before any analysis. The opening redirection itself is kept, so
    the commit form remains recognizable.

    :param command: the raw shell command text
    :return: the command with every quoted heredoc body removed
    """

    lines = command.split('\n')
    kept = []
    idx = 0

    while idx < len(lines):
        line = lines[idx]
        kept.append(line)
        idx += 1

        delims = HEREDOC_OPEN_RE.findall(line)
        for delim in delims:
            while idx < len(lines) and lines[idx].strip() != delim:
                idx += 1
            if idx < len(lines):
                idx += 1

    return '\n'.join(kept)


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
        if '=' in token:
            head = token.split('=', 1)[0]
            if head in {'--git-dir', '--work-tree'}:
                idx += 1
                continue
        break
    return args[idx:]


def is_commit_heredoc_segment(segment):
    """
    Return True for git commit -F - <<'EOF' ... EOF on raw shell text
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
    Return True only for git commit -F - argv
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


def is_worktree_path(path):
    """
    Return True when path is a nested directory under a worktree root
    """

    try:
        resolved = os.path.realpath(os.path.expanduser(path))
    except (OSError, TypeError, ValueError):
        return False

    for candidate in WORKTREE_ROOTS:
        try:
            root = os.path.realpath(candidate)
        except (OSError, TypeError, ValueError):
            continue

        prefix = root + os.sep
        if not resolved.startswith(prefix):
            continue

        rel = os.path.relpath(resolved, root)
        parts = [part for part in rel.split(os.sep) if part]
        if parts:
            return True

    return False


def git_worktree_list_check(tail):
    """
    Return True for git worktree list [--porcelain]
    """

    return tail in ([], ['--porcelain'])


def git_worktree_add_check(tail):
    """
    Return True for git worktree add --detach <worktree-path> [<ref>]
    """

    if '--detach' not in tail:
        return False

    positionals = []
    for token in tail:
        if token == '--detach':
            continue
        if token in WORKTREE_ADD_FORBIDDEN_FLAGS:
            return False
        if token.startswith('-'):
            return False
        positionals.append(token)

    if not positionals:
        return False

    return is_worktree_path(positionals[0])


def git_worktree_remove_check(tail):
    """
    Return True for git worktree remove [-f|--force] <worktree-path>
    """

    filtered = [token for token in tail if token not in ('-f', '--force')]
    if len(filtered) != 1:
        return False

    return is_worktree_path(filtered[0])


def git_worktree_check(argv):
    """
    Return True only for guarded git worktree subcommands
    """

    rest = skip_git_globals(argv[1:])
    if len(rest) < 2 or rest[0] != 'worktree':
        return False

    subcmd = rest[1]
    tail = rest[2:]

    if subcmd == 'list':
        return git_worktree_list_check(tail)
    if subcmd == 'add':
        return git_worktree_add_check(tail)
    if subcmd == 'remove':
        return git_worktree_remove_check(tail)
    if subcmd == 'prune':
        return not tail

    return False


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
    if subcmd == 'worktree':
        return git_worktree_check(argv)

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
        return

    if is_commit_heredoc_segment(segment):
        return

    try:
        tokens = shlex.split(segment)
    except ValueError:
        deny(
            'Unparseable shell command containing git was blocked. Split it'
            ' into separate calls.')

    for argv in git_invocations(tokens):
        if not is_allowed_git(argv):
            quoted = ' '.join(shlex.quote(token) for token in argv)
            deny(
                'Git is restricted to reads plus add, mv, rm, and heredoc'
                ' commit. Pushing, branch creation, and history rewriting'
                ' belong to the user alone. Blocked: %s' % quoted)


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
    if not command or 'git' not in command:
        proceed()

    command = strip_heredoc_bodies(command)
    if 'git' not in command:
        proceed()

    for segment in SEGMENT_SPLIT_RE.split(command):
        inspect_segment(segment)

    proceed()


if __name__ == '__main__':
    main()


# The end.
