# obsidian-mcp

```
     .--------.
    /  .---.   \
   |  /     \   |
   | |   O   |   |
    \  '---'   /
     '--------'
```

Search, read, and safely edit notes in your Obsidian vault through MCP (`2026-07-28`). Find notes by title, alias, tag, or content; rename a note and update its backlinks across the vault.

Unlike tools tied to the Obsidian UI, this MCP works directly with Markdown files, so Obsidian does not need to be running. It runs standalone over **Streamable HTTP** (`http://127.0.0.1:8081/mcp`), **stdio** (`mcp`), or as a narrow JSON **CLI** without a separate plugin, background indexer, or Node/V8 runtime.

> **Breaking change (`2026-07-28`):** `obsidian-mcp` implements the MCP `2026-07-28` contract directly (`server/discover`, per-request `_meta`, `resultType`, and HTTP header reflection). Legacy `initialize`, `ping`, HTTP session IDs, GET SSE streams, and unified gateway prefixes (`notes_*`) are removed.

## Features

- Search by keywords, aliases, tags, folders, and frontmatter; read and list notes.
- Create, append to, or rename notes, with vault-wide link updates on rename.
- Enforces vault path boundaries and refuses paths that escape the vault.
- Concurrent HTTP reads (`read`, `list`, `search`, `status`) synchronized with exclusive vault writes via `std::shared_mutex`.
- Small footprint: on-demand local search with no resident indexer or Node/V8 runtime.

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

## Homebrew Formula & Service Lifecycle

`Formula/obsidian-mcp.rb` installs `obsidian-mcp` and configures a `launchd` service running:

```text
obsidian-mcp http --host 127.0.0.1 --port 8081
```

Logs are written to `$(brew --prefix)/var/log/obsidian-mcp.log`. No secrets are embedded in the formula. Under `launchd`, `HOME` is provided by the user agent environment (`$HOME/Documents/Notes` default vault); configure `OBSIDIAN_VAULT` or `OBSIDIAN_CONVENTIONS_FILE` in the user environment or XDG config (`~/.config/obsidian/conventions.md`) when overriding defaults.

```bash
just install-local    # Build Release binary, stage local dev formula safely, brew install/reinstall
just reinstall-local  # Fast rebuild + binary swap into existing Cellar keg
just start            # brew services start bdombro/obsidian-mcp/obsidian-mcp
just stop             # brew services stop bdombro/obsidian-mcp/obsidian-mcp
just restart          # brew services restart bdombro/obsidian-mcp/obsidian-mcp
just status           # Check brew service state and probe http://127.0.0.1:8081/mcp
just uninstall        # Stop service, uninstall formula, untap
```

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

## Architecture & Vault Isolation

- **Default vault path:** `$HOME/Documents/Notes` (override with `OBSIDIAN_VAULT`). Missing `HOME` is an error only when a vault operation needs the default.
- **Path containment:** Every note path is canonicalized relative to the vault root (`std::filesystem::canonical`). Any path resolving outside the vault (`..`, absolute paths, symlinks pointing outside the vault) or into hidden directories (`.obsidian`, `.trash`, `.git`) is strictly refused.
- **No background index:** Searches scan the vault at query time, parsing YAML frontmatter (`aliases`, `tags`, arbitrary `key: value` metadata) and inline `#tags` (including nested `#parent/child` tags) on the fly.
- **Conventions resource:** Serves `obsidian://conventions`, also readable through `mcp_resource_read`. On resource reads only, it loads `OBSIDIAN_CONVENTIONS_FILE`, otherwise `$XDG_CONFIG_HOME/obsidian/conventions.md` or `$HOME/.config/obsidian/conventions.md`. A missing default private file uses the embedded public guide (`docs/conventions.md`); a configured missing or unreadable file fails visibly. Discovery never reads the vault or creates configuration files. Keep private conventions outside the repository with owner-only permissions.

## Tools & CLI Commands

Every tool is exposed over Streamable HTTP (`obsidian-mcp http`), stdio (`obsidian-mcp mcp`), and on the CLI as `obsidian-mcp <tool> ['{"arg":"val"}']`.

| Tool | Mutating | Description |
| --- | --- | --- |
| `read` | No | Read one note by vault-relative path, filename, title, or frontmatter alias (resolved in that order, matching Obsidian's link resolution). Returns raw content, body, and parsed YAML frontmatter. |
| `search` | No | Keyword search (case-insensitive ANDed terms + `"quoted phrases"`) across note titles, aliases, and bodies, with optional `folder`, `tag` (frontmatter and inline `#tag`, supporting nested tags), and `frontMatter` (`key=value`) filters. Returns ranked matches with score and contextual snippet lines. |
| `list` | No | List notes in a folder (optionally `recursive: true`) with titles, frontmatter keys, and immediate subfolders. |
| `write` | Yes | Create a new note (automatically creating parent folders inside the vault). Replacing an existing note requires `overwrite: true`. |
| `append` | Yes | Append text to the end of a note, or at the end of a specific Markdown heading section (`heading: "## Section"`). |
| `rename` | Yes | Rename or move a note and automatically rewrite every `[[wikilink]]`, `![[embed]]`, `[[Note#Heading]]`, `[[Note|Alias]]`, and `[markdown](link.md)` reference across the entire vault. Supports `aliases`, `keepOldTitle`, and `dryRun: true`. |
| `status` | No | Returns plugin version. |

### CLI & Server Examples

```bash
# Search with quoted phrase and folder filter
./build/bin/obsidian-mcp search '{"query":"\"open evening\" maynooth","folder":"Places","limit":5}'

# Read by title or alias
./build/bin/obsidian-mcp read '{"note":"Skoda Kodiaq"}'

# Preview a rename across all vault backlinks without writing
./build/bin/obsidian-mcp rename '{"from":"Old Title","to":"Places/New Title","dryRun":true}'

# Append to a specific heading section
./build/bin/obsidian-mcp append '{"note":"Home.md","heading":"Maintenance","text":"- Serviced boiler"}'

# Status (no JSON argument needed when all fields are optional)
./build/bin/obsidian-mcp status

# Start stdio MCP server
./build/bin/obsidian-mcp mcp

# Start Streamable HTTP server (defaults to 127.0.0.1:8081)
./build/bin/obsidian-mcp http
./build/bin/obsidian-mcp http --host 127.0.0.1 --port 0 --allow-ephemeral-port
```

## Environment Variables

| Variable | Default | Purpose |
| --- | --- | --- |
| `OBSIDIAN_VAULT` | `$HOME/Documents/Notes` | Root directory of the Obsidian Markdown vault |
| `OBSIDIAN_CONVENTIONS_FILE` | XDG/private file, then embedded guide | Optional private conventions override, loaded only on resource reads |
| `BUGSINK_DSN` | Unset (disabled) | Sentry-compatible error reporting DSN (`""` disables reporting) |

## macOS Build Prerequisites (vcpkg setup)

Install [Homebrew](https://brew.sh) and current Xcode Command Line Tools
(`xcode-select --install`; finish installation before continuing), or full Xcode.
Apple Clang 21 is the verified C++26/libc++ toolchain; older installations need
upgrading. These instructions assume Apple Silicon Homebrew at `/opt/homebrew`.

Add this environment snippet to `~/.zshrc`, then open a new terminal (or run
`source ~/.zshrc`). Use `~/.zshrc`, not `~/.zshenv`, to avoid changing
non-interactive zsh scripts:

`local-ports` is the dedicated vcpkg overlay directory for local development
linking: `just publish --local --all` registers symlinks to your slimr-cpp checkout,
overriding published packages without making a registry release. See the
[publishing guide on slimr-cpp](https://github.com/bdombro/slimr-cpp/blob/main/docs/publishing.md#local-environment).

```bash
export VCPKG_ROOT="$HOME/.local/share/vcpkg"
export VCPKG_OVERLAY_PORTS="$VCPKG_ROOT/local-ports"
# Use your existing workspace checkout instead when available.
export SLIMR_CPP="${SLIMR_CPP:-$HOME/dev/bdombro/slimr-cpp}"
export CXX="/usr/bin/clang++"
export CLANG_TIDY="/opt/homebrew/opt/llvm/bin/clang-tidy"
export PATH="$VCPKG_ROOT:/opt/homebrew/opt/ruby/bin:/opt/homebrew/bin:$PATH"
```

Then install and build from the repository root:

```bash
brew install cmake ninja just llvm node ruby autoconf automake libtool pkgconf
mkdir -p "$(dirname "$VCPKG_ROOT")"
if [ ! -d "$VCPKG_ROOT/.git" ]; then
  git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
fi
"$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
mkdir -p "$(dirname "$SLIMR_CPP")"
if [ ! -d "$SLIMR_CPP/.git" ]; then
  git clone https://github.com/bdombro/slimr-cpp.git "$SLIMR_CPP"
fi
(cd "$SLIMR_CPP" && just publish --local --all)
ln -s "$SLIMR_CPP/ports/libhv" "$VCPKG_OVERLAY_PORTS/libhv"

"$CXX" --version
vcpkg version
BUGSINK_DSN="" just test
just build
```

The libhv link is a one-time step: if it exists, verify that it points to the
chosen checkout's `ports/libhv` before skipping the command. Never overwrite
a conflicting port directory or modify installed headers/caches. Current HTTP
builds need both local slimr overlays and the separately pinned libhv port;
the older published registry baseline alone is not an HTTP build substitute.
`just deps` installs native dependencies into `build/deps/`; no separate
Homebrew curl/OpenSSL library installation is required.

Bootstrapping puts the executable at `$VCPKG_ROOT/vcpkg`, with no `bin/`
subdirectory. Keg-only Homebrew Ruby supplies `/opt/homebrew/opt/ruby/bin/ruby`.
Homebrew LLVM supplies lint tooling, not an implicit compiler override.
Set a different compatible `CXX` before first configure; CMake caches it.
For reproducible vcpkg tooling, check out the tested full `VCPKG_REF` commit
before bootstrapping; package registry baselines pin resolution separately.
Obsidian itself is optional: the server reads Markdown directly. Build/test
commands do not start services or touch your live vault.

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
