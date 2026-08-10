


from koskari.reader import LayoutReader, source_str
import koskari.compile.specials as _koskari_compile_specials  # noqa: F401
import koskari.reader.layouts as _koskari_reader_layouts  # noqa: F401
from koskari.bootstrap import _BUILTIN_REGISTRY


def parse_layout(src_str, **defns):

    builtins = dict(_BUILTIN_REGISTRY)
    builtins.update(defns)

    reader = LayoutReader(builtins)

    # a raw reader is deliberately bare; opt in to the pragma-gated
    # surfaces the layout tests exercise
    reader.set_layout_modes(layout_decorators=True)

    stream = source_str(src_str, '<unittest>')
    return reader.read(stream)


# The end.
