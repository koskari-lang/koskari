# Test package for koskari.compile


from koskari.compile import Compiler


def make_fragment(instr):
    """
    A ByteCodeFragment over instr from a fresh Compiler, for driving
    ByteCodeProcessor passes directly in tests
    """

    frag = Compiler("<unittest>", None).fragment("test")
    frag.instructions = list(instr)
    return frag


# The end.
