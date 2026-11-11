# AGENTS.md (`obsidian-mcp`)

Written for: AI coding agents working in the `obsidian-mcp` C++26 plugin repository.

`obsidian-mcp` is a native C++26 MCP plugin and CLI built on `slimr-cpp` that reads, searches, lists, writes, appends, and renames Markdown notes in the configured Obsidian vault (`$HOME/Documents/Notes` by default). It compiles both as a standalone binary (`build/bin/obsidian-mcp`) and in-process inside the workspace `build/bin/assistant-mcp` gateway (`notes_*` tools).

## Commands

| Task | Command |
| --- | --- |
| Run unit & temp-vault tests (ASan + UBSan) | `just test` |
| Build and test release binary (`build/bin/obsidian-mcp`) | `just build` |
| Analyze app and test translation units | `just lint` |
| Verify isolated MCP initialization and tool discovery | `just test-mcp` |
| Clean build artifacts | `just clean` |

## Module Boundaries

| Path | Owns | Must Not |
| --- | --- | --- |
| `src/main.cpp` | Thin entrypoint calling `obsidian::make_obsidian_app().run(argc, argv)` | Business logic or tool handlers |
| `src/app.hpp` | Vault path containment (`relNormalize`, `vaultPath`, `vaultPathMutable`), frontmatter/inline tag parsing, note resolution, search ranking, backlink-rewriting rename, and `make_obsidian_app()` | Assume a fixed CWD or shell access |
| `docs/conventions.md` | Generic embedded fallback for `obsidian://conventions`; private overrides use `OBSIDIAN_CONVENTIONS_FILE` or XDG configuration | Personal conventions or tool usage docs (keep tool docs in `README.md` / tool descriptions) |
| `tests/test_main.cpp` | Temp-vault (`TempDirGuard`) unit and MCP tests | Ever touch the live vault (`~/Documents/Notes`) |

## Safety & App Conventions

1. **Never touch the live vault in tests:** Every test must create an isolated temporary directory (`slimr::testing::TempDirGuard`) and point `OBSIDIAN_VAULT` (or the `VaultConfig` parameter) at that temp directory.
2. **Strict path containment:**
   - Every read path must pass through `relNormalize` / `vaultPath`: vault-relative only, rejecting `..`, absolute paths, hidden directories (`.obsidian`, `.trash`, `.git`), and symlinks resolving outside the vault.
   - Every write/rename path must pass through `vaultPathMutable`, which additionally refuses symlinks as the target or any intermediate path component.
3. **In-process C++ API:** `personal-log` (`src/mine.hpp`) can call `obsidian` functions in-process during mining (`Med` and `Medical Expenses` note updates). Keep functions in `namespace obsidian` cleanly callable without global mutable state.
4. **Remote-ready:** Tools take and return note content and vault-relative paths, never host absolute paths.

## C++ Memory Safety

- Immediately wrap owned C handles, allocations, descriptors, and temporary resources in RAII. Make owning wrappers noncopyable unless copying creates independent ownership; moves must leave the source safely destructible.
- Borrow views, pointers, and references only for the current call by default. Store owning values unless lifetime/invalidation guarantees are explicit. Deferred callbacks require explicit ownership-safe captures; no escaping reference captures without a demonstrated lifetime guarantee.
- Initialize scalar/pointer members at declaration and local values before use. Compiler auto-initialization is defense in depth, not permission for uninitialized reads.
- Check range/overflow before narrowing lengths/counts at C API boundaries; casts alone are not validation. Report invalid inputs using existing error mechanisms.
- Never let C++ exceptions escape C callbacks. Translate them into the C API's error/abort mechanism and propagate failures at the C++ boundary; do not silently swallow them.
- Preserve hardening, zero auto-initialization, ASan/UBSan with fatal reports, and required lint/tests. These checks do not prove memory safety.
