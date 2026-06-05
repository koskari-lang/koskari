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
tools.regen_version

Regenerate koskari/_version.py from setup.cfg metadata and git state.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Composer via Cursor
"""


import argparse
import os
import subprocess
import sys
from configparser import ConfigParser
from datetime import datetime, timezone
from pathlib import Path


def project_root(root=None):
    """
    Return the repository root path

    :param root: optional override; defaults to parent of tools/
    :return: Path to repository root
    """

    if root is not None:
        return Path(root).resolve()
    return Path(__file__).resolve().parent.parent


def read_version(root=None):
    """
    Read the version from setup.cfg metadata

    :param root: repository root path
    :return: version string
    """

    root = project_root(root)
    conf = ConfigParser()
    conf.read([str(root / 'setup.cfg')])
    return conf['metadata']['version']


def git_state(root=None):
    """
    Collect git metadata for version assembly

    :param root: repository root path
    :return: dict with commit, exact_tag, and dirty keys
    """

    result = {
        'commit': None,
        'exact_tag': None,
        'dirty': False,
    }

    root = project_root(root)
    git_dir = root / '.git'
    if not git_dir.exists():
        return result

    try:
        commit_proc = subprocess.run(
            ['git', 'rev-parse', '--short', 'HEAD'],
            cwd=str(root),
            check=False,
            capture_output=True,
            text=True)
        if commit_proc.returncode == 0:
            result['commit'] = commit_proc.stdout.strip()

        tag_proc = subprocess.run(
            ['git', 'describe', '--exact-match', '--tags', 'HEAD'],
            cwd=str(root),
            check=False,
            capture_output=True,
            text=True)
        if tag_proc.returncode == 0:
            result['exact_tag'] = tag_proc.stdout.strip()

        dirty_proc = subprocess.run(
            ['git', 'diff-index', '--quiet', 'HEAD', '--'],
            cwd=str(root),
            check=False)
        result['dirty'] = dirty_proc.returncode != 0

    except OSError:
        pass

    return result


def parse_bool(value):
    """
    Interpret common truthy string values

    :param value: string from an environment variable
    :return: True for truthy values, False otherwise
    """

    return value.strip().lower() in ('1', 'true', 'yes', 'on')


def version_state(root=None):
    """
    Collect git metadata, optionally overridden by environment

    When KOSKARI_FORCE_VERSION_REGEN is set, KOSKARI_COMMIT,
    KOSKARI_DIRTY, and KOSKARI_EXACT_TAG replace git-derived values.

    :param root: repository root path
    :return: dict with commit, exact_tag, and dirty keys
    """

    state = git_state(root)

    if os.environ.get('KOSKARI_FORCE_VERSION_REGEN') != '1':
        return state

    if 'KOSKARI_COMMIT' in os.environ:
        val = os.environ['KOSKARI_COMMIT'].strip()
        state['commit'] = val or None

    if 'KOSKARI_DIRTY' in os.environ:
        state['dirty'] = parse_bool(os.environ['KOSKARI_DIRTY'])

    if 'KOSKARI_EXACT_TAG' in os.environ:
        val = os.environ['KOSKARI_EXACT_TAG'].strip()
        state['exact_tag'] = val or None

    return state


def version_info(root=None):
    """
    Assemble full version metadata from setup.cfg and version state

    :param root: repository root path
    :return: dict from assemble_version
    """

    root = project_root(root)
    version = read_version(root)
    return assemble_version(version, version_state(root))


def full_version(info):
    """
    Render the koskari __version__ string from assembled metadata

    :param info: dict from assemble_version
    :return: version string with release qualifier
    """

    return info['version'] + '+' + info['release']


def report_env(root=None):
    """
    Render shell-sourceable KOSKARI_* assignments from git state

    :param root: repository root path
    :return: multi-line string of KEY=value assignments
    """

    proj = project_root()
    version = read_version(proj)
    state = git_state(root)
    info = assemble_version(version, state)

    commit = state.get('commit') or ''
    dirty = '1' if state.get('dirty') else '0'
    exact_tag = state.get('exact_tag') or ''

    lines = [
        'KOSKARI_COMMIT=' + commit,
        'KOSKARI_DIRTY=' + dirty,
        'KOSKARI_EXACT_TAG=' + exact_tag,
        'KOSKARI_VERSION=' + full_version(info),
        '',
    ]
    return '\n'.join(lines)


def report_tag(root=None):
    """
    Render an OCI-safe image tag from git-derived version metadata

    :param root: repository root path
    :return: tag string with plus signs replaced by hyphens
    """

    root = project_root(root)
    version = read_version(root)
    info = assemble_version(version, git_state(root))
    return full_version(info).replace('+', '-')


def assemble_version(version, state, now=None):
    """
    Build version metadata from release string and git state

    :param release: base release from setup.cfg
    :param state: dict from git_state
    :param now: optional datetime for dirty local segments (tests)
    :return: dict with version, release, build_kind, commit, dirty
    """

    commit = state.get('commit')
    exact_tag = state.get('exact_tag')
    dirty = bool(state.get('dirty'))
    expected_tag = 'v' + version

    if (not dirty
            and exact_tag == expected_tag
            and commit is not None):
        build_kind = 'release'
        release = 'ga'
    elif commit is None:
        build_kind = 'release'
        release = 'ga'
    else:
        build_kind = 'dev'
        release = 'dev+' + commit
        if dirty:
            if now is None:
                now = datetime.now(timezone.utc)
            stamp = now.strftime('%Y%m%dT%H%M%S')
            release = release + '+' + stamp

    major, minor, micro = version.split('.')

    version_info = (int(major), int(minor), int(micro), release)

    return {
        'version': version,
        'major': major,
        'minor': minor,
        'micro': micro,
        'release': release,
        'version_info': version_info,
        'build_kind': build_kind,
        'commit': commit,
        'dirty': dirty,
    }


def render_version_py(info, template_path):
    """
    Render _version.py from template and version info dict

    :param info: dict from assemble_version
    :param template_path: path to _version.py.in
    :return: rendered file contents
    """

    template = template_path.read_text()
    return template % info


def should_regen(root=None):
    """
    Whether regen_version_py should run in the current context

    :param root: repository root path
    :return: True when regeneration should proceed
    """

    if os.environ.get('KOSKARI_FORCE_VERSION_REGEN') == '1':
        return True

    root = project_root(root)

    if not (root / 'koskari' / '_version.py').exists():
        return True

    return (root / '.git').exists()


def regen_version_py(root=None, dest=None):
    """
    Regenerate koskari/_version.py when inputs differ from output

    :param root: repository root path
    :param dest: optional output path override
    :return: True when the file was written or updated
    """

    root = project_root(root)

    info = version_info(root)

    template_path = root / 'koskari' / '_version.py.in'
    output_path = Path(dest) if dest else root / 'koskari' / '_version.py'
    rendered = render_version_py(info, template_path)

    output_path.write_text(rendered)


def main(argv=None):
    """
    Regenerate koskari/_version.py from the command line

    :param argv: optional argument list; defaults to sys.argv
    :return: integer exit code
    """

    parser = argparse.ArgumentParser(
        prog='regen_version',
        description='Regenerate koskari/_version.py from setup.cfg and git')
    parser.add_argument(
        '--report',
        nargs='?',
        const='env',
        choices=['env', 'tag'],
        metavar='MODE',
        help='Print version metadata (env assignments or OCI tag) '
        'instead of writing _version.py')
    args = parser.parse_args(argv)

    if args.report is not None:
        if args.report == 'tag':
            sys.stdout.write(report_tag())
        else:
            sys.stdout.write(report_env())
        return 0

    regen_version_py()
    return 0


if __name__ == '__main__':
    sys.exit(main())


# The end.
