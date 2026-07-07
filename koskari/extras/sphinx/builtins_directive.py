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
koskari.extras.sphinx.builtins_directive

Sphinx directive for partitioned koskari.builtins API documentation

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from docutils import nodes

from sphinx.ext.autodoc import ModuleDocumenter
from sphinx.ext.autodoc.directive import (
    DocumenterBridge,
    parse_generated_content,
    process_documenter_options,
)
from sphinx.util.docutils import SphinxDirective

from .autodoc import bind_builtin_member, pick_documenter
from .partition import (collect_builtins_members, member_display_name,
                        partition_builtins)


_BUILTINS_MODULE = 'koskari.builtins'


class KosBuiltinsDirective(SphinxDirective):
    """
    Document all koskari.builtins bindings in Runtime, Compile, and Reader sections
    """

    has_content = False

    def run(self):
        """
        Build docutils nodes for the partitioned builtins reference
        """

        _, lineno = self.get_source_info()
        reporter = self.state.document.reporter

        import koskari.builtins as builtins_module

        members = collect_builtins_members(builtins_module)
        sections = partition_builtins(members)

        options = process_documenter_options(
            ModuleDocumenter,
            self.config,
            dict(self.options))

        bridge = DocumenterBridge(
            self.env,
            reporter,
            options,
            lineno,
            self.state)

        parent = ModuleDocumenter(bridge, _BUILTINS_MODULE)

        # A binding is documented under its Koskari name, so aliases that share
        # one object (e.g. first .. fifth reuse the car / cadr .. caddddr
        # functions) resolve to the same display name. Document each name once;
        # skipping the repeat avoids duplicate object-description warnings.
        seen_names = set()

        out = []
        for title, payload in sections:
            section = nodes.section()
            section['ids'].append(nodes.make_id(title))
            section += nodes.title(text=title)

            if title == 'Runtime features':
                for rubric_title, member_pairs in payload:
                    if not member_pairs:
                        continue
                    section += nodes.rubric(text=rubric_title)
                    for child in self._member_nodes(
                            member_pairs,
                            bridge,
                            parent,
                            builtins_module,
                            seen_names):
                        section += child
            else:
                if not payload:
                    continue
                for child in self._member_nodes(
                        payload,
                        bridge,
                        parent,
                        builtins_module,
                        seen_names):
                    section += child

            if len(section) > 1:
                out.append(section)

        return out


    def _member_nodes(
            self, member_pairs, bridge, parent, builtins_module, seen_names):
        """
        Document each (name, value) pair via legacy autodoc documenters

        Names like compile/set! do not match py_ext_sig_re, so the object is
        bound directly and _generate is invoked without parse_name/import_object

        :param member_pairs: list of (name, value)
        :param bridge: shared DocumenterBridge template for options
        :param parent: ModuleDocumenter for koskari.builtins
        :param builtins_module: loaded koskari.builtins module
        :param seen_names: run-scoped set of already-documented display names;
            aliases resolving to a repeat display name are skipped
        :return: list of docutils nodes
        """

        nodes_out = []
        for name, value in member_pairs:
            display_name = member_display_name(name, value)
            if display_name in seen_names:
                continue
            seen_names.add(display_name)

            doccls = pick_documenter(
                value, name, False, parent, self.env._registry)
            if doccls is None:
                doccls = pick_documenter(
                    value, name, True, parent, self.env._registry)
            if doccls is None:
                continue

            full_name = '%s.%s' % (_BUILTINS_MODULE, name)
            member_bridge = DocumenterBridge(
                self.env,
                bridge._reporter,
                bridge.genopt,
                bridge.lineno,
                bridge.state)
            documenter = doccls(member_bridge, full_name)
            bind_builtin_member(documenter, name, value, builtins_module)
            documenter._generate(
                more_content=None,
                real_modname=_BUILTINS_MODULE,
                check_module=False,
                all_members=False)
            if not member_bridge.result:
                continue

            parsed = parse_generated_content(
                self.state,
                member_bridge.result,
                documenter)
            nodes_out.extend(parsed)

            for fn in member_bridge.record_dependencies:
                self.state.document.settings.record_dependencies.add(fn)

        return nodes_out


# The end.
