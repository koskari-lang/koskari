#! /usr/bin/env python3


# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as
# published by the Free Software Foundation; either version 3 of the
# License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, see
# <http://www.gnu.org/licenses/>.


"""
Curses life in Sibilant

author: Christopher O'Brien <obriencj@gmail.com>
license: LGPL v.3
"""


from setuptools import setup


TROVE_CLASSIFIERS = [
    "Development Status :: 4 - Beta",
    "Intended Audience :: Developers",
    "Operating System :: OS Independent",
    "Programming Language :: Python :: 3 :: Only",
    "Programming Language :: Python :: Implementation :: CPython",
    "Topic :: Software Development :: Libraries :: Python Modules",
]


setup(name = "koskari-curses-life",
      version = "0.9.0",

      packages = ["koslife"],

      entry_points = {
          "koskari_scripts": [
              'koslife=koslife.cli:main',
          ],
      },

      install_requires = [
          "koskari",
      ],

      # koskari.extras.setuptools adds *.ki/*.kio package_data and sets
      # zip_safe=False for declared packages during the build finalize hook.

      # PyPI information
      author = "Christopher O'Brien",
      author_email = "obriencj@gmail.com",
      url = "https://github.com/obriencj/python-sibilant/",
      license = "GNU General Public License v3",

      description = "Curses life in Koskari",

      classifiers = TROVE_CLASSIFIERS)


# The end.
