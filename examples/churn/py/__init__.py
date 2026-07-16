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
Python-language churn benchmark variants and their descriptors.

Each non-underscore submodule defines a module-level ``BENCHMARK`` (see
:mod:`registry`).  Adding a benchmark is a matter of dropping one file here
and its Koskari kernel of the same base name in the sibling ``ki`` package.

Regular package (not a namespace package) so this directory wins over any
installed ``py`` distribution, and so ``import koskari`` runs to activate the
``.ki`` import hook before the sibling kernels load.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import koskari  # noqa: F401  (activates the .ki import hook)


# The end.
