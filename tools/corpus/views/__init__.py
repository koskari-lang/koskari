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
tools.corpus.views

Registry of corpus dataset view transforms.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.6 via Cursor
"""


import importlib.util
from pathlib import Path


_VIEWS_DIR = Path(__file__).resolve().parent


def _load_view_module(filename):
    """
    Load a view module from this directory by filename

    :param filename: module basename under views/
    :return: loaded module
    """

    path = _VIEWS_DIR / filename
    name = 'corpus_view_' + path.stem
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


_eval = _load_view_module('eval.py')
_full = _load_view_module('full.py')
_docs = _load_view_module('docs.py')
_programs = _load_view_module('programs.py')

# Record-mapped views: each value is a transform(record, provenance) callable
# applied once per corpus record.
VIEWS = {
    _eval.VIEW_NAME: _eval.transform,
    _full.VIEW_NAME: _full.transform,
}

# Source-generated views: each value is a generate(root, provenance) callable
# that produces rows from repository files rather than corpus records. These
# publish reference material (docs, example programs) that is not part of the
# verified corpus.
SOURCE_VIEWS = {
    _docs.VIEW_NAME: _docs.generate,
    _programs.VIEW_NAME: _programs.generate,
}


# The end.
