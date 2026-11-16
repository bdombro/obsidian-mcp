class ObsidianMcp < Formula
  desc "Read, search, write, append, and rename notes in an Obsidian vault over MCP"
  homepage "https://github.com/bdombro/obsidian-mcp"
  version "2.1.0"

  on_macos do
    url "https://github.com/bdombro/obsidian-mcp/releases/download/v2.1.0/obsidian-mcp-arm64-macos.tar.gz"
    sha256 "269b1777086a36a8ed2c28b7ba55ce06b597f4cbb6ce872966f15ae325df5c18"
  end

  on_linux do
    url "https://github.com/bdombro/obsidian-mcp/releases/download/v2.1.0/obsidian-mcp-x64-linux.tar.gz"
    sha256 "8fdd85b1697a1ae338d02e9265a0d4e4e9ce3852280320145386e6f3d95f07c7"
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
    require "net/http"
    assert_match version.to_s, shell_output("#{bin}/obsidian-mcp --version")
    r, w = IO.pipe
    pid = spawn("#{bin}/obsidian-mcp", "--skip-checks", "http", "--host", "127.0.0.1", "--port", "0", err: w)
    w.close
    begin
      port = nil
      r.each_line { |line| break if (port = line[%r{listening on http://[\d.]+:(\d+)/mcp}, 1]) }
      assert port, "server did not report a listening port"
      discover = '{"jsonrpc":"2.0","id":1,"method":"server/discover","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientCapabilities":{}}}}'
      res = Net::HTTP.post(URI("http://127.0.0.1:#{port}/mcp"), discover,
                           "Content-Type"         => "application/json",
                           "Accept"               => "application/json, text/event-stream",
                           "MCP-Protocol-Version" => "2026-07-28",
                           "Mcp-Method"           => "server/discover")
      assert_match "obsidian-mcp", res.body
    ensure
      Process.kill("TERM", pid)
      Process.wait(pid)
    end
  end
end
