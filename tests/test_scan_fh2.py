"""Exercise pre-launch discovery, upgrade adoption, recovery, and rollback in temporary roots."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile, ZIP_DEFLATED

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "Animation"))
import scan_fh2 as scanner


class ScanTest(unittest.TestCase):
    def setUp(self):
        work = tempfile.TemporaryDirectory()
        self.addCleanup(work.cleanup)
        self.root = Path(work.name)
        self.mount = self.root / "Mount"
        self.az = self.root / "Azimuth"
        self.state = self.root / "state"
        self.stock = self.mount / "media/cars"
        self.stock.mkdir(parents=True)
        self.cfg = {"mount": str(self.mount), "azimuth": str(self.az)}
        self.called = []

    def fit(self, car, raw):
        self.called.append((car, raw))
        if raw == b"unsupported":
            raise ValueError("Unsupported steering rig")
        if raw == b"bad native curve":
            raise OSError("exception: access violation writing 0x28B")
        return b"fitted " + raw

    def archive(self, folder, name, raw):
        folder.mkdir(parents=True, exist_ok=True)
        with ZipFile(folder / (name + ".zip"), "w", ZIP_DEFLATED) as z:
            z.writestr(scanner.TURN.replace("/", "\\"), raw)

    def scan(self):
        return scanner.sync(self.cfg, self.state, self.fit)

    def test_new_addon_updates_and_rollback(self):
        self.archive(self.stock, "Classic", b"body")
        self.assertEqual(self.scan()["converted"], 1)
        mods = self.az / "mods"
        path = mods / "Authorized car pack"
        self.archive(path / "vehicle media/cars", "New_69", b"higher grip")
        self.archive(path / "vehicle media/cars", "Classic", b"replacement car")
        (path / "mod.json").write_text(json.dumps({"id": "cars", "format_version": 1, "media": "vehicle media"}))
        self.assertEqual(self.scan()["converted"], 2)
        new = scanner.target(self.mount, "New_69")
        self.assertEqual(new.read_bytes(), b"fitted higher grip")
        self.assertEqual(self.scan()["unchanged"], 2)
        (mods / "loadorder.txt").write_text("# Authorized car pack\n", encoding="utf8")
        retired = self.scan()
        self.assertEqual(retired["retired"], 1)
        self.assertEqual(retired["converted"], 1)
        self.assertFalse(new.exists())
        self.assertEqual(scanner.uninstall(self.cfg, self.state)["restored"], 1)
        self.assertFalse(scanner.target(self.mount, "Classic").exists())

    def test_existing_override_legacy_and_foreign_edit(self):
        self.archive(self.stock, "Old", b"legacy source")
        old = scanner.target(self.mount, "Old")
        old.parent.mkdir(parents=True)
        old.write_bytes(b"tested cockpit setup")
        hashes = self.root / "known.json"
        hashes.write_text(json.dumps({"old": [scanner.digest(b"legacy source"), "unused", scanner.digest(old.read_bytes())]}))
        other = scanner.target(self.mount, "Loose")
        other.parent.mkdir(parents=True)
        other.write_bytes(b"third party original")
        with patch.object(scanner, "KNOWN", hashes):
            result = self.scan()
            self.assertEqual((result["adopted"], result["converted"]), (1, 1))
            self.assertEqual(self.called, [("Loose", b"third party original")])
            self.archive(self.stock, "Old", b"updated car")
            self.assertEqual(self.scan()["converted"], 1)
            other.write_bytes(b"my later edit")
            self.assertEqual(self.scan()["skipped"], 1)
            with self.assertRaises(RuntimeError):
                scanner.uninstall(self.cfg, self.state)
            self.assertEqual(other.read_bytes(), b"my later edit")
            self.assertEqual(old.read_bytes(), b"tested cockpit setup")

    def test_recover_interrupted_write_and_unsupported(self):
        self.archive(self.stock, "A", b"body")
        with patch.object(scanner, "atomic", side_effect=OSError("disk full")):
            with self.assertRaises(OSError):
                self.scan()
        self.assertEqual(self.scan()["unchanged"], 1)
        self.assertEqual(scanner.target(self.mount, "A").read_bytes(), b"fitted body")
        self.archive(self.stock, "B", b"bad native curve")
        self.archive(self.stock, "Working", b"follow up")
        native_report = self.scan()
        self.assertIn("B", native_report["skipped_cars"])
        self.assertEqual(scanner.target(self.mount, "Working").read_bytes(), b"fitted follow up")
        self.archive(self.stock, "A", b"unsupported")
        self.assertIn("A", self.scan()["skipped_cars"])
        self.assertEqual(scanner.target(self.mount, "A").read_bytes(), b"fitted body")
        (self.stock / "A.zip").write_bytes(b"bad archive")
        self.archive(self.stock, "Builder's Special", b"new car")
        report = self.scan()
        self.assertIn("A", report["skipped_cars"])
        self.assertEqual(report["converted"], 1)
        self.assertEqual(scanner.target(self.mount, "A").read_bytes(), b"fitted body")


if __name__ == "__main__":
    unittest.main()
