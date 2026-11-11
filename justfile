set shell := ["zsh", "-f", "-eu", "-o", "pipefail", "-c"]
set positional-arguments

architecture := replace(replace(arch(), "aarch64", "arm64"), "x86_64", "x64")
system := if os() == "macos" { "osx" } else { os() }
triplet := f'{{architecture}}-{{system}}'
compiler := if os() == "macos" { "/usr/bin/clang++" } else { "clang++" }

_:
    @just --list

deps:
    vcpkg install --x-wait-for-lock --triplet={{triplet}} --x-install-root=build/deps

configure configuration="Debug": deps
    CXX="${CXX:-{{compiler}}}" cmake -S . -B build/cmake/{{configuration}} -G Ninja \
        -DCMAKE_BUILD_TYPE={{configuration}} \
        -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
        -DVCPKG_MANIFEST_MODE=OFF \
        -DVCPKG_INSTALLED_DIR="{{justfile_directory()}}/build/deps" \
        -DVCPKG_TARGET_TRIPLET={{triplet}} \
        -DENABLE_SANITIZERS={{if configuration == "Debug" { "ON" } else { "OFF" }}}

test: (configure "Debug")
    cmake --build build/cmake/Debug --parallel 2
    ctest --test-dir build/cmake/Debug --output-on-failure --no-tests=error

build: (configure "Release")
    cmake --build build/cmake/Release --parallel 2
    ctest --test-dir build/cmake/Release --output-on-failure --no-tests=error

lint: (configure "Debug")
    clang-tidy -p build/cmake/Debug src/main.cpp tests/test_main.cpp

test-mcp: build
    node scripts/test-mcp.mjs build/bin/obsidian-mcp read,search,list,write,append,rename,status

inspect:
    npx --yes @modelcontextprotocol/inspector@2.14.0 ./build/bin/obsidian-mcp mcp

run *args:
    ./build/bin/obsidian-mcp "$@"

clean:
    rm -rf build
