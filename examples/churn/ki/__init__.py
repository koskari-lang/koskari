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
Koskari-language churn benchmark kernels.

Each submodule is a self-contained ``.ki`` kernel of the same base name as
its Python counterpart in the sibling ``py`` package.  A regular package
(with this ``__init__.py``) so it wins over any installed ``ki`` distribution
and so ``import koskari`` activates the ``.ki`` import hook.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import koskari  # noqa: F401  (activates the .ki import hook)


# The end.
