# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
tests.reproducers.helpers

Small Python types injected into full-stack class-hierarchy reproducers.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from abc import ABC


class PlainBase(object):
    """
    Minimal concrete Python base for Koskari subclass reproducers
    """

    def __init__(self, tag):
        self.tag = tag


class Animal(ABC):
    """
    Minimal ABC base for Koskari subclass reproducers
    """

    def __init__(self, name):
        self.name = name


# The end.
