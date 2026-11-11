# obsidian-mcp: Developer Reference

User installation, configuration and compatibility status are in the
[README](../README.md). This reference preserves implementation details,
alternative transport schemas and validation commands. A client configuration
example is not a claim of verified July/OAuth interoperability.

Current HTTP source builds use local slimr and libhv overlays. The pinned
published registry baseline predates these HTTP changes; do not claim that
baseline builds the current HTTP app without overrides. CI configuration is
not evidence that a runner/platform/client has passed.

## App Architecture

The standalone Streamable HTTP server, stdio MCP server, and JSON CLI are implemented in C++26 on `slimr-cpp`.

```text
plugins/obsidian-mcp/
├── vcpkg.json                # Independently resolved dependencies (`slimr-mcp[http]`, `slimr-test`)
├── CMakeLists.txt            # App/test targets linking `slimr::mcp_http`
├── justfile                  # Thin CMake/Ninja, lint, test, and Homebrew lifecycle recipes
├── Formula/
│   └── obsidian-mcp.rb       # Homebrew formula + launchd service on 127.0.0.1:8081
├── .cursor-plugin/
│   └── plugin.json           # Cursor plugin manifest -> http://127.0.0.1:8081/mcp
├── .claude-plugin/
│   ├── plugin.json           # Claude Code plugin manifest -> http://127.0.0.1:8081/mcp
│   └── marketplace.json      # Claude Code marketplace catalog
├── docs/
│   └── conventions.md        # Note conventions served as `obsidian://conventions`
├── scripts/
│   ├── dev-formula.sh        # Safe local formula staging + automatic backup restore
│   └── test-mcp.mjs          # Native MCP 2026-07-28 stdio + ephemeral HTTP conformance check
├── src/
│   ├── app.hpp               # Vault path isolation, frontmatter/tag parser, search/rename engine, and `make_obsidian_app()`
│   └── main.cpp              # Standalone CLI, stdio (`mcp`), and Streamable HTTP (`http`) entrypoint
├── tests/
│   └── test_main.cpp         # Unit, temp-vault, HTTP, CLI, and concurrency tests
└── build/bin/
    └── obsidian-mcp          # Standalone release binary
```

### Libraries Used

- **`<mcp.hpp>`, `<mcp_http.hpp>`, & `<reflect.hpp>`:** Typed tool definitions, compile-time JSON Schema generation, narrow JSON CLI, stdio NDJSON MCP `2026-07-28` server, Streamable HTTP (`libhv`), and `obsidian://conventions` resource registration.
- **`<glaze/yaml.hpp>`:** YAML frontmatter parsing (`aliases`, `tags`, key-value filters).
- **`<json.hpp>` (`glaze`):** RFC-8259 JSON serialization.
- **`<bugsink.hpp>` (`-lcurl`):** Optional exception reporting configured with `BUGSINK_DSN`.
- **`<test.hpp>` (`doctest`):** Temp-vault fixture testing with `-fsanitize=address,undefined`.
- **C++26 Standard Library:** `<filesystem>`, `<shared_mutex>`, `<regex>` (wikilink/embed/markdown link rewriting).

## Client Configuration

### Streamable HTTP (Recommended — `http://127.0.0.1:8081/mcp`)

Start the background service (`just install-local && just start` or `brew services start bdombro/obsidian-mcp/obsidian-mcp`), or run `./build/bin/obsidian-mcp http`.

**Claude Code (`.mcp.json` / plugin config):**

```json
{
  "mcpServers": {
    "obsidian-mcp": {
      "type": "http",
      "url": "http://127.0.0.1:8081/mcp"
    }
  }
}
```

**Cursor (`.cursor/mcp.json` / plugin config):**

```json
{
  "mcpServers": {
    "obsidian-mcp": {
      "url": "http://127.0.0.1:8081/mcp"
    }
  }
}
```

Plugin marketplace manifests are included in `.cursor-plugin/plugin.json`, `.claude-plugin/plugin.json`, and `.claude-plugin/marketplace.json`.

### Stdio Transport (`.mcp.json`)

```json
{
  "mcpServers": {
    "obsidian-mcp": {
      "command": "/absolute/path/to/obsidian-mcp/build/bin/obsidian-mcp",
      "args": ["mcp"],
      "env": {
        "OBSIDIAN_VAULT": "/absolute/path/to/vault"
      }
    }
  }
}
```

## Building & Testing

Use Clang with C++26 and libc++ FAST hardening, CMake 3.31+, Ninja, [just](https://just.systems), a bootstrapped [vcpkg](https://vcpkg.io), Node.js (for `scripts/test-mcp.mjs`), and a compatible clang-tidy (override with `CLANG_TIDY`). Set `VCPKG_ROOT` and put vcpkg and the tools on `PATH`. Linux additionally needs libc++ and libc++abi; select libc++ with `CXXFLAGS=-stdlib=libc++`.

Dependencies are declared in `vcpkg.json`. During local development, `(cd /absolute/path/to/slimr-cpp && just publish --local --all)` publishes ports into vcpkg's local overlay; export `VCPKG_OVERLAY_PORTS="$VCPKG_ROOT/local-ports"` in your shell. `just deps` installs into `build/deps/` for the detected native triplet. `configure` always runs it, with CMake manifest installation disabled. CMake links installed `slimr::mcp_http` and `slimr::test` targets, never source-header overrides.

```bash
just test      # Debug ASan/UBSan tests against isolated vault fixtures + HTTP/concurrency tests
just build     # Optimized IPO Release app and tests; build/bin/obsidian-mcp
just lint      # App and test translation units using the Debug compilation database (${CLANG_TIDY:-clang-tidy})
just test-mcp  # Native MCP 2026-07-28 stdio + ephemeral HTTP server/discover & tools/list check
just run status
just clean     # Remove build/ only; leaves legacy dist/ and user data untouched
```

Debug and Release graphs live in `build/cmake/Debug/` and `build/cmake/Release/`; only Release apps are emitted to `build/bin/`. Set `CXX` before the first configure to choose a compiler; CMake retains its absolute path even when lint uses a different `PATH`. Both configurations enable libc++ hardening and zero automatic-variable initialization; Debug uses fatal UBSan reports. `test-mcp` isolates HOME/XDG directories in a temp directory, clears credentials, disables error reporting, and cleans up afterward; it never invokes mutating tools or opens the live vault.

CI runs the same commands on native Linux/macOS. Configure `VCPKG_REF` to the tested full vcpkg commit SHA; slimr packages resolve from the public registry pinned in `vcpkg-configuration.json`, without a dependency token. Local macOS validation is not Linux execution evidence.
