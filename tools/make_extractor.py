"""The fake-PKG extraction helper (installer/Extractor, PT.PkgExtract) built for this machine: the LGPL LibOrbisPkg source,
patched as the installers ship it (tools/patch_liborbis_readers.py, docs/installer.md), then dotnet publish. The Windows
and Linux installers get theirs from tools/ci/release.py; this is the Mac's way (docs/macos.md, Running), `make extractor`.
Needs the .NET 10 SDK and git."""
import argparse
import platform
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LIBORBIS_GIT = "https://github.com/maxton/LibOrbisPkg.git"
# pinned like the other downloads (cmake/Dependencies.cmake): the commit the patch script and installer/Tests were checked
# against (docs/installer.md); a newer upstream fails in the patch step, by design
LIBORBIS_COMMIT = "643477263b2644e0803e0f58b8726ea4e3f3b7d4"


def host_rid():
    arm = platform.machine().lower() in ("arm64", "aarch64")
    if sys.platform == "darwin":
        return "osx-arm64" if arm else "osx-x64"
    if sys.platform == "win32":
        return "win-x64"
    return "linux-arm64" if arm else "linux-x64"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--liborbis", type=Path, help="a LibOrbisPkg clone (default: cloned into build/liborbis-upstream once)")
    parser.add_argument("--rid", default=host_rid(), help=".NET runtime identifier (default: this machine's)")
    args = parser.parse_args()
    build = REPO / "build"
    upstream = args.liborbis or build / "liborbis-upstream"
    if not (upstream / "LibOrbisPkg.Core" / "LibOrbisPkg.Core.csproj").exists():
        if args.liborbis:
            sys.exit(f"{upstream} is not a LibOrbisPkg source tree")
        subprocess.run(["git", "clone", "--no-checkout", LIBORBIS_GIT, str(upstream)], check=True)
        subprocess.run(["git", "-C", str(upstream), "checkout", "--quiet", LIBORBIS_COMMIT], check=True)
    head = subprocess.run(["git", "-C", str(upstream), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    if head and head != LIBORBIS_COMMIT:
        print(f"note: {upstream} is at {head[:7]}, the checked commit is {LIBORBIS_COMMIT[:7]}")
    # a private copy, patched: the clone itself is never changed (the same edits as release.py's liborbis_source)
    source = build / f"extractor-{args.rid}-src"
    if source.exists():
        shutil.rmtree(source)
    shutil.copytree(upstream, source, ignore=shutil.ignore_patterns("bin", "obj", ".git"))
    csproj = source / "LibOrbisPkg.Core" / "LibOrbisPkg.Core.csproj"
    csproj.write_text(csproj.read_text(encoding="utf-8-sig").replace("netcoreapp3.0", "net10.0").replace("<LangVersion>7.3</LangVersion>", "<LangVersion>latest</LangVersion>")
                      .replace("<GenerateSerializationAssemblies>Auto</GenerateSerializationAssemblies>", "<GenerateSerializationAssemblies>Off</GenerateSerializationAssemblies>"),
                      encoding="utf-8")
    subprocess.run([sys.executable, str(REPO / "tools" / "patch_liborbis_readers.py"), str(source)], check=True)
    out = build / f"extractor-{args.rid}"
    subprocess.run(["dotnet", "publish", str(REPO / "installer" / "Extractor" / "Extractor.csproj"), "-c", "Release", "-r", args.rid,
                    "--self-contained", "true", f"-p:LibOrbisSource={source}", "-o", str(out)], check=True)
    print(f"{out / 'PT.PkgExtract'} <P.T. package> <empty output directory>")


if __name__ == "__main__":
    main()
