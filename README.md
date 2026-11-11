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

> **Client compatibility:** MCP **2026-07-28 only**; legacy clients cannot connect.
> The workspace has been exercised with OpenCode v2.2.6 using explicit July
> configuration. Cursor's July support is deferred; Claude Code July runtime
> acceptance is not yet verified. Shipping a plugin manifest is not proof of
> client compatibility.

## Try It

After setup, ask your agent:

> Find notes about the boiler service, then show me the maintenance checklist.

## Features

- Search by keywords, aliases, tags, folders, and frontmatter; read and list notes.
- Create, append to, or rename notes, with vault-wide link updates on rename.
- Enforces vault path boundaries and refuses paths that escape the vault.
- Concurrent HTTP reads (`read`, `list`, `search`, `status`) synchronized with exclusive vault writes via `std::shared_mutex`.
- Small footprint: on-demand local search with no resident indexer or Node/V8 runtime.

## Installation

- **Prebuilt release:** the checked-in [Homebrew formula](Formula/obsidian-mcp.rb)
  currently has a placeholder checksum. It is not a ready-to-install release.
  Check [releases](https://github.com/bdombro/obsidian-mcp/releases) for published
  binaries and a finalized formula before using a release installation command.
- **Build from source:** the current path is [Development](#development).
  Local slimr overlays are for development, not a runtime requirement.
- **Already installed:** follow the setup and service steps below. Installing a
  Cursor/Claude manifest only configures an endpoint; it does not install or
  start the native service.


## Configure Your Vault

The default vault is `$HOME/Documents/Notes`; Obsidian does not need to be
running. For another vault, set `OBSIDIAN_VAULT` in the server's environment.
For example, with an installed binary:

```bash
OBSIDIAN_VAULT="/absolute/path/to/vault" obsidian-mcp http
```

For a Homebrew service, configure its environment separately from your shell.
Private convention overrides are optional; the embedded public guide works
without them. Never put door/alarm codes or other secrets in public docs.


## Start and Manage the Service

For an already-installed Homebrew formula:

```bash
brew services start bdombro/obsidian-mcp/obsidian-mcp
brew services info obsidian-mcp
brew services restart bdombro/obsidian-mcp/obsidian-mcp
brew services stop bdombro/obsidian-mcp/obsidian-mcp
```

The endpoint is `http://127.0.0.1:8081/mcp`; logs are under
`/opt/homebrew/var/log/`. To run in the foreground instead, use
`obsidian-mcp http` (or `./build/bin/obsidian-mcp http` from a source checkout).
Do not run the foreground server and service on the same port.
Keep the default loopback bind; this is not a shared remote-host deployment.


## Connect Your Agent

After starting the service, add this server to OpenCode v2.2.6's
`opencode.jsonc` (merge it with any existing servers):

```json
{
  "mcp": {
    "servers": {
      "obsidian-mcp": {
        "type": "remote",
        "url": "http://127.0.0.1:8081/mcp",
        "protocol": "2026-07-28"
      }
    }
  }
}
```

Use either an explicit server entry or a plugin manifest, not both.
Alternative client schemas and stdio configuration are in the
[developer reference](docs/development.md). The compatibility notice above
applies to every transport.


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

## Architecture & Vault Isolation

- **Default vault path:** `$HOME/Documents/Notes` (override with `OBSIDIAN_VAULT`). Missing `HOME` is an error only when a vault operation needs the default.
- **Path containment:** Every note path is canonicalized relative to the vault root (`std::filesystem::canonical`). Any path resolving outside the vault (`..`, absolute paths, symlinks pointing outside the vault) or into hidden directories (`.obsidian`, `.trash`, `.git`) is strictly refused.
- **No background index:** Searches scan the vault at query time, parsing YAML frontmatter (`aliases`, `tags`, arbitrary `key: value` metadata) and inline `#tags` (including nested `#parent/child` tags) on the fly.
- **Conventions resource:** Serves `obsidian://conventions`, also readable through `mcp_resource_read`. On resource reads only, it loads `OBSIDIAN_CONVENTIONS_FILE`, otherwise `$XDG_CONFIG_HOME/obsidian/conventions.md` or `$HOME/.config/obsidian/conventions.md`. A missing default private file uses the embedded public guide (`docs/conventions.md`); a configured missing or unreadable file fails visibly. Discovery never reads the vault or creates configuration files. Keep private conventions outside the repository with owner-only permissions.

## Environment Variables

| Variable | Default | Purpose |
| --- | --- | --- |
| `OBSIDIAN_VAULT` | `$HOME/Documents/Notes` | Root directory of the Obsidian Markdown vault |
| `OBSIDIAN_CONVENTIONS_FILE` | XDG/private file, then embedded guide | Optional private conventions override, loaded only on resource reads |
| `BUGSINK_DSN` | Unset (disabled) | Sentry-compatible error reporting DSN (`""` disables reporting) |

## Troubleshooting

- **Client cannot connect:** confirm it supports July MCP, the service is
  running, and the client URL matches the configured port. OpenCode needs
  `"protocol": "2026-07-28"`; old `initialize` clients will fail.
- **Port occupied:** inspect the listener and stop only the service/process
  you own, or deliberately choose another port and update the client URL.
- **Works in a terminal, not as a service:** launchd does not inherit
  `~/.zshrc`. Configure the service environment separately or use the
  plugin's supported private configuration file; inspect
  `/opt/homebrew/var/log/` for errors.
- **Vault unavailable:** verify the configured vault path, ownership and service permissions. Tests never use the live vault.
- **Source build cannot find vcpkg or slimr ports:** load the development
  environment, bootstrap vcpkg, and register both slimr links and the pinned
  libhv overlay. Never repair this by editing installed headers.
- **Compiler/hardening check fails:** upgrade the Xcode toolchain; set
  `CXX` before first configure. CMake caches the compiler, so an intentional
  compiler change needs a fresh configuration tree.


## Development

This section is for building or changing the server, not configuring an
installed binary. [Architecture, transport reference and test details](docs/development.md)
are separate from the user setup flow.

<details>
<summary>macOS source-build setup (vcpkg and local slimr overlays)</summary>

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

</details>

After the source build succeeds, stage and install the local Homebrew formula:

```bash
just install-local
```

Installing a formula and starting its service are separate actions.
For a source-only foreground run, use
`./build/bin/obsidian-mcp http`.
