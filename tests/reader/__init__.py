


from koskari.reader import LayoutReader, source_str
from koskari.reader.layouts import LAYOUTS


def parse_layout(src_str, **defns):

    builtins = {}
    for name, value in LAYOUTS.items():
        builtins[str(name)] = value
    builtins.update(defns)

    reader = LayoutReader(builtins)
    stream = source_str(src_str, '<unittest>')
    return reader.read(stream)


# The end.
