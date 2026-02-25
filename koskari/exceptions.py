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
koskari.exceptions

Base exception classes for Koskari.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


class KoskariException(Exception):
    """
    Base class for error-driven Exceptions raised by Koskari.
    """

    pass


class NotYetImplemented(KoskariException):
    """
    Raised as a placeholder for features that have not been
    implemented yet.
    """

    pass


class KoskariSyntaxError(SyntaxError):
    """
    An error in Koskari syntax, either during read or compile time.
    """

    def __init__(self, message, location=None, filename=None, text=None):

        if filename:
            if not location:
                location = (1, 0)
            super().__init__(message, (filename, *location, text))
            self.print_file_and_line = True
        else:
            super().__init__(message)

        self.message = message
        self.location = location
        self.filename = filename
        self.text = text


class ReaderError(KoskariSyntaxError):
    """
    Base class for reader-phase errors
    """

    pass


class ReaderSyntaxError(ReaderError):
    """
    Low-level parse errors during s-expression or layout reading
    """

    pass


class FormatStringSyntaxError(ReaderSyntaxError):
    """
    Errors in format string (f-string) syntax
    """

    pass


class LayoutError(ReaderError):
    """
    Layout block structure errors
    """

    pass


class CompilerError(KoskariException):
    """
    Base class for compile-phase errors
    """

    pass


class SpecialError(CompilerError):
    """
    Special-form validation errors
    """

    pass


class ParametersError(CompilerError):
    """
    Call-site parameter errors from gather_parameters and gather_parameters_ng
    """

    pass


class UncaughtCompilerError(CompilerError):
    """
    Wraps unexpected exceptions raised during compilation
    """

    pass


# The end.
