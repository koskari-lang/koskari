


from koskari.reader import LayoutReader, source_str
import koskari.compile.specials as _koskari_compile_specials  # noqa: F401
import koskari.reader.layouts as _koskari_reader_layouts  # noqa: F401
from koskari.stdlib import BUILTINS


def parse_layout(src_str, **defns):

    builtins = dict(BUILTINS)
    builtins.update(defns)

    reader = LayoutReader(builtins)
    stream = source_str(src_str, '<unittest>')
    return reader.read(stream)


# The end.
