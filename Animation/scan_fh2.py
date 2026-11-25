"""Discover active FH2 steering clips and maintain recoverable loose media overrides."""
from contextlib import closing, contextmanager
import ctypes
from dataclasses import dataclass
import hashlib
import json
import msvcrt
import os
from pathlib import Path, PureWindowsPath
import sqlite3
import struct
import tempfile
from zipfile import BadZipFile, ZipFile
from zlib import crc32, error as CompressionError

TURN = "scene/Drivers/default/leftRightTurn.gr2"
KNOWN = Path(__file__).with_name("known_fh2.json")
LIMIT = 16 * 1024 * 1024


def digest(data):
    return hashlib.sha256(data).hexdigest() if data is not None else None


def inside(root, relative):
    root = root.resolve()
    path = (root / relative).resolve()
    if not path.is_relative_to(root) or path == root:
        raise ValueError(f"Path outside installation: {path}")
    return path


def target(mount, car):
    if (not car or car.endswith((" ", ".")) or any(c in '/\\:*?"<>|' or ord(c) < 32 for c in car)
            or PureWindowsPath(car).is_reserved()):
        raise ValueError(f"Invalid car directory: {car!r}")
    return inside(mount, Path("mediaoverride/cars") / car / TURN)


def read_local(path):
    if not path.exists():
        return None
    if path.stat().st_size > LIMIT:
        raise ValueError(f"Oversized steering clip: {path}")
    return path.read_bytes()


def lzx(path, entry, library):
    if not library or not Path(library).is_file():
        raise RuntimeError("Select your local xcompress64.dll to read FH2 vehicle archives.")
    with path.open("rb") as f:
        f.seek(entry.header_offset)
        header = f.read(30)
        if len(header) != 30 or header[:4] != b"PK\x03\x04":
            raise ValueError(f"Invalid archive header: {path}")
        name_len, extra_len = struct.unpack_from("<HH", header, 26)
        f.seek(name_len + extra_len, 1)
        data = f.read(entry.compress_size)
    if len(data) != entry.compress_size:
        raise ValueError(f"Truncated car archive: {path}")
    lib = ctypes.WinDLL(str(Path(library).resolve()))
    ptr, uint = ctypes.c_void_p, ctypes.c_uint32
    lib.XMemCreateDecompressionContext.argtypes = [ctypes.c_int, ptr, ctypes.c_int, ctypes.POINTER(ptr)]
    lib.XMemResetDecompressionContext.argtypes = [ptr]
    lib.XMemDecompressStream.argtypes = [ptr, ptr, ctypes.POINTER(uint), ptr, ctypes.POINTER(uint)]
    lib.XMemDestroyDecompressionContext.argtypes = [ptr]
    context = ptr()
    if lib.XMemCreateDecompressionContext(1, None, 0, ctypes.byref(context)) or not context:
        raise RuntimeError("Cannot initialize the selected xcompress64.dll.")
    try:
        output = ctypes.create_string_buffer(entry.file_size)
        src_len, dst_len = uint(len(data)), uint(entry.file_size)
        if lib.XMemResetDecompressionContext(context) or lib.XMemDecompressStream(
            context, output, ctypes.byref(dst_len), data, ctypes.byref(src_len)
        ) or dst_len.value != entry.file_size:
            raise ValueError(f"Cannot decompress steering animation: {path}")
        return output.raw
    finally:
        lib.XMemDestroyDecompressionContext(context)


@dataclass
class Clip:
    car: str
    path: Path
    member: str | None = None
    token: str = ""

    def read(self, library):
        if self.member is None:
            return read_local(self.path)
        with ZipFile(self.path) as z:
            e = z.getinfo(self.member)
            if max(e.file_size, e.compress_size) > LIMIT or e.file_size == 0:
                raise ValueError(f"Invalid clip size: {self.path}")
            raw = lzx(self.path, e, library) if e.compress_type == 21 else z.read(e)
            if len(raw) != e.file_size or crc32(raw) != e.CRC:
                raise ValueError(f"Steering clip checksum mismatch: {self.path}")
            return raw


def mod_layers(azimuth):
    mods = azimuth / "mods"
    if not mods.is_dir():
        return
    folders = {p.name.casefold(): p for p in mods.iterdir()
               if p.is_dir() and not p.name.startswith(".") and not p.is_symlink()
               and not getattr(p, "is_junction", lambda: False)()}
    order_file = mods / "loadorder.txt"
    lines = order_file.read_text(encoding="utf-8-sig").splitlines() if order_file.is_file() else []
    order, listed = [], set()
    for line in lines:
        name = line.strip()
        if not name:
            continue
        enabled = not name.startswith("#")
        key = name if enabled else name[1:].strip()
        key = key.casefold()
        listed.add(key)
        if enabled and key in folders:
            if key in order:
                raise ValueError(f"Duplicate folder in {order_file}: {name}")
            order.append(key)
    order.extend(sorted(folders.keys() - listed))
    ids = set()
    for key in order:
        folder = folders[key]
        manifest = next((folder / n for n in ("mod.json", "addon.json") if (folder / n).is_file()), None)
        try:
            doc = json.loads(manifest.read_text(encoding="utf-8-sig")) if manifest else {}
        except (UnicodeError, ValueError):
            continue
        if manifest and (not isinstance(doc, dict) or doc.get("format_version") != 1 or not isinstance(doc.get("id"), str)):
            continue
        ident = doc.get("id", folder.name)
        deps = doc.get("dependencies", [])
        if (not isinstance(deps, list) or ident.casefold() in ids or 
                any(not isinstance(dep, str) or dep.casefold() not in ids for dep in deps)):
            continue
        ids.add(ident.casefold())
        relative = doc.get("media", "media")
        if isinstance(relative, str) and relative:
            yield inside(folder, relative) / "cars"


def discover(mount, az):
    # Extra Options precedence: original, enabled mods (last first), mediapc,
    # mediaoverride. The final loose file may be our own previous output.
    lower, loose, issues = {}, {}, {}
    top = mount / "mediaoverride/cars"
    for folder in (mount / "media/cars", *mod_layers(az), mount / "mediapc/cars", top):
        if not folder.is_dir():
            continue
        for path in sorted(folder.glob("*.zip")):
            key = path.stem.casefold()
            try:
                with ZipFile(path) as z:
                    hits = [e for e in z.infolist() if e.filename.replace("\\", "/").casefold() == TURN.casefold()]
                if len(hits) > 1:
                    raise ValueError("Multiple default steering animations")
            except (BadZipFile, ValueError, OSError) as exc:
                issues[key] = f"{path}: {exc}"
                continue
            if hits:
                entry = hits[0]
                stat = path.stat()
                token = json.dumps([str(path), stat.st_mtime_ns, stat.st_size, entry.CRC, entry.file_size])
                lower[key] = Clip(path.stem, path, entry.filename, token)
                issues.pop(key, None)
        for car in folder.iterdir():
            if car.is_dir() and (car / TURN).is_file():
                (loose if folder == top else lower)[car.name.casefold()] = Clip(car.name, car / TURN)
                if folder != top:
                    issues.pop(car.name.casefold(), None)
    return lower, loose, issues


def atomic(path, data):
    if data is None:
        path.unlink(missing_ok=True)
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".drift-", delete=False) as f:
        tmp = Path(f.name)
        try:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        except BaseException:
            f.close()
            tmp.unlink(missing_ok=True)
            raise
    try:
        os.replace(tmp, path)
    finally:
        tmp.unlink(missing_ok=True)


def finish_pending(db, mount):
    for car, before, after in db.execute("SELECT car,before_hash,payload FROM pending").fetchall():
        path = target(mount, car)
        current, desired = digest(read_local(path)), digest(after)
        if current != desired:
            if current != before:
                raise RuntimeError(f"Animation changed during a pending update; retaining both files: {path}")
            atomic(path, after)
            if digest(read_local(path)) != desired:
                raise OSError(f"Failed to verify {path}")
        with db:
            db.execute("DELETE FROM pending WHERE car=?", (car,))


@contextmanager
def session(state_dir, mount):
    state_dir = Path(state_dir)
    state_dir.mkdir(parents=True, exist_ok=True)
    with (state_dir / "fh2.lock").open("a+b") as lock:
        lock.seek(0)
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        try:
            with closing(sqlite3.connect(state_dir / "fh2.sqlite3")) as db:
                db.executescript("""
                    CREATE TABLE IF NOT EXISTS identity(mount TEXT PRIMARY KEY);
                    CREATE TABLE IF NOT EXISTS owned(
                        car TEXT PRIMARY KEY COLLATE NOCASE, original BLOB, loose_source INTEGER,
                        input_hash TEXT, output_hash TEXT, token TEXT);
                    CREATE TABLE IF NOT EXISTS pending(car TEXT PRIMARY KEY, before_hash TEXT, payload BLOB);
                    """)
                rows = db.execute("SELECT mount FROM identity").fetchall()
                if rows and rows != [(str(mount).casefold(),)]:
                    raise ValueError("Animation records belong to a different FH2 Mount.")
                with db:
                    db.execute("INSERT OR IGNORE INTO identity VALUES(?)", (str(mount).casefold(),))
                finish_pending(db, mount)
                yield db
        finally:
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)


def replace(db, mount, car, current, new, metadata):
    path = target(mount, car)
    if digest(read_local(path)) != current:
        raise RuntimeError(f"Steering animation changed during scanning: {path}")
    with db:
        if metadata is None:
            db.execute("DELETE FROM owned WHERE car=?", (car,))
        else:
            db.execute("INSERT OR REPLACE INTO owned VALUES(?,?,?,?,?,?)", (car, *metadata))
        if current != digest(new):
            db.execute("INSERT INTO pending VALUES(?,?,?)", (car, current, new))
    finish_pending(db, mount)


def sync(config, state_dir, convert_one):
    mount, az = (Path(config[k]).resolve() for k in ("mount", "azimuth"))
    if not (mount / "media/cars").is_dir():
        raise ValueError("FH2 Mount/media/cars is missing.")
    known = json.loads(KNOWN.read_bytes())
    stats = dict(converted=0, adopted=0, unchanged=0, skipped=0, retired=0, skipped_cars={})
    def skip(car, reason):
        print(f"{car}: {reason}", flush=True)
        stats["skipped_cars"][car] = str(reason)
        stats["skipped"] += 1
    with session(state_dir, mount) as db:
        lower, loose, issues = discover(mount, az)
        owned = {r[0].casefold(): r for r in db.execute("SELECT car,loose_source,input_hash,output_hash,token FROM owned")}
        for key in sorted(lower.keys() | loose.keys() | owned.keys() | issues.keys()):
            row = owned.get(key)
            candidate = loose.get(key) or lower.get(key)
            car = row[0] if row else candidate.car if candidate else key
            try:
                path = target(mount, car)
                before = read_local(path)
            except (ValueError, OSError) as exc:
                skip(car, exc)
                continue
            current = digest(before)
            if row and current != row[3]:
                skip(car, "Another tool changed the loose animation; retained.")
                continue
            legacy = known.get(key)
            is_legacy = bool(legacy and current in legacy[1:])
            use_loose = bool(row[1]) if row else before is not None and not is_legacy
            if not use_loose and key in issues:
                skip(car, issues[key])
                continue
            token = "original loose" if use_loose else lower[key].token if key in lower else None
            if row and token and token == row[4]:
                stats["unchanged"] += 1
                continue
            original = (db.execute("SELECT original FROM owned WHERE car=?", (car,)).fetchone()[0] if row else before)
            if use_loose:
                source = original
            elif key in lower:
                try:
                    source = lower[key].read(config.get("xcompress"))
                except (BadZipFile, ValueError, KeyError, NotImplementedError, EOFError, OSError,
                        CompressionError, struct.error) as exc:
                    skip(car, exc)
                    continue
            else:
                if row:
                    replace(db, mount, car, current, original, None)
                    stats["retired"] += 1
                continue
            source_hash = digest(source)
            if row and source_hash == row[2]:
                with db:
                    db.execute("UPDATE owned SET token=? WHERE car=?", (token, car))
                stats["unchanged"] += 1
                continue
            if not row and legacy and current == legacy[2] and source_hash == legacy[0]:
                replace(db, mount, car, current, before, (original, False, source_hash, current, token))
                stats["adopted"] += 1
                continue
            try:
                output = convert_one(car, source)
            except ValueError as exc:
                skip(car, exc)
                continue
            except OSError as exc:
                # ctypes may surface a native decoder fault as OSError on
                # Windows. Preserve the previous animation and keep scanning.
                # Unrelated I/O errors (e.g. disk full) must stop the update.
                if "access violation" not in str(exc).casefold():
                    raise
                skip(car, "The local animation decoder could not sample this vehicle: " + str(exc))
                continue
            if not isinstance(output, bytes) or len(output) == 0 or len(output) > LIMIT or source == output:
                raise RuntimeError(f"Invalid converter output for {car}")
            replace(db, mount, car, current, output, (original, use_loose, source_hash, digest(output), token))
            print(f"{car}: steering animation fitted.", flush=True)
            stats["converted"] += 1
    print("FH2 animation scan:", {k: v for k, v in stats.items() if k != "skipped_cars"})
    return stats


def uninstall(config, state_dir):
    mount = Path(config["mount"]).resolve()
    restored, conflicts = 0, []
    with session(state_dir, mount) as db:
        for car, out in db.execute("SELECT car,output_hash FROM owned").fetchall():
            path = target(mount, car)
            if digest(read_local(path)) != out:
                conflicts.append(str(path))
                continue
            prior, = db.execute("SELECT original FROM owned WHERE car=?", (car,)).fetchone()
            replace(db, mount, car, out, prior, None)
            restored += 1
    if conflicts:
        raise RuntimeError("Retained externally edited files and their backups: " + ", ".join(conflicts))
    return {"restored": restored}
