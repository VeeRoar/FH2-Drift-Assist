"""Exercise the extracted release against a temporary copy of real FH2 garage tables."""
import hashlib
from pathlib import Path
import shutil
import sqlite3
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from zipfile import ZipFile


def strings(table):
    section, = struct.unpack_from("<I", table, 20)
    _, count = struct.unpack_from("<II", table, section)
    start = section + 8
    pool = start + 6 * (count + 1)
    entries = list(struct.iter_unpack("<HI", table[start:pool]))
    return {key: table[pool + 2*a:pool + 2*b].decode("utf-16le").rstrip("\0")
            for (key, a), (_, b) in zip(entries, entries[1:])}


def run(archive, mount, expected_plugin):
    digest = hashlib.sha256(expected_plugin.read_bytes()).digest()
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        package = root / "release"
        with ZipFile(archive) as z:
            z.extractall(package)
        az = root / "launcher"
        az.mkdir()
        (az / "azimuth.exe").touch()
        game = root / "game" / "Mount"
        stock = game / "media/stripped/stringtables"
        shutil.copytree(mount / "media/stripped/stringtables", stock, ignore=shutil.ignore_patterns("*.str"))
        shutil.copy(mount / "AppxManifest.xml", game)
        command = [str(package / "Install.exe"), str(az), str(game)]
        subprocess.run([*command, "--check"], check=True)
        assert not (az / "fh2-mods").exists()
        subprocess.run(command, check=True)
        assert hashlib.sha256((az / "fh2-mods/FH2DriftAssist.dll").read_bytes()).digest() == digest
        rules = ET.parse(package / "source/installer/Garage.xml").getroot()
        for zip_path in stock.glob("*.zip"):
            with ZipFile(zip_path) as z:
                raw = z.read(next(n for n in z.namelist() if n.lower() == "upgrades.str"))
            expected = strings(raw)
            for row in rules:
                expected[int(row.attrib["key"])] = row.attrib["zh" if zip_path.stem.upper() == "CHT" else "en"]
            loose = game / "mediaoverride/stripped/stringtables" / zip_path.stem / "Upgrades.str"
            assert strings(loose.read_bytes()) == expected, zip_path
        ini = az / "fh2-mods/FH2DriftAssist.ini"
        assert ini.read_bytes() == (package / "source/preset/FH2DriftAssist.ini").read_bytes()
        ini.write_bytes(b"[DriftAssist]\r\nDeadzone=9\r\n")
        subprocess.run(command, check=True)
        assert b"Deadzone=9" in ini.read_bytes() and len(list((az / "ModBackups").iterdir())) == 1

        sql = (package / "source/src/drift_kits.sql").read_text(encoding="utf-8")
        db = sqlite3.connect(":memory:")
        stock_db = (mount / "media/stripped/gamedbRC.slt").resolve()
        source = sqlite3.connect(stock_db.as_uri() + "?mode=ro&immutable=1", uri=True)
        source.backup(db)
        source.close()
        db.executescript(sql)
        cars, = db.execute("SELECT count(*) FROM Data_Car").fetchone()
        count = db.execute("""SELECT Level, count(DISTINCT t.Ordinal) FROM List_UpgradeSpringDamper t
                              JOIN List_SpringDamperPhysics f ON f.SpringDamperPhysicsID=t.FrontSpringDamperPhysicsID
                              JOIN List_SpringDamperPhysics r ON r.SpringDamperPhysicsID=t.RearSpringDamperPhysicsID
                              WHERE t.IsStock=0 AND Level IN (5,6) GROUP BY Level ORDER BY Level""").fetchall()
        assert count == [(5, cars), (6, cars)], count
        db.close()
        print(f"PASS: package round trip, garage names in all installed languages, original tuning preset, both kits on {cars} base vehicles.")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("Usage: python test_package.py RELEASE.zip FH2_MOUNT TESTED_PLUGIN.dll")
    run(*(Path(arg) for arg in sys.argv[1:]))
