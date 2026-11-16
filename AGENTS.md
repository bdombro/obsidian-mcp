# AGENTS.md (`obsidian-mcp`)

Written for: AI coding agents working in the `obsidian-mcp` C++26 plugin repository.

`obsidian-mcp` is a native C++26 MCP `2026-07-28` plugin and CLI built on `slimr-mcp` that reads, searches, lists, writes, appends, and renames Markdown notes in the configured Obsidian vault (`$HOME/Documents/Notes` by default). It runs as a standalone binary (`build/bin/obsidian-mcp`) supporting Streamable HTTP (`http://127.0.0.1:10001/mcp`), stdio (`mcp`), and narrow JSON CLI invocation.

<!-- cpp-templates:begin -->
## Tooling alignment

Align this repository via the `cpp-templates` skill (`just check <template> .`). Keep repository-specific rules outside `<!-- cpp-templates:begin -->` and `<!-- cpp-templates:end -->`. Do not hand-edit verbatim template files or template recipes (repositories may add extra recipes or variables).

## Edit-don't-rewrite

- Make targeted edits and move files with `git mv`; never regenerate files from scratch or retype them from memory.
- Template files arrive through `just new` or `just sync`.
- Never delete, skip, or loosen tests to pass; if a test must change, explain why in the commit message.

## C++ Memory Safety

- Immediately wrap owned C handles, allocations, descriptors, and temporary resources in RAII. Make owning wrappers noncopyable unless copying deliberately creates independent ownership; moves must leave the source safely destructible.
- Borrow views, pointers, and references only for the current call by default. Store owning values unless lifetime/invalidation guarantees are explicit. Deferred callbacks require explicit ownership-safe captures; no escaping reference captures without a demonstrated lifetime guarantee.
- Initialize scalar/pointer members at declaration and local values before use. Compiler zero-initialization is defense in depth, not permission for uninitialized reads.
- Check range/overflow before narrowing lengths/counts at C API boundaries; casts alone are not validation. Report failures through existing error mechanisms.
- Never let C++ exceptions escape C callbacks. Translate them into the C API's error/abort mechanism and propagate failures at the C++ boundary; do not silently swallow them.
- Preserve libc++ hardening, zero auto-initialization, ASan/UBSan with fatal reports, and required lint/tests. These checks do not prove memory safety.

## C++ style

- Favor modern C++26 and the standard library over legacy patterns; write for readability; comment what is not obvious and why.
- Prefer values over pointers, `const` by default, and no raw `new`/`delete`.
- Use `std::expected` for expected failures; use exceptions only at process and callback boundaries.
- Structure code with one folder per feature under `src/`, mirrored under `tests/`. Keep one test TU per executable (`tests/test_main.cpp`).
- Name what you use (`using slimr::Json;` or `slimr::Json`, never `using namespace`). Use PascalCase for types and camelCase for functions and variables (review guidance, not lint).
- Put file-local helpers in an unnamed namespace in `.cpp` files, or in `detail::` in headers.
- Follow KISS: inline single-use helpers, avoid speculative abstractions, keep diffs lean.
- Apply style rules to new and touched code; ports do not rewrite working code just for style.
- `.clang-tidy` enforces bug-finding checks (`clang-analyzer-core.*`, `clang-analyzer-cplusplus.*`, and bug-risk `bugprone-*` checks; pending legacy findings are temporarily disabled with per-check counts in `.clang-tidy`); fix findings or suppress inline with a reason.
- `INTERFACE_SOURCES` caveat: linking a header-only library that ships an implementation TU (such as `slimr-yaml`) into both a library and an executable that links that library compiles the TU twice and causes duplicate symbols.

## Testing

Iterate with `just test "<case>"` (Debug build with ASan + UBSan, `BUGSINK_DSN=""`), then finish with a full `just test`. `just build` compiles Release only and runs no tests.

## Recipes

- Core: `build`, `clean`, `configure`, `deps`, `doctor`, `fmt`, `lint`, `release`, `test [filter]`, `upgrade-deps`
- `homebrew-*`: `install`, `uninstall`
- `homebrew-service` and `homebrew-mcp`: `daemon-logs`, `daemon-restart`, `daemon-start`, `daemon-status`, `daemon-stop`
- `homebrew-mcp`: `test-mcp`
- Dropped tooling (do not re-add): foreground runner, local tap/overlay symlinking, GitHub-branch/local-publish/dry-run helpers, standalone formula check, and JS formula helpers

## Changelog

Add one line under `## Unreleased` in `CHANGELOG.md` for every change (use `Internal: ...` for internal changes).

## Paths, env vars, and README

- `<name>` in Homebrew paths is always the formula name, never a short XDG name: app data lives in `$(brew --prefix)/var/<name>`, config in `$(brew --prefix)/etc/<name>/`, and service secrets in `$(brew --prefix)/etc/<name>/env` (`chmod 600`, never in the public formula).
- Secrets are env vars only.
- Every `README.md` is end-user first: what it does, install, usage, env var table (name, required or optional, purpose), then links into `docs/` (`docs/development.md`, `docs/architecture.md`) for contributor workflow and architecture.

## Remote tests

Never run `test.yml` manually before `just release` (because `release` runs `test.yml` first). Run `test.yml` manually only when asked or to reproduce a Linux-only failure.
<!-- cpp-templates:end -->


## Documentation Upkeep

When a change conflicts with `README.md`, update the affected README and its
linked user/developer documentation in the same change. Keep examples,
configuration, install/build commands and compatibility claims consistent
with implemented behavior; do not claim unverified release or client support.

## Commands

| Task | Command |
| --- | --- |
| Run unit, temp-vault, HTTP & concurrency tests (ASan + UBSan) | `just test` |
| Build and test release binary (`build/bin/obsidian-mcp`) | `just build` |
| Analyze app and test translation units | `just lint` |
| Verify isolated MCP `2026-07-28` stdio & ephemeral HTTP discovery | `just test-mcp` |
| Clean build artifacts | `just clean` |

## Module Boundaries

| Path | Owns | Must Not |
| --- | --- | --- |
| `src/main.cpp` | Thin entrypoint enabling HTTP on `127.0.0.1:10001` and calling `run(argc, argv)` | Business logic or tool handlers |
| `src/app.hpp` | Vault path containment (`rel_normalize`, `vault_path`, `vault_path_mutable`), `vault_mutex()` concurrency guard, frontmatter/inline tag parsing, note resolution, search ranking, backlink-rewriting rename, and `make_obsidian_app()` | Assume a fixed CWD or shell access |
| `docs/conventions.md` | Generic embedded fallback for `obsidian://conventions`; private overrides use `OBSIDIAN_CONVENTIONS_FILE` or XDG configuration | Personal conventions or tool usage docs (keep tool docs in `README.md` / tool descriptions) |
| `Formula/obsidian-mcp.rb` | Public Homebrew formula and service (`10001`) | Embedded secrets or private download strategies |
| `scripts/test-mcp.ts` | Native July `2026-07-28` stdio and ephemeral HTTP `server/discover` + `tools/list` verification | Touch live vault or run mutating tool calls |
| `.cursor-plugin/` & `.claude-plugin/` | Cursor and Claude Code HTTP plugin manifests (`http://127.0.0.1:10001/mcp`) | Stdio/Node wrappers or secrets |
| `tests/test_main.cpp` | Temp-vault, HTTP, CLI, and concurrency tests | Ever touch the live vault (`~/Documents/Notes`) |

## Safety & App Conventions

1. **Never touch the live vault in tests:** Every test must create an isolated temporary directory and point `OBSIDIAN_VAULT` at that temp directory.
2. **Strict path containment:**
   - Every read path must pass through `rel_normalize` / `vault_path`: vault-relative only, rejecting `..`, absolute paths, hidden directories (`.obsidian`, `.trash`, `.git`), and symlinks resolving outside the vault.
   - Every write/rename path must pass through `vault_path_mutable`, which additionally refuses symlinks as the target or any intermediate path component.
3. **Thread safety under HTTP:** Read-only tools (`read`, `list`, `search`, `status`) opt into `threadSafe = true` and acquire `std::shared_lock` on `vault_mutex()`. Mutating operations (`write_note`, `append_note`, `rename_note`) acquire `std::unique_lock` on `vault_mutex()` and use atomic temp-file writes (`vault_write`).
4. **Remote-ready:** Tools take and return note content and vault-relative paths, never host absolute paths.
