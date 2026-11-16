# Changelog

## Unreleased

- Internal: switch macOS builds to llvm@23 with static libc++ and symbol hiding.

## v2.1.0 - 2026-11-16

- Tag Bugsink error reports with `obsidian-mcp@<version>` from `VERSION`
- HTTP server binds to `0.0.0.0` by default (use `--host 127.0.0.1` for loopback only)
- Log files passed via `--log-file` rotate at 10 MiB, keeping one `.1` backup
- HTTP server no longer rejects non-loopback hosts, port 0, or foreign Host/Origin headers (slimr-mcp upgrade)
- Internal: align build, CI, Homebrew formula, and documentation with cpp-templates
- Internal: resolve slimr registries from the `slimr-down-now` org
