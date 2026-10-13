# Building

Requires devkitPro (devkitA64 and switch-tools), Python 3.10+, Git, Make and a C++20 host compiler with ASan/UBSan support.

```sh
git clone --recurse-submodules https://github.com/chuthree/nx-gamehide.git
cd nx-gamehide
make dependencies
make -j2 package
```

`make package` runs tests and checks the binaries and installation ZIP.

Build files go to `~/.cache/nx-gamehide`; packages go to `dist/`. `make clean` removes project build files and keeps the dependencies.

## Installation package

`NX-GameHide-v<version>.zip` contains installation files and license notices. Config files are created at runtime.

## Source

For a released version, check out its tag before building (for example, `git checkout v0.1.0`), then run `git submodule update --init --recursive` and the build commands above.

Project source and build scripts are in this repository. The libtesla headers are provided by the pinned `overlay/libtesla` submodule. [scripts/dependencies.json](../scripts/dependencies.json) records the upstream source URLs, exact revisions and licenses for Atmosphère, libnx and libtesla.

`make dependencies` downloads the pinned Atmosphère and libnx sources into `~/.cache/nx-gamehide/deps/` and builds them locally. Their source trees are retained there; they are not included in the installation ZIP. GitHub's automatic source archives omit submodule contents, so use the recursive Git clone above when building.
