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
koskari.fences

Fence handlers for compile-time expansion of fenced foreign literals.

A triple-backtick fence captures raw foreign source at read time; the
reader lowers it to a (compile/apply-fence NAME RAW META) form. The
compile/apply-fence special resolves fence/<name> in the module
namespace and calls the handler with the raw text and a metadata dict;
the handler returns ordinary Koskari source, which is compiled in place
of the fence.

Fence expansions are self-contained: they reference only universal
builtins (fence/json lowers to the #dict / #list builders, fence/bash
to run-process), never pre-existing helper bindings in the compiling
module's namespace.

Handlers register via ``koskari.bootstrap.builtin``.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from .helpers import (FenceHandler, fence, find_fence, meta_plist_to_dict,
                      meta_position, opener_position)
from .json_ import fence_json


__all__ = (
    "fence",
    "FenceHandler",

    "find_fence",
    "meta_plist_to_dict",
    "meta_position",
    "opener_position",

    "fence_json",
)


# The end.
