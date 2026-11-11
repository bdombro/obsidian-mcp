# obsidian-mcp

```
     .--------.
    /  .---.   \
   |  /     \   |
   | |   O   |   |
    \  '---'   /
     '--------'
```

Search, read, and safely edit notes in your Obsidian vault through MCP. Find notes by title, alias, tag, or content; rename a note and update its backlinks across the vault.

Unlike tools tied to the Obsidian UI, this MCP works directly with Markdown files, so Obsidian does not need to be running. It keeps your vault structure and conventions intact without a separate plugin or background indexer.

## Features

- Search by keywords, aliases, tags, folders, and frontmatter; read and list notes.
- Create, append to, or rename notes, with vault-wide link updates on rename.
- Enforces vault path boundaries and refuses paths that escape the vault.
- Small footprint: on-demand local search with no resident indexer or Node/V8 runtime.

## `.mcp.json` Configuration

In the `assistant2` workspace, `obsidian-mcp` is mounted into the unified `build/bin/assistant-mcp` gateway under the `notes_` prefix (`notes_read`, `notes_search`, `notes_list`, `notes_write`, `notes_append`, `notes_rename`, `notes_status`).

To run `obsidian-mcp` as a standalone MCP server in `.mcp.json`:

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

## App Architecture

The standalone MCP and JSON CLI are implemented in C++26 on `slimr-cpp`.

```text
plugins/obsidian-mcp/
├── vcpkg.json                # Independently resolved dependencies
├── CMakeLists.txt            # App/test targets and transitive dependency linking
├── justfile                  # Thin CMake/Ninja orchestration
├── README.md                 # User & operator documentation
├── AGENTS.md                 # Maintainer & AI agent conventions
├── docs/
│   └── conventions.md        # Note conventions served as `obsidian://conventions`
├── src/
│   ├── app.hpp               # Vault path isolation, frontmatter/tag parser, search/rename engine, and `make_obsidian_app()`
│   └── main.cpp              # Standalone CLI & stdio MCP entrypoint (`make_obsidian_app().run(argc, argv)`)
├── tests/
│   └── test_main.cpp         # 32 unit & temp-vault test cases (144 assertions)
└── build/bin/
    └── obsidian-mcp          # Standalone release binary
```

### Libraries Used

- **`<mcp.hpp>` & `<reflect.hpp>`:** Typed tool definitions, compile-time JSON Schema generation, narrow JSON CLI, stdio NDJSON MCP server, and `obsidian://conventions` resource registration.
- **`<glaze/yaml.hpp>`:** YAML frontmatter parsing (`aliases`, `tags`, key-value filters).
- **`<json.hpp>` (`glaze`):** RFC-8259 JSON serialization.
- **`<bugsink.hpp>` (`-lcurl`):** Optional exception reporting configured with `BUGSINK_DSN`.
- **`<test.hpp>` (`doctest`):** Temp-vault fixture testing with `-fsanitize=address,undefined`.
- **C++26 Standard Library:** `<filesystem>` (`weakly_canonical` path containment), `<regex>` (wikilink/embed/markdown link rewriting).

## Architecture & Vault Isolation

- **Default vault path:** `$HOME/Documents/Notes` (override with `OBSIDIAN_VAULT`). Missing `HOME` is an error only when a vault operation needs the default.
- **Path containment:** Every note path is canonicalized relative to the vault root (`std::filesystem::weakly_canonical`). Any path resolving outside the vault (`..`, absolute paths, symlinks pointing outside the vault) or into hidden directories (`.obsidian`, `.trash`, `.git`) is strictly refused.
- **No background index:** Searches scan the vault at query time, parsing YAML frontmatter (`aliases`, `tags`, arbitrary `key: value` metadata) and inline `#tags` (including nested `#parent/child` tags) on the fly.
- **Conventions resource:** Serves `obsidian://conventions`, also readable through `mcp_resource_read`. On resource reads only, it loads `OBSIDIAN_CONVENTIONS_FILE`, otherwise `$XDG_CONFIG_HOME/obsidian/conventions.md` or `$HOME/.config/obsidian/conventions.md`. A missing default private file uses the embedded public guide (`docs/conventions.md`); a configured missing or unreadable file fails visibly. Discovery never reads the vault or creates configuration files. Keep private conventions outside the repository with owner-only permissions.

## Tools & CLI Commands

Every tool is exposed both over MCP (`obsidian-mcp mcp` or via the unified `assistant-mcp` gateway under the `notes_` prefix) and on the CLI as `obsidian-mcp <tool> ['{"arg":"val"}']`.

| Tool | Gateway Name | Mutating | Description |
| --- | --- | --- | --- |
| `read` | `notes_read` | No | Read one note by vault-relative path, filename, title, or frontmatter alias (resolved in that order, matching Obsidian's link resolution). Returns raw content, body, and parsed YAML frontmatter. |
| `search` | `notes_search` | No | Keyword search (case-insensitive ANDed terms + `"quoted phrases"`) across note titles, aliases, and bodies, with optional `folder`, `tag` (frontmatter and inline `#tag`, supporting nested tags), and `frontmatter` (`key=value`) filters. Returns ranked matches with score and contextual snippet lines. |
| `list` | `notes_list` | No | List notes in a folder (optionally `recursive: true`) with titles, frontmatter keys, and immediate subfolders. |
| `write` | `notes_write` | Yes | Create a new note (automatically creating parent folders inside the vault). Replacing an existing note requires `overwrite: true`. |
| `append` | `notes_append` | Yes | Append text to the end of a note, or at the end of a specific Markdown heading section (`heading: "## Section"`). |
| `rename` | `notes_rename` | Yes | Rename or move a note and automatically rewrite every `[[wikilink]]`, `![[embed]]`, `[[Note#Heading]]`, `[[Note|Alias]]`, and `[markdown](link.md)` reference across the entire vault. Supports `addAlias: true` and `dryRun: true`. |
| `status` | `notes_status` | No | Returns plugin version and active vault path. |

### CLI Examples

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
```

## Environment Variables

| Variable | Default | Purpose |
| --- | --- | --- |
| `OBSIDIAN_VAULT` | `$HOME/Documents/Notes` | Root directory of the Obsidian Markdown vault |
| `OBSIDIAN_CONVENTIONS_FILE` | XDG/private file, then embedded guide | Optional private conventions override, loaded only on resource reads |
| `BUGSINK_DSN` | Unset (disabled) | Sentry-compatible error reporting DSN (`""` disables reporting) |

## Building & Testing

Use Clang with C++26 and libc++ FAST hardening, CMake 3.31+, Ninja, [just](https://just.systems), a bootstrapped [vcpkg](https://vcpkg.io), Node for Inspector, and a compatible clang-tidy (LLVM 23 locally). Set `VCPKG_ROOT` and put vcpkg and the tools on `PATH`. Linux additionally needs libc++ and libc++abi; select libc++ with `CXXFLAGS=-stdlib=libc++`.

Dependencies are declared in `vcpkg.json`. During local development, `(cd /absolute/path/to/slimr-cpp && just publish --local --all)` publishes ports into vcpkg's local overlay; export `VCPKG_OVERLAY_PORTS="$VCPKG_ROOT/local-ports"` in your shell. `just deps` installs into `build/deps/` for the detected native triplet. `configure` always runs it, with CMake manifest installation disabled. CMake links installed `slimr::mcp` and `slimr::test` targets, never source-header overrides.

```bash
just test      # Debug ASan/UBSan tests against isolated vault fixtures only
just build     # Optimized IPO Release app and tests; build/bin/obsidian-mcp
just lint      # App and test translation units using the Debug compilation database
just test-mcp  # Pinned Inspector 2.14.0: initialize and tools/list only
just run status
just clean     # Remove build/ only; leaves legacy dist/ and user data untouched
```

Debug and Release graphs live in `build/cmake/Debug/` and `build/cmake/Release/`; only Release apps are emitted to `build/bin/`. Set `CXX` before the first configure to choose a compiler; CMake retains its absolute path even when lint uses a different `PATH`. Both configurations enable libc++ hardening and zero automatic-variable initialization; Debug uses fatal UBSan reports. Inspector isolates HOME/XDG directories under `build/`, clears credentials, disables error reporting and external CLI access, and cleans up afterward; it never invokes a tool or opens the live vault.

CI runs the same commands on native Linux/macOS. Configure `VCPKG_REF` to the tested full vcpkg commit SHA; slimr packages resolve from the public registry pinned in `vcpkg-configuration.json`, without a dependency token. Local macOS validation is not Linux execution evidence.
