"""The fake-PKG extraction helper (installer/Extractor, PT.PkgExtract) built for this machine: the LGPL LibOrbisPkg source,
patched as the installers ship it (tools/patch_liborbis_readers.py, docs/installer.md), then dotnet publish. The Windows
and Linux installers get theirs from tools/ci/release.py; this is the Mac's way (docs/macos.md, Running), `make extractor`.
Needs the .NET 10 SDK and git."""
import argparse
import platform
import subprocess
import sys
from pathlib import Path

from patch_liborbis_readers import prepare

REPO = Path(__file__).resolve().parent.parent
LIBORBIS_GIT = "https://github.com/maxton/LibOrbisPkg.git"
# pinned like the other downloads (cmake/Dependencies.cmake): the commit the patch script and installer/Tests were checked
# against (docs/installer.md); a newer upstream fails in the patch step, by design
LIBORBIS_COMMIT = "643477263b2644e0803e0f58b8726ea4e3f3b7d4"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--liborbis", type=Path, help="a LibOrbisPkg clone (default: cloned into build/liborbis-upstream once)")
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
    source = build / "liborbis-src"
    prepare(upstream, source)
    # the runtime of this Mac (an Intel Mac builds osx-x64); the output folder carries it
    rid = "osx-arm64" if platform.machine().lower() == "arm64" else "osx-x64"
    out = build / f"extractor-{rid}"
    subprocess.run(["dotnet", "publish", str(REPO / "installer" / "Extractor" / "Extractor.csproj"), "-c", "Release", "-r", rid,
                    "--self-contained", "true", f"-p:LibOrbisSource={source}", "-o", str(out)], check=True)
    print(f"{out / 'PT.PkgExtract'} <P.T. package> <empty output directory>")


if __name__ == "__main__":
    main()
