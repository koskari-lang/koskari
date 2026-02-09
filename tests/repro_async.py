"""Minimal repro for async test - run with: tox -e py312 -- tests/repro_async.py"""
import asyncio
import collections.abc
from tests import make_callable, build_expr


async def get_value():
    await asyncio.sleep(0)
    return 41


def main():
    code, ns = build_expr([
        ["VAR", "get_value"],
        ["CALL", 0],
        ["CONST", 1],
        ["OBJECT_ADD", 2],
        ["RETURN", 1],
    ], values={"get_value": get_value})
    expr = make_callable(code, ns)
    coro = expr()
    print("coro", coro, "type", type(coro))
    print("asyncio.iscoroutine(coro)", asyncio.iscoroutine(coro))
    print("isinstance(coro, collections.abc.Coroutine)", isinstance(coro, collections.abc.Coroutine))
    try:
        result = asyncio.run(coro)
        print("result", result)
    except Exception as e:
        import traceback
        print("Exception:", e)
        traceback.print_exc()
        if getattr(e, "__context__", None):
            print("--- context ---")
            traceback.print_exception(type(e.__context__), e.__context__, e.__context__.__traceback__)


if __name__ == "__main__":
    main()
