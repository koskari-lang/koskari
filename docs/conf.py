"""
Sphinx configuration for Koskari user documentation.
"""


import os
import sys


def load_setup():
    from configparser import ConfigParser

    global project
    global release
    global version
    global author
    global copyright

    conf = ConfigParser()
    conf.read([os.path.join('..', 'setup.cfg')])
    metadata = conf['metadata']

    project = metadata['name']
    release = metadata['version']
    version = '.'.join(release.split('.')[:2])
    author = metadata['author']
    copyright = f"{metadata['copyright_years']}, {author}"


load_setup()


sys.path.insert(0, os.path.abspath('..'))


def skip_bootstrap(app, what, name, obj, skip, options):
    if name and name.startswith('koskari.bootstrap'):
        return True
    return skip


def setup(app):
    app.connect('autodoc-skip-member', skip_bootstrap)


extensions = [
    'myst_parser',
    'sphinx.ext.autodoc',
    'sphinx.ext.autosummary',
    'sphinx.ext.intersphinx',
    'sphinx.ext.napoleon',
    'sphinxcontrib.mermaid',
]

templates_path = ['_templates']
exclude_patterns = ['build', 'Thumbs.db', '.DS_Store']

root_doc = 'index'

html_theme = 'sphinx_rtd_theme'
html_static_path = ['_static']

source_suffix = {
    '.rst': 'restructuredtext',
    '.md': 'markdown',
}

intersphinx_mapping = {
    'python': ('https://docs.python.org/3', None),
}

myst_enable_extensions = [
    'colon_fence',
    'deflist',
    'linkify',
]

myst_fence_as_directive = [
    'mermaid',
]

mermaid_version = '10.9.1'

autosummary_generate = True

autodoc_default_options = {
    'members': True,
    'undoc-members': True,
    'show-inheritance': True,
}

suppress_warnings = [
    'autodoc.duplicate_object',
]

# The end.
