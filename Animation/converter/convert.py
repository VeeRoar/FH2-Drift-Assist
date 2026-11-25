"""Command-line entry point for local FH2 and FM6 cockpit animation conversion."""
import argparse
import os
from pathlib import Path
import sys
import tempfile

from animator import Converter, prepare


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("prepare", help="read motion data from your installed FH6")
    build.add_argument("--fh6-media", type=Path, required=True)
    build.add_argument("--acl-dll", type=Path, required=True)
    build.add_argument("--cache", type=Path, required=True)
    job = commands.add_parser("convert", help="convert an original steering clip into a separate file")
    job.add_argument("--cache", type=Path, required=True)
    job.add_argument("--granny-dll", type=Path, required=True)
    job.add_argument("--driver", type=Path, required=True, help="the target game's driver.gr2")
    job.add_argument("--input", type=Path, required=True)
    job.add_argument("--output", type=Path, required=True)
    profile = job.add_mutually_exclusive_group()
    profile.add_argument("--profile", choices=("normal", "yoke"))
    profile.add_argument("--car", help="car archive name to select its FH6 profile automatically")
    args = parser.parse_args()
    if args.command == "prepare":
        print(prepare(args.fh6_media, args.acl_dll, args.cache))
        return
    if args.input.resolve() == args.output.resolve():
        parser.error("Use a separate output file to preserve the source clip")
    converter = Converter(args.cache, args.granny_dll, args.driver.read_bytes())
    selected = converter.profile_for_car(args.car) if args.car else (args.profile or "normal")
    data = converter.convert(args.input.read_bytes(), selected)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=args.output.parent, delete=False) as stream:
        temporary = Path(stream.name)
    try:
        temporary.write_bytes(data)
        os.replace(temporary, args.output)
    finally:
        temporary.unlink(missing_ok=True)
    print(args.output)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError) as exc:
        sys.exit(str(exc))
