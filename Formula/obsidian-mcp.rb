class ObsidianMcp < Formula
  desc "Read, search, write, append, and rename notes in an Obsidian vault over MCP"
  homepage "https://github.com/bdombro/obsidian-mcp"
  version "2.0.0"

  on_macos do
    url "https://github.com/bdombro/obsidian-mcp/releases/download/v2.0.0/obsidian-mcp-arm64-macos.tar.gz"
    sha256 "0000000000000000000000000000000000000000000000000000000000000000"
  end

  on_linux do
    url "https://github.com/bdombro/obsidian-mcp/releases/download/v2.0.0/obsidian-mcp-x64-linux.tar.gz"
    sha256 "0000000000000000000000000000000000000000000000000000000000000000"
  end

  def install
    bin.install "obsidian-mcp"
    chmod 0755, bin/"obsidian-mcp"
    pkgshare.install "docs" if File.directory?("docs")
  end

  def post_install
    (var/"obsidian-mcp").mkpath
    chmod 0700, var/"obsidian-mcp"
    (var/"log").mkpath
  end

  service do
    run ["/bin/sh", "-c", "[ -f #{etc}/obsidian-mcp/env ] && set -a && . #{etc}/obsidian-mcp/env; exec #{opt_bin}/obsidian-mcp http --log-file #{var}/log/obsidian-mcp.log"]
    keep_alive true
    working_dir var/"obsidian-mcp"
    log_path var/"log/obsidian-mcp.log"
    error_log_path var/"log/obsidian-mcp.log"
    environment_variables PATH: "#{HOMEBREW_PREFIX}/bin:/usr/bin:/bin"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/obsidian-mcp --version")
    discover = '{"jsonrpc":"2.0","id":1,"method":"server/discover","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientCapabilities":{}}}}'
    out = pipe_output("#{bin}/obsidian-mcp mcp --skip-checks", "#{discover}\n")
    assert_match "obsidian-mcp", out
  end
end
