# obsidian-mcp: Developer Reference

User installation, configuration and compatibility status are in the
[README](../README.md). This reference covers implementation details,
alternative transport schemas, and build/test/release workflows. A client
configuration example is not a claim of verified July/OAuth interoperability.

CI configuration is not evidence that a runner/platform/client has passed.

## App Architecture

The standalone Streamable HTTP server and JSON CLI are implemented in C++26 on `slimr-mcp`.

```text
plugins/obsidian-mcp/
├── vcpkg.json                # Dependencies (`slimr-mcp[http]`, `slimr-test`)
├── vcpkg-configuration.json  # Pinned vcpkg builtin and `slimr-*` registries
├── CMakeLists.txt            # App/test targets linking `slimr::mcp_http`
├── justfile                  # Thin CMake/Ninja, lint, test, and Homebrew lifecycle recipes
├── VERSION                   # Single source of truth for `--version` and Bugsink `release`
├── CHANGELOG.md              # Release notes under `## Unreleased`
├── Formula/
│   └── obsidian-mcp.rb       # Homebrew formula + service (`--log-file`, `etc/obsidian-mcp/env`)
├── .cursor-plugin/
│   └── plugin.json           # Cursor plugin manifest -> http://127.0.0.1:10001/mcp
├── .claude-plugin/
│   ├── plugin.json           # Claude Code plugin manifest -> http://127.0.0.1:10001/mcp
│   └── marketplace.json      # Claude Code marketplace catalog
├── docs/
│   ├── conventions.md        # Note conventions served as `obsidian://conventions`
│   └── development.md        # Developer reference
├── scripts/
│   ├── doctor.sh             # Toolchain & environment check
│   ├── formula-shared.ts     # Shared release/formula helpers
│   ├── release.ts            # Version bump, changelog, formula & release helper
│   └── test-mcp.ts           # Native MCP 2026-07-28 ephemeral HTTP conformance check
├── triplets/
│   ├── arm64-osx.cmake       # macOS 15.0 deployment target overlay triplet
│   └── x64-linux.cmake       # Linux Homebrew llvm@23 libc++ overlay triplet
├── src/
│   ├── app.hpp               # Vault path isolation, frontmatter/tag parser, search/rename engine, and `make_obsidian_app()`
│   ├── log_file.hpp          # `--log-file` redirection and 10 MiB log rotation
│   └── main.cpp              # Standalone CLI and Streamable HTTP (`http`) entrypoint
├── tests/
│   ├── test_log_file.hpp     # Log rotation unit tests
│   └── test_main.cpp         # Unit, temp-vault, HTTP, CLI, and concurrency tests
└── build/bin/
    └── obsidian-mcp          # Standalone release binary
```

### Libraries Used

- **`<mcp.hpp>`, `<mcp_http.hpp>`, & `<reflect.hpp>`:** Typed tool definitions, compile-time JSON Schema generation, narrow JSON CLI, Streamable HTTP (`cpp-httplib`), and `obsidian://conventions` resource registration.
- **`<glaze/yaml.hpp>`:** YAML frontmatter parsing (`aliases`, `tags`, key-value filters).
- **`<json.hpp>` (`glaze`):** RFC-8259 JSON serialization.
- **`<bugsink.hpp>`:** Optional exception reporting configured with `BUGSINK_DSN`.
- **`<test.hpp>` (`doctest`):** Temp-vault fixture testing with `-fsanitize=address,undefined`.
- **C++26 Standard Library:** `<filesystem>`, `<shared_mutex>`, `<regex>` (wikilink/embed/markdown link rewriting).

## Client Configuration

### Streamable HTTP (`http://127.0.0.1:10001/mcp`)

Start the background service (`just install` or `brew services start bdombro/obsidian-mcp/obsidian-mcp`), or run `./build/bin/obsidian-mcp http --port 20001` for local development.

**Claude Code (`.mcp.json` / plugin config):**

```json
{
  "mcpServers": {
    "obsidian-mcp": {
      "type": "http",
      "url": "http://127.0.0.1:10001/mcp"
    }
  }
}
```

**Cursor (`.cursor/mcp.json` / plugin config):**

```json
{
  "mcpServers": {
    "obsidian-mcp": {
      "url": "http://127.0.0.1:10001/mcp"
    }
  }
}
```

Plugin marketplace manifests are included in `.cursor-plugin/plugin.json`, `.claude-plugin/plugin.json`, and `.claude-plugin/marketplace.json`.

## Building & Testing

Run `just doctor` to verify required tools (`brew`, `bun`, `cmake`, `gh`, `git`, `jq`, `just`, `ninja`, `vcpkg`, `LLVM_BIN` from `llvm@23`, and a C++26 compiler).

- **macOS:** uses Homebrew `llvm@23` (`clang`/`clang++`) with static `libc++`, symbol hiding, and `CMAKE_OSX_DEPLOYMENT_TARGET=15.0` (`LLVM_BIN` defaults to `$(brew --prefix llvm@23)/bin`).
- **Linux (`ubuntu-24.04`):** uses Homebrew `llvm@23` (`clang++` with `-stdlib=libc++`, static libc++ in Release builds) and Homebrew `brew services` via `systemd --user` (`loginctl enable-linger "$USER"`).

Dependencies are declared in `vcpkg.json` and resolved from the `slimr-*` registries pinned in `vcpkg-configuration.json`. `just deps` installs into `build/deps/` for the detected native triplet (`triplets/arm64-osx.cmake` or `triplets/x64-linux.cmake`). `just upgrade-deps` pulls `$VCPKG_ROOT`, updates baselines, and runs `just test`.

```bash
just doctor    # Check toolchain, C++26 support, and environment
just test      # Debug ASan/UBSan tests (optional doctest filter: just test "case")
just build     # Optimized IPO Release binary at build/bin/obsidian-mcp (no tests)
just fmt       # Format C++ sources and justfile
just lint      # Format check + clang-tidy bug-finding checks
just test-mcp  # Native MCP 2026-07-28 ephemeral HTTP conformance check
just clean     # Remove build/
```

For a local foreground dev loop without touching the installed service on port `10001`:

```bash
./build/bin/obsidian-mcp http --port 20001
```

## Develop against an unreleased lib

Add `"overlay-ports"` to the consumer's `vcpkg-configuration.json`, then run `just test` or `just build` as usual (add one entry per unreleased lib):

```json
{
  "overlay-ports": [
    "<relative path to ~/dev/slimr/<name>/ports/slimr-<name>>"
  ]
}
```

Remove `"overlay-ports"` and bump the registry baseline once the lib is released. Never commit or release with `overlay-ports` set.

## Releasing & Installing

Trigger the GitHub release workflow (`test.yml` on macOS + Linux, version bump, `CHANGELOG.md` update, formula SHA updates, tag, and asset upload), then pull the release commit:

```bash
just release patch   # or minor / major
just install         # Tap, install, brew test, and start service
```
