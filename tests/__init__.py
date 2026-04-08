# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 3 of the
# License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see
# <http://www.gnu.org/licenses/>.

"""
Test suite for koskari.
"""

from koskari.lib import Expression, Function, FormalsSpec


def make_callable(code, ns, formals_spec=None):
    """
    Build a callable from code, namespace, and optional formals spec.
    Test helper only.

    If formals_spec is None, returns Expression(code, ns). Otherwise
    returns Function(code, ns, formals_spec). Calling the result
    returns a Coroutine.

    :param code: Code (bytecode)
    :param ns: Namespace
    :param formals_spec: Optional FormalsSpec
    :return: Expression or Function
    """
    if formals_spec is None:
        return Expression(code, ns)
    if not isinstance(formals_spec, FormalsSpec):
        raise TypeError(
            'expected FormalsSpec or None, got %s' % type(formals_spec).__name__
        )
    return Function(code, ns, formals_spec)


# The end.
