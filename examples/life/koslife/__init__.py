
import koskari


def main():
    print("Launching ...")

    from .cli import main as cli_main
    from asyncio import run

    return run(cli_main())


if __name__ == "__main__":
    import sys
    sys.exit()


# The end.
