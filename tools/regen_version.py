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


import os
import subprocess
import sys
from configparser import ConfigParser
from datetime import datetime, timezone
from pathlib import Path

from packaging.version import Version


def project_root(root=None):
    """
    Return the repository root path

    :param root: optional override; defaults to parent of tools/
    :return: Path to repository root
    """

    if root is not None:
        return Path(root).resolve()
    return Path(__file__).resolve().parent.parent


def read_release(root=None):
    """
    Read the release version from setup.cfg metadata

    :param root: repository root path
    :return: release version string
    """

    root = project_root(root)
    conf = ConfigParser()
    conf.read([str(root / 'setup.cfg')])
    return conf['metadata']['version']


def _git_run(root, *args):
    """
    Run a git command and return stripped stdout, or None on failure

    :param root: repository root path
    :param args: git command arguments after the git binary
    :return: stdout text or None
    """

    try:
        proc = subprocess.run(
            ['git'] + list(args),
            cwd=str(root),
            check=False,
            capture_output=True,
            text=True)
    except OSError:
        return None

    if proc.returncode != 0:
        return None

    return proc.stdout.strip()


def git_state(root=None):
    """
    Collect git metadata for version assembly

    :param root: repository root path
    :return: dict with commit, exact_tag, and dirty keys
    """

    root = project_root(root)
    git_dir = root / '.git'
    if not git_dir.exists():
        return {
            'commit': None,
            'exact_tag': None,
            'dirty': False,
        }

    commit = _git_run(root, 'rev-parse', '--short', 'HEAD')
    exact_tag = _git_run(root, 'describe', '--exact-match', '--tags', 'HEAD')

    dirty_proc = subprocess.run(
        ['git', 'diff-index', '--quiet', 'HEAD', '--'],
        cwd=str(root),
        check=False)
    dirty = dirty_proc.returncode != 0

    return {
        'commit': commit,
        'exact_tag': exact_tag,
        'dirty': dirty,
    }


def assemble_version(release, state, now=None):
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
    expected_tag = 'v' + release

    if (not dirty
            and exact_tag == expected_tag
            and commit is not None):
        version = release
        build_kind = 'release'
    elif commit is None:
        version = release
        build_kind = 'release'
    else:
        build_kind = 'dev'
        local = 'g' + commit
        if dirty:
            if now is None:
                now = datetime.now(timezone.utc)
            stamp = now.strftime('%Y%m%dT%H%M%S')
            local = local + '.d' + stamp
        version = release + '+' + local

    Version(version)

    return {
        'version': version,
        'release': release,
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
    fields = {
        'version': info['version'],
        'release': info['release'],
        'build_kind': info['build_kind'],
        'commit': repr(info['commit']),
        'dirty': repr(info['dirty']),
    }
    return template % fields


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
    release = read_release(root)
    info = assemble_version(release, git_state(root))
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

    regen_version_py()


if __name__ == '__main__':
    sys.exit(main())


# The end.
