set shell := ["bash", "-eu", "-o", "pipefail", "-c"]
set positional-arguments

formula := "obsidian-mcp"
endpoint := "http://127.0.0.1:10001/mcp"
tools := "read,search,list,write,append,rename,status"
test_target := "obsidian_mcp_test"
platforms := "macos linux"
required := "brew bun cmake gh git jq just ninja vcpkg"
log := `brew --prefix` + "/var/log/" + formula + ".log"
llvm := env("LLVM_BIN", `brew --prefix llvm@23` + "/bin")
cxx := env("CXX", llvm + "/clang++")
triplet := replace(replace(arch(), "aarch64", "arm64"), "x86_64", "x64") + if os() == "macos" { "-osx" } else { "-linux" }
deps_env := "CC=\"" + llvm + "/clang\" CXX=\"" + cxx + "\" LIBCXX_DIR=\"$(dirname \"$(\"" + cxx + "\" -print-file-name=" + (if os() == "macos" { "c++/libc++.a" } else { "libc++.so" }) + ")\")\" "
discover := '{"jsonrpc":"2.0","id":1,"method":"server/discover","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientCapabilities":{}}}}'

_:
    @just --list

# Poll the endpoint until server/discover answers
_wait-ready:
    for _ in $(seq 30); do just _probe >/dev/null 2>&1 && exit 0; sleep 0.2; done; echo "Timed out waiting for {{ endpoint }}" >&2; exit 1

# POST server/discover to the endpoint
_probe:
    curl -fsS -X POST "{{ endpoint }}" -H "Content-Type: application/json" -H "Accept: application/json, text/event-stream" -H "MCP-Protocol-Version: 2026-07-28" -H "Mcp-Method: server/discover" -d '{{ discover }}'

# Compile the Release binary into build/bin (no tests)
build: (configure "Release")
    cmake --build build/cmake/Release --parallel 2

# Remove build output
clean:
    rm -rf build

# Configure CMake (Debug enables ASan + UBSan)
configure configuration="Debug": deps
    CC="{{ llvm }}/clang" CXX="{{ cxx }}" cmake -S . -B build/cmake/{{ configuration }} -G Ninja -DCMAKE_BUILD_TYPE={{ configuration }} -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_MANIFEST_MODE=OFF -DVCPKG_INSTALLED_DIR="{{ justfile_directory() }}/build/deps" -DVCPKG_TARGET_TRIPLET={{ triplet }} -DENABLE_SANITIZERS={{ if configuration == "Debug" { "ON" } else { "OFF" } }}

# Follow the service log
daemon-logs:
    tail -n 100 -f "{{ log }}"

# Restart the service and wait until ready
daemon-restart:
    brew services stop {{ formula }}
    brew services start {{ formula }}
    just _wait-ready

# Register the service to run at login, start it, and wait until ready
daemon-start:
    brew services start {{ formula }}
    just _wait-ready

# Show brew service state and probe the live endpoint
daemon-status:
    brew services info {{ formula }} || true
    just _probe

# Stop the service and unregister it, so it won't start at login
daemon-stop:
    brew services stop {{ formula }}

# Install vcpkg dependencies into build/deps
deps:
    {{ deps_env }}vcpkg install --x-wait-for-lock --overlay-triplets=triplets --triplet={{ triplet }} --x-install-root=build/deps

# Check required tools, C++26 support and env vars, printing fixes
doctor:
    LLVM_BIN="{{ llvm }}" bash scripts/doctor.sh {{ required }}

# Format C++ sources and this justfile
fmt:
    git ls-files -co --exclude-standard '*.cpp' '*.hpp' | xargs "{{ llvm }}/clang-format" -i
    just --fmt --unstable

# Clean-install the released binary from GitHub exactly as users do, test it, then start the service
install: uninstall
    brew tap bdombro/{{ formula }} https://github.com/bdombro/{{ formula }}
    brew install bdombro/{{ formula }}/{{ formula }}
    brew test bdombro/{{ formula }}/{{ formula }}
    just daemon-start

# Check formatting, then lint with clang-tidy
lint: (configure "Debug")
    git ls-files -co --exclude-standard '*.cpp' '*.hpp' | xargs "{{ llvm }}/clang-format" --dry-run --Werror
    just --fmt --check --unstable
    "{{ llvm }}/clang-tidy" -p build/cmake/Debug src/main.cpp

# Release on GitHub (tests per platform, version bump, changelog, build, formula, tag), then pull the release commit
release bump:
    gh workflow run release.yml -f bump="$1"
    sleep 5; id="$(gh run list --workflow release.yml --limit 1 --json databaseId -q '.[0].databaseId')"; if [ -t 1 ]; then gh run watch --compact --exit-status "$id"; else gh run watch --exit-status "$id" >/dev/null; fi || { gh run view "$id" --log-failed | tail -n 100; exit 1; }
    git pull --ff-only

# Debug build with ASan + UBSan, then run all tests, or only doctest cases matching filter
test filter="": (configure "Debug")
    if [ -z "$1" ]; then cmake --build build/cmake/Debug --parallel 2 && BUGSINK_DSN= ctest --test-dir build/cmake/Debug --output-on-failure --no-tests=error; else cmake --build build/cmake/Debug --target {{ test_target }} --parallel 2 && BUGSINK_DSN= build/cmake/Debug/{{ test_target }} -tc="$1"; fi

# Isolated MCP stdio + HTTP startup check against build/bin
test-mcp: build
    bun scripts/test-mcp.ts build/bin/{{ formula }} {{ tools }}

# Stop the service, uninstall the formula, remove the tap
uninstall:
    brew services stop {{ formula }} || true
    brew uninstall {{ formula }} || true
    brew untap bdombro/{{ formula }} || true

# Move vcpkg registry baselines to latest, then run tests (no commit)
upgrade-deps:
    git -C "$VCPKG_ROOT" pull --ff-only
    vcpkg x-update-baseline --x-install-root=build/deps
    just test
