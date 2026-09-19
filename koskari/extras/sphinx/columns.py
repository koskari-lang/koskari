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
koskari.extras.sphinx.columns

Two-column directives for tutorial pages: a dialogue row with the
question on the left and the answer on the right, and a pair row that
sets one spelling beside another, such as Python beside Koskari. The
body of either directive is split into its two columns at a line
holding only +++, and each half is parsed as ordinary markup.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from pathlib import Path

from docutils import nodes
from docutils.parsers.rst import directives
from docutils.statemachine import StringList
from sphinx.util.docutils import SphinxDirective


COLUMN_SEPARATOR = '+++'

CSS_FILE = 'koskari-columns.css'


def split_columns(lines):
    """
    Split directive content into its left and right halves

    :param lines: the content lines
    :return: a tuple of two lists of lines
    :raises ValueError: when the separator is absent or repeated
    """

    lines = list(lines)
    cuts = [
        index for index, line in enumerate(lines)
        if line.strip() == COLUMN_SEPARATOR]

    if len(cuts) != 1:
        raise ValueError(
            f"expected exactly one {COLUMN_SEPARATOR} line, found {len(cuts)}")

    cut = cuts[0]
    return lines[:cut], lines[cut + 1:]


class TwoColumnDirective(SphinxDirective):
    """
    Base for directives that render their content as two columns
    """

    has_content = True
    option_spec = {
        'class': directives.class_option,
    }

    row_class = 'ko-columns'
    left_class = 'ko-col-left'
    right_class = 'ko-col-right'
    left_label = None
    right_label = None

    def labels(self):
        """
        The caption text for each column, or None for no caption

        :return: a tuple of the left and right captions
        """

        return self.left_label, self.right_label

    def column(self, css_class, label, lines, offset):
        """
        Parse one half of the content into a column container

        :param css_class: the column's class
        :param label: caption text, or None
        :param lines: the column's content lines
        :param offset: line offset of the first line in the source
        :return: a container node
        """

        col = nodes.container(classes=['ko-col', css_class])
        if label:
            caption = nodes.paragraph(
                text=label, classes=['ko-col-label'])
            col += caption

        body = StringList(lines, source=self.content.source(0))
        for index, line in enumerate(lines):
            body.items[index] = self.content.items[offset + index]
        self.state.nested_parse(body, self.content_offset + offset, col)
        return col

    def run(self):
        """
        Build the row node with its two parsed columns

        :return: a list holding the row container
        """

        try:
            left, right = split_columns(self.content)
        except ValueError as error:
            raise self.error(f"{self.name}: {error}")

        row = nodes.container(
            classes=[self.row_class] + self.options.get('class', []))
        self.set_source_info(row)

        left_label, right_label = self.labels()
        row += self.column(self.left_class, left_label, left, 0)
        row += self.column(
            self.right_class, right_label, right, len(left) + 1)
        return [row]


class DialogueDirective(TwoColumnDirective):
    """
    A question on the left and its answer on the right
    """

    row_class = 'ko-columns ko-dialogue'
    left_class = 'ko-col-question'
    right_class = 'ko-col-answer'


class PairDirective(TwoColumnDirective):
    """
    One spelling beside another, each column captioned

    The captions default to Python and Koskari and are set with the
    left and right options
    """

    option_spec = {
        'class': directives.class_option,
        'left': directives.unchanged,
        'right': directives.unchanged,
    }

    row_class = 'ko-columns ko-pair'
    left_label = 'Python'
    right_label = 'Koskari'

    def labels(self):
        return (
            self.options.get('left', self.left_label),
            self.options.get('right', self.right_label))


def static_dir():
    """
    The directory holding the stylesheet the directives depend on

    :return: a Path
    """

    return Path(__file__).parent / 'static'


def add_static_path(app, config):
    """
    Register the extension's static directory with the HTML builder

    :param app: the Sphinx application
    :param config: its configuration
    """

    path = str(static_dir())
    if path not in config.html_static_path:
        config.html_static_path.append(path)


def register_columns(app):
    """
    Register the dialogue and pair directives and their stylesheet

    :param app: the Sphinx application
    """

    app.add_directive('dialogue', DialogueDirective)
    app.add_directive('pair', PairDirective)
    app.connect('config-inited', add_static_path)
    app.add_css_file(CSS_FILE)


# The end.
