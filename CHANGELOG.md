# Changelog

## Unreleased

- Upgrade `slimr-serve` to `0.3.0` and `slimr-mcp` to `0.2.1`: serve OpenAPI 3.1.0 at `GET /openapi.json` and `--openapi`, and bind `127.0.0.1` by default (pass `--host 0.0.0.0` to expose)
- Docs: add "Develop against an unreleased lib" section to `docs/development.md`
- Internal: check `GET /openapi.json` in `scripts/test-mcp.ts`

## v2.2.0 - 2026-11-16

- Migrate to `slimr-serve` typed handlers (`outputSchema` / `structuredContent`) and drop stdio `mcp` mode.
- Re-enable direct CLI tool calls (`obsidian-mcp <tool> '{...}'`), now opt-in since `slimr-mcp` v0.1.4
- Internal: upgrade `slimr-mcp` (`cpp-httplib`) and `slimr-bugsink`, dropping `libhv` and `slimr-curl` references.
- Internal: switch macOS builds to llvm@23 with static libc++ and symbol hiding.

## v2.1.0 - 2026-11-16

- Tag Bugsink error reports with `obsidian-mcp@<version>` from `VERSION`
- HTTP server binds to `0.0.0.0` by default (use `--host 127.0.0.1` for loopback only)
- Log files passed via `--log-file` rotate at 10 MiB, keeping one `.1` backup
- HTTP server no longer rejects non-loopback hosts, port 0, or foreign Host/Origin headers (slimr-mcp upgrade)
- Internal: align build, CI, Homebrew formula, and documentation with cpp-templates
- Internal: resolve slimr registries from the `slimr-down-now` org
