"""Install and run the per-car cockpit animation synchronizer on Windows."""
import argparse
from contextlib import contextmanager
import ctypes
from ctypes import wintypes
import hashlib
import importlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
from tempfile import NamedTemporaryFile, TemporaryDirectory
from xml.etree import ElementTree
from zipfile import ZipFile

HERE = Path(__file__).resolve().parent
GAME_ID = {"FH2": "Anthem", "FM6": "Monument"}


def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temp = Path(stream.name)
        try:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        except BaseException:
            stream.close()
            temp.unlink(missing_ok=True)
            raise
    try:
        os.replace(temp, path)
    finally:
        temp.unlink(missing_ok=True)


def write_json(path, data):
    atomic_write(path, json.dumps(data, ensure_ascii=False, indent=2).encode("utf8"))


@contextmanager
def exclusive(mount):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateMutexW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.LPCWSTR]
    kernel.CreateMutexW.restype = wintypes.HANDLE
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.ReleaseMutex.argtypes = kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    key = hashlib.sha256(str(mount.resolve()).casefold().encode("utf8")).hexdigest()
    mutex = kernel.CreateMutexW(None, False, "Local\\DriftAssistAnimation-" + key)
    if not mutex:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        if kernel.WaitForSingleObject(mutex, 0) not in (0, 0x80):
            raise RuntimeError("Another animation update is running for this game.")
        try:
            yield
        finally:
            kernel.ReleaseMutex(mutex)
    finally:
        kernel.CloseHandle(mutex)


def check_game(game, azimuth, mount):
    if not (azimuth / "azimuth.exe").is_file():
        raise ValueError(f"azimuth.exe is missing in {azimuth}")
    identity = ElementTree.parse(mount / "AppxManifest.xml").find("{*}Identity")
    if identity is None or identity.get("Name", "").casefold() != GAME_ID[game].casefold():
        raise ValueError(f"The selected Mount folder does not belong to {game}.")
    # Query the process executable path so that a separate Azimuth installation
    # for a different game does not block this installation.
    shell = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    result = subprocess.run(
        [str(shell), "-NoProfile", "-NonInteractive", "-Command",
         "$ErrorActionPreference='Stop'; if (Get-Process -Name azimuth -ErrorAction SilentlyContinue | "
         "Where-Object { $_.Path -eq $env:DRIFT_AZIMUTH_EXE }) {exit 17}; exit 0"],
        env={**os.environ, "DRIFT_AZIMUTH_EXE": str(azimuth / "azimuth.exe")}, check=False)
    if result.returncode == 17:
        raise RuntimeError(f"Save and close {game} and its Azimuth window, then try again.")
    if result.returncode:
        raise RuntimeError("Could not check whether the game is running.")


def install_code(base, game):
    names = ["manager.py", "AnimationSetup.exe", "scan_" + game.lower() + ".py", "known_" + game.lower() + ".json"]
    if game == "FM6":
        names.append("native_zip_fm6.py")
    paths = [HERE / s for s in names] + sorted((HERE / "converter").glob("*.py"))
    if not (HERE / "converter/animator.py").is_file():
        raise FileNotFoundError("Missing converter/animator.py. Extract the entire release ZIP.")
    digest = hashlib.sha256()
    for path in paths:
        digest.update(path.relative_to(HERE).as_posix().encode("utf8") + b"\0")
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    app = base / "app" / digest.hexdigest()
    app.parent.mkdir(parents=True, exist_ok=True)
    if not app.exists():
        with TemporaryDirectory(dir=app.parent) as tmp:
            staged = Path(tmp) / "app"
            for path in paths:
                dst = staged / path.relative_to(HERE)
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, dst)
            staged.rename(app)
    return app / "manager.py"


def launcher_text(game):
    # Only fixed ASCII paths occur in this file. Read the selected Python path
    # from UTF-8 JSON so that spaces, percent signs and Unicode work as entered.
    relative = "DriftAssistAnimation/" + game + "/config.json"
    lines = ["@echo off", "setlocal", 'pushd "%~dp0" || exit /b 1',
             'powershell.exe -NoProfile -Command "$ErrorActionPreference=\'Stop\'; '
             f"$p=(Resolve-Path -LiteralPath '{relative}').Path; "
             "$c=Get-Content -LiteralPath $p -Raw -Encoding UTF8 | ConvertFrom-Json; "
             '& $c.python -B $c.manager launch --config $p; exit $LASTEXITCODE"',
             'set "result=%ERRORLEVEL%"', "popd", "if not %result%==0 pause", "exit /b %result%"]
    return ("\r\n".join(lines) + "\r\n").encode("ascii")


def scanner(game):
    return importlib.import_module("scan_" + game.lower())


def create_converter(config, base):
    sys.path.insert(0, str(HERE / "converter"))
    from animator import Converter
    archive = Path(config["mount"]) / "media/drivers/default.zip"
    if config["game"] == "FM6":
        from native_zip_fm6 import read_member
        driver = read_member(archive, "driver.gr2", config["xcompress"])
    else:
        with ZipFile(archive) as z:
            entry, = [i for i in z.infolist() if i.filename.casefold() == "driver.gr2"]
            if entry.compress_type == 21:
                from scan_fh2 import lzx
                driver = lzx(archive, entry, config["xcompress"])
            else:
                driver = z.read(entry)
    return Converter(base / "cache", config["granny"], driver)


FRAME = struct.Struct("<cI")
MAX_FRAME = 32 * 1024 * 1024


def send_frame(stream, kind, payload=b""):
    if len(payload) > MAX_FRAME:
        raise ValueError("Steering animation exceeds the conversion size limit.")
    stream.write(FRAME.pack(kind, len(payload)))
    stream.write(payload)
    stream.flush()


def read_frame(stream):
    header = stream.read(FRAME.size)
    if len(header) != FRAME.size:
        raise EOFError("The animation worker stopped.")
    kind, size = FRAME.unpack(header)
    if size > MAX_FRAME:
        raise RuntimeError("Invalid response size from the animation worker.")
    payload = stream.read(size)
    if len(payload) != size:
        raise EOFError("The animation worker stopped during conversion.")
    return kind, payload


def run_worker(config_path):
    # Keep native-library messages off the binary reply pipe. Only this process
    # opens a car clip with the native animation reader.
    with os.fdopen(os.dup(sys.stdout.fileno()), "wb") as reply:
        os.dup2(sys.stderr.fileno(), sys.stdout.fileno())
        ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002)
        config = json.loads(config_path.read_bytes())
        converter = create_converter(config, config_path.parent)
        send_frame(reply, b"R")
        while True:
            try:
                kind, name = read_frame(sys.stdin.buffer)
            except EOFError:
                return
            data_kind, raw = read_frame(sys.stdin.buffer)
            if kind != b"N" or data_kind != b"D":
                raise RuntimeError("Invalid conversion request.")
            car = name.decode("utf8")
            try:
                output = converter.convert(raw, converter.profile_for_car(car))
            except (ValueError, KeyError, struct.error) as exc:
                send_frame(reply, b"S", str(exc).encode("utf8"))
            except OSError as exc:
                if "access violation" not in str(exc).lower():
                    raise
                send_frame(reply, b"X", b"Unsupported native steering curve; retained the vehicle animation.")
                return
            else:
                send_frame(reply, b"D", output)


class Worker:
    def __init__(self, config, base):
        self.config = config
        self.path = base / "config.json"
        self.process = None

    def start(self):
        self.process = subprocess.Popen([self.config["python"], "-B", self.config["manager"],
                                         "worker", "--config", str(self.path)],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        try:
            if read_frame(self.process.stdout) != (b"R", b""):
                raise RuntimeError("Failed to initialize the steering animation reader.")
        except BaseException:
            self.close()
            raise

    def close(self):
        if self.process:
            try:
                self.process.stdin.close()
            except BrokenPipeError:
                pass
            self.process.stdout.close()
            self.process.wait()
            self.process = None

    def fit(self, car, raw):
        if Path(car).stem.casefold() == "fal_atom_77":
            raise ValueError("This car uses non-wheel controls.")
        if self.process is None:
            self.start()
        try:
            send_frame(self.process.stdin, b"N", car.encode("utf8"))
            send_frame(self.process.stdin, b"D", raw)
            kind, payload = read_frame(self.process.stdout)
        except (BrokenPipeError, EOFError):
            process = self.process
            self.close()
            if process.returncode & 0xffffffff in (0xc0000005, 0xc0000374, 0xc0000409, 0x80000003):
                raise ValueError("Native reader rejected this car's steering clip.") from None
            raise RuntimeError(f"Animation worker exited with code {process.returncode} while fitting {car}.") from None
        if kind == b"D":
            return payload
        if kind in (b"S", b"X"):
            if kind == b"X":
                self.close()
            raise ValueError(payload.decode("utf8"))
        raise RuntimeError("Unknown response from steering animation reader.")


def synchronize(config, base):
    state = base / "state"
    state.mkdir(exist_ok=True)
    worker = Worker(config, base)
    try:
        worker.start()
        result = scanner(config["game"]).sync(config, state, worker.fit)
    finally:
        worker.close()
    write_json(base / "last-scan.json", result)
    print("Animation scan finished.", base / "last-scan.json", flush=True)
    return result


def configure(args, base):
    sys.path.insert(0, str(HERE / "converter"))
    from animator import CACHE_FILE, prepare
    config_path = base / "config.json"
    old = json.loads(config_path.read_bytes()) if config_path.is_file() else {}
    if old and Path(old["mount"]).resolve() != args.mount:
        raise ValueError("This launcher already manages another Mount. Uninstall its animation setup first.")
    launcher = args.azimuth / ("Play " + args.game + " (Drift Assist).cmd")
    command = launcher_text(args.game)
    if launcher.exists() and launcher.read_bytes() != command:
        raise ValueError(f"A different file already uses the launcher name: {launcher}")
    cfg = dict(game=args.game, mount=str(args.mount), azimuth=str(args.azimuth),
               python=sys.executable, manager=str(install_code(base, args.game)))
    for attr in ("fh6", "acl", "granny", "xcompress"):
        value = getattr(args, attr) or old.get(attr)
        if not value:
            raise ValueError(f"Select the {attr} path in animation setup.")
        cfg[attr] = str(Path(value).resolve(strict=True))
    # Check native dependencies before installing any vehicle motion.
    ctypes.WinDLL(cfg["xcompress"]).XMemCreateDecompressionContext
    source = Path(cfg["fh6"]) / "animation.zip"
    rig = source.parent / "cars/NIS_SkylineTurboRS_83.zip"
    cache = base / "cache"
    cfg["source_files"] = [[str(p), p.stat().st_size, p.stat().st_mtime_ns]
                           for p in (source, rig, Path(cfg["acl"]))]
    if cfg["source_files"] != old.get("source_files") or not (cache / CACHE_FILE).is_file():
        print("Reading steering references from FH6...", flush=True)
        prepare(source.parent, cfg["acl"], cache)
    # Persist recovery information before the scanner makes its first change.
    write_json(config_path, cfg)
    synchronize(cfg, base)
    atomic_write(launcher, command)
    print("Start the game with", launcher)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    setup = commands.add_parser("setup")
    setup.add_argument("--game", required=True, choices=GAME_ID)
    setup.add_argument("--azimuth", type=Path, required=True)
    setup.add_argument("--mount", type=Path, required=True)
    for key in ("fh6", "acl", "granny", "xcompress"):
        setup.add_argument("--" + key, type=Path)
    for cmd in ("sync", "launch", "uninstall", "worker"):
        commands.add_parser(cmd).add_argument("--config", type=Path, required=True)
    args = parser.parse_args()
    if args.action == "worker":
        run_worker(args.config.resolve(strict=True))
        return
    if args.action == "setup":
        args.mount = args.mount.resolve(strict=True)
        args.azimuth = args.azimuth.resolve(strict=True)
        with exclusive(args.mount):
            check_game(args.game, args.azimuth, args.mount)
            base = args.azimuth / "DriftAssistAnimation" / args.game
            base.mkdir(parents=True, exist_ok=True)
            configure(args, base)
        return
    config_path = args.config.resolve(strict=True)
    cfg = json.loads(config_path.read_bytes())
    game, azimuth, mount = cfg["game"], Path(cfg["azimuth"]), Path(cfg["mount"])
    base = config_path.parent
    with exclusive(mount):
        check_game(game, azimuth, mount)
        if args.action == "uninstall":
            result = scanner(game).uninstall(cfg, base / "state")
            write_json(base / "last-uninstall.json", result)
            launcher = azimuth / ("Play " + game + " (Drift Assist).cmd")
            if launcher.is_file() and launcher.read_bytes() == launcher_text(game):
                launcher.unlink()
            print("Animation uninstall report:", base / "last-uninstall.json")
        else:
            synchronize(cfg, base)
            if args.action == "launch":
                subprocess.Popen([str(azimuth / "azimuth.exe")], cwd=azimuth)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print("Animation setup/scan could not finish:", exc, file=sys.stderr, flush=True)
        sys.exit(1)
