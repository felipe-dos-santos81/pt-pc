"""Apply the extraction read fixes to the LGPL LibOrbisPkg corresponding source. prepare() also makes that source from a
clone, the way the installers ship it (docs/installer.md): tools/ci/release.py and tools/make_extractor.py both use it."""
import argparse
import shutil
from pathlib import Path

FIXES = [
    ("LibOrbisPkg/PFS/PFSCReader.cs", "ds.Read(output, 0, hdr.BlockSz);",
     "ds.ReadExactly(output, 0, hdr.BlockSz);"),
    ("LibOrbisPkg/Util/MemoryMapped.cs",
     "reader.Read((long)chunks[chunkIdx++]*chunkSize + offsetIntoChunk, buf, offset, count);",
     "reader.Read((long)chunks[chunkIdx++]*chunkSize + offsetIntoChunk, buf, offset, toReadFromChunk);"),
]


def patch(source: Path):
    for name, before, after in FIXES:
        path = source / name
        text = path.read_text(encoding="utf-8-sig")
        if after in text:
            print("Already patched:", name)
            continue
        if text.count(before) != 1:
            raise RuntimeError(f"Unexpected source version: {name}")
        path.write_text(text.replace(before, after), encoding="utf-8")
        print("Patched:", name)


def prepare(upstream: Path, dest: Path):
    """A private copy of the clone at dest, built for net10.0 and patched: the clone itself is never changed."""
    if dest.exists():
        shutil.rmtree(dest)
    shutil.copytree(upstream, dest, ignore=shutil.ignore_patterns("bin", "obj", ".git"))
    csproj = dest / "LibOrbisPkg.Core" / "LibOrbisPkg.Core.csproj"
    csproj.write_text(csproj.read_text(encoding="utf-8-sig").replace("netcoreapp3.0", "net10.0").replace("<LangVersion>7.3</LangVersion>", "<LangVersion>latest</LangVersion>")
                      .replace("<GenerateSerializationAssemblies>Auto</GenerateSerializationAssemblies>", "<GenerateSerializationAssemblies>Off</GenerateSerializationAssemblies>"),
                      encoding="utf-8")
    patch(dest)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("source", type=Path)
    patch(p.parse_args().source)
