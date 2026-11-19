"""Package a built installer with the sources used to make it."""
import hashlib
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parent
DIST = ROOT / "dist"
ASSET = DIST / "FH2-Drift-Assist-0.5.2-win-x64.zip"


def build():
    docs = ["README.md", "README.zh-Hant.md", "THIRD-PARTY-NOTICES.md", "LICENSE"]
    licenses = sorted(p.relative_to(ROOT) for p in (ROOT / "licenses").glob("*.txt"))
    source = [Path(n) for n in [*docs, "build.ps1", "package.py", "RELEASE.md", ".gitignore"]]
    source.extend(licenses)
    for folder, extensions in (("src", {".c", ".h", ".def", ".sql"}), ("installer", {".cs", ".xml"}),
                               ("tests", {".c", ".py"}), ("preset", {".ini"})):
        source.extend(sorted(p.relative_to(ROOT) for p in (ROOT / folder).rglob("*") if p.suffix in extensions))
    with ZipFile(ASSET, "w", ZIP_DEFLATED, compresslevel=9) as z:
        z.write(DIST / "Install.exe", "Install.exe")
        for name in [*docs, *licenses]:
            z.write(ROOT / name, name)
        for name in source:
            z.write(ROOT / name, Path("source") / name)
    with ZipFile(ASSET) as z:
        assert z.testzip() is None and len(z.namelist()) == len(set(z.namelist()))
    digest = hashlib.sha256(ASSET.read_bytes()).hexdigest()
    ASSET.with_suffix(".zip.sha256").write_text(f"{digest}  {ASSET.name}\n", encoding="ascii")
    print(f"{ASSET.name}  {digest}")


if __name__ == "__main__":
    build()
