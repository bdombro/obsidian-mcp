# AGENTS.md (`obsidian-mcp`)

Written for: AI coding agents working in the `obsidian-mcp` C++26 plugin repository.

`obsidian-mcp` is a native C++26 MCP `2026-07-28` plugin and CLI built on `slimr-cpp` that reads, searches, lists, writes, appends, and renames Markdown notes in the configured Obsidian vault (`$HOME/Documents/Notes` by default). It runs as a standalone binary (`build/bin/obsidian-mcp`) supporting Streamable HTTP (`http://127.0.0.1:8081/mcp`), stdio (`mcp`), and narrow JSON CLI invocation.


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
| `src/main.cpp` | Thin entrypoint enabling HTTP on `127.0.0.1:8081` and calling `run(argc, argv)` | Business logic or tool handlers |
| `src/app.hpp` | Vault path containment (`rel_normalize`, `vault_path`, `vault_path_mutable`), `vault_mutex()` concurrency guard, frontmatter/inline tag parsing, note resolution, search ranking, backlink-rewriting rename, and `make_obsidian_app()` | Assume a fixed CWD or shell access |
| `docs/conventions.md` | Generic embedded fallback for `obsidian://conventions`; private overrides use `OBSIDIAN_CONVENTIONS_FILE` or XDG configuration | Personal conventions or tool usage docs (keep tool docs in `README.md` / tool descriptions) |
| `Formula/obsidian-mcp.rb` | Public Homebrew formula and `launchd` service (`127.0.0.1:8081`) | Embedded secrets or private download strategies |
| `scripts/dev-formula.sh` | Safe local dev formula staging and backup restore | Leave release formula modified after failure |
| `scripts/test-mcp.mjs` | Native July `2026-07-28` stdio and ephemeral HTTP `server/discover` + `tools/list` verification | Touch live vault or run mutating tool calls |
| `.cursor-plugin/` & `.claude-plugin/` | Cursor and Claude Code HTTP plugin manifests (`http://127.0.0.1:8081/mcp`) | Stdio/Node wrappers or secrets |
| `tests/test_main.cpp` | Temp-vault, HTTP, CLI, and concurrency tests | Ever touch the live vault (`~/Documents/Notes`) |

## Safety & App Conventions

1. **Never touch the live vault in tests:** Every test must create an isolated temporary directory and point `OBSIDIAN_VAULT` at that temp directory.
2. **Strict path containment:**
   - Every read path must pass through `rel_normalize` / `vault_path`: vault-relative only, rejecting `..`, absolute paths, hidden directories (`.obsidian`, `.trash`, `.git`), and symlinks resolving outside the vault.
   - Every write/rename path must pass through `vault_path_mutable`, which additionally refuses symlinks as the target or any intermediate path component.
3. **Thread safety under HTTP:** Read-only tools (`read`, `list`, `search`, `status`) opt into `threadSafe = true` and acquire `std::shared_lock` on `vault_mutex()`. Mutating operations (`write_note`, `append_note`, `rename_note`) acquire `std::unique_lock` on `vault_mutex()` and use atomic temp-file writes (`vault_write`).
4. **Remote-ready:** Tools take and return note content and vault-relative paths, never host absolute paths.

## C++ Memory Safety

- Immediately wrap owned C handles, allocations, descriptors, and temporary resources in RAII. Make owning wrappers noncopyable unless copying creates independent ownership; moves must leave the source safely destructible.
- Borrow views, pointers, and references only for the current call by default. Store owning values unless lifetime/invalidation guarantees are explicit. Deferred callbacks require explicit ownership-safe captures; no escaping reference captures without a demonstrated lifetime guarantee.
- Initialize scalar/pointer members at declaration and local values before use. Compiler auto-initialization is defense in depth, not permission for uninitialized reads.
- Check range/overflow before narrowing lengths/counts at C API boundaries; casts alone are not validation. Report invalid inputs using existing error mechanisms.
- Never let C++ exceptions escape C callbacks. Translate them into the C API's error/abort mechanism and propagate failures at the C++ boundary; do not silently swallow them.
- Preserve hardening, zero auto-initialization, ASan/UBSan with fatal reports, and required lint/tests. These checks do not prove memory safety.
