# Changelog

## Unreleased

- HTTP server binds to `0.0.0.0` by default (use `--host 127.0.0.1` for loopback only)
- Log files passed via `--log-file` rotate at 10 MiB, keeping one `.1` backup
- HTTP server no longer rejects non-loopback hosts, port 0, or foreign Host/Origin headers (slimr-mcp upgrade)
- Internal: align build, CI, Homebrew formula, and documentation with cpp-templates
