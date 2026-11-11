class ObsidianMcp < Formula
  desc "Read, search, write, append, and rename notes in an Obsidian vault over MCP"
  homepage "https://github.com/bdombro/obsidian-mcp"
  url "https://github.com/bdombro/obsidian-mcp/releases/download/v2.0.0/obsidian-mcp.zip"
  version "2.0.0"
  sha256 "0000000000000000000000000000000000000000000000000000000000000000"
  license "ISC"

  def install
    bin.install "obsidian-mcp"
    chmod 0755, bin/"obsidian-mcp"
  end

  service do
    run [opt_bin/"obsidian-mcp", "http", "--host", "127.0.0.1", "--port", "8081"]
    keep_alive true
    log_path var/"log/obsidian-mcp.log"
    error_log_path var/"log/obsidian-mcp.log"
  end

  test do
    output = shell_output("#{bin}/obsidian-mcp status")
    assert_match version.to_s, output
  end
end
