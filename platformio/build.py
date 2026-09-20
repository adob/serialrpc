"""Compile serialrpc with buildtool so its baselib module imports resolve on the MCU."""

Import("env")

import os
from pathlib import Path
import sys
from SCons.Errors import UserError

from platformio.package.manager.library import LibraryPackageManager
from platformio.package.meta import PackageSpec

root = Path(env.Dir('.').srcnode().abspath).parent
try:
    Import("projenv")
except UserError:
    projenv = None


def dependency(name: str) -> Path:
    """Locate an installed dependency, preferring the project's explicit lib_deps spec."""
    # Resolve through the package manager, including symlink dependencies;
    # GetLibBuilders() is incomplete while this library script is loading.
    requested = name
    for spec in map(PackageSpec, env.GetProjectOption('lib_deps', [])):
        if spec.name == name:
            requested = spec
            break
    for storage in env.GetLibSourceDirs():
        package = LibraryPackageManager(storage).get_package(requested)
        if package:
            return Path(package.path)
    raise RuntimeError(f'{name} dependency is missing; run pio pkg install')


buildtool = dependency('buildtool')
if not (buildtool / 'platformio_adapter.py').is_file():
    raise RuntimeError(f'buildtool at {buildtool} lacks the PlatformIO adapter; update the dependency')
baselib = dependency('baselib')

# Public headers are included as "serialrpc/<header>" regardless of the checkout
# directory name. Publish an include prefix that maps that name onto this checkout.
include = Path(env.subst('$BUILD_DIR')) / 'serialrpc-include'
include.mkdir(parents=True, exist_ok=True)
link = include / 'serialrpc'
if not (link.is_symlink() and os.readlink(link) == str(root)):
    if link.is_symlink() or link.exists():
        link.unlink()
    link.symlink_to(root)
env.Append(CPPPATH=[str(include)])
if projenv is not None:
    projenv.Append(CPPPATH=[str(include)])

if projenv is not None:
    sys.path.insert(0, str(buildtool))
    from platformio_adapter import configure
    configure(env, projenv, root, sources=[root / 'encoding_impl.cc'],
              search_roots=[baselib], module_roots={'serialrpc': root})
