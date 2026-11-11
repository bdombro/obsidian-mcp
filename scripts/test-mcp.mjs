import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { mkdtempSync, readdirSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const [executable, expected] = process.argv.slice(2);
assert(executable && expected, "Usage: test-mcp.mjs <executable> <comma-separated-tools>");
const binary = path.resolve(executable);
const temporary = mkdtempSync(path.join(tmpdir(), "mcp-july-conformance-"));

const JULY_VERSION = "2026-07-28";

const isolatedEnv = {
  HOME: temporary,
  PATH: "/usr/bin:/bin",
  XDG_CONFIG_HOME: path.join(temporary, "config"),
  XDG_DATA_HOME: path.join(temporary, "data"),
  XDG_STATE_HOME: path.join(temporary, "state"),
  BUGSINK_DSN: "",
};

function rpcRequest(id, method, params = {}) {
  return {
    jsonrpc: "2.0",
    id,
    method,
    params: {
      ...params,
      _meta: {
        "io.modelcontextprotocol/protocolVersion": JULY_VERSION,
        "io.modelcontextprotocol/clientCapabilities": {},
      },
    },
  };
}

function assertDiscoverAndTools(discover, listResp, expectedTools) {
  assert(discover?.result, "Missing server/discover result");
  assert.equal(discover.result.resultType, "complete", "Missing resultType=complete on discover");
  assert.deepEqual(discover.result.supportedVersions, [JULY_VERSION], "Server must support only 2026-07-28");
  assert(discover.result._meta?.["io.modelcontextprotocol/serverInfo"]?.name, "Missing serverInfo.name");
  assert(typeof discover.result.ttlMs === "number" && discover.result.ttlMs >= 0, "Missing CacheableResult.ttlMs");

  assert(Array.isArray(listResp?.result?.tools), "Missing tools array");
  assert.equal(listResp.result.resultType, "complete", "Missing resultType=complete on tools/list");
  assert(typeof listResp.result.ttlMs === "number" && listResp.result.ttlMs >= 0, "Missing CacheableResult.ttlMs on tools/list");
  const tools = new Map(listResp.result.tools.map(tool => [tool.name, tool]));
  for (const name of expectedTools) {
    assert(tools.has(name), `Missing tool: ${name}`);
    assert.equal(tools.get(name).inputSchema?.type, "object", `Invalid schema: ${name}`);
  }
  return tools;
}

async function runStdioSession(messages) {
  const child = spawn(binary, ["mcp"], {
    cwd: temporary,
    env: isolatedEnv,
    stdio: ["pipe", "pipe", "pipe"],
  });

  let stdout = "";
  let stderr = "";
  child.stdout.setEncoding("utf8").on("data", chunk => { stdout += chunk; });
  child.stderr.setEncoding("utf8").on("data", chunk => { stderr += chunk; });

  const timer = setTimeout(() => {
    child.kill("SIGKILL");
  }, 15000);

  try {
    for (const msg of messages) {
      child.stdin.write(`${JSON.stringify(msg)}\n`);
    }
    child.stdin.end();

    const code = await new Promise((resolve, reject) => {
      child.once("error", reject);
      child.once("close", c => resolve(c));
    });
    assert.equal(code, 0, `Stdio server exited ${code}; stderr: ${stderr}`);
    const responses = new Map();
    for (const line of stdout.split("\n")) {
      const trimmed = line.trim();
      if (!trimmed) continue;
      const obj = JSON.parse(trimmed);
      if (obj.id !== undefined) responses.set(obj.id, obj);
    }
    return responses;
  } finally {
    clearTimeout(timer);
  }
}

async function runHttpSession(expectedTools) {
  const child = spawn(binary, ["http", "--host", "127.0.0.1", "--port", "0", "--allow-ephemeral-port"], {
    cwd: temporary,
    env: isolatedEnv,
    stdio: ["ignore", "pipe", "pipe"],
  });

  let stderr = "";
  const endpointUrl = await new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      child.kill("SIGKILL");
      reject(new Error(`Timed out waiting for HTTP server startup; stderr: ${stderr}`));
    }, 15000);

    child.once("error", err => {
      clearTimeout(timer);
      reject(err);
    });
    child.once("close", code => {
      clearTimeout(timer);
      reject(new Error(`HTTP server exited early (${code}); stderr: ${stderr}`));
    });
    child.stderr.setEncoding("utf8").on("data", chunk => {
      stderr += chunk;
      const m = /listening on (http:\/\/127\.0\.0\.1:\d+\/mcp)/.exec(stderr);
      if (m) {
        clearTimeout(timer);
        resolve(m[1]);
      }
    });
  });

  try {
    async function postRpc(method, body) {
      const res = await fetch(endpointUrl, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          "Accept": "application/json, text/event-stream",
          "MCP-Protocol-Version": JULY_VERSION,
          "Mcp-Method": method,
        },
        body: JSON.stringify(body),
      });
      const text = await res.text();
      return { status: res.status, json: JSON.parse(text) };
    }

    const discoverRes = await postRpc("server/discover", rpcRequest(1, "server/discover"));
    assert.equal(discoverRes.status, 200, "HTTP server/discover must return 200");

    const listRes = await postRpc("tools/list", rpcRequest(2, "tools/list"));
    assert.equal(listRes.status, 200, "HTTP tools/list must return 200");

    const tools = assertDiscoverAndTools(discoverRes.json, listRes.json, expectedTools);

    const legacyRes = await postRpc("initialize", {
      jsonrpc: "2.0",
      id: 3,
      method: "initialize",
      params: { protocolVersion: "2025-11-25" },
    });
    assert.equal(legacyRes.status, 404, "HTTP legacy initialize must return 404");
    assert.equal(legacyRes.json?.error?.code, -32601, "Legacy initialize must fail with -32601");

    return { discover: discoverRes.json, tools };
  } finally {
    const exitPromise = new Promise(resolve => child.once("close", resolve));
    child.kill("SIGTERM");
    const killTimer = setTimeout(() => {
      child.kill("SIGKILL");
    }, 5000);
    await exitPromise;
    clearTimeout(killTimer);
  }
}

try {
  const expectedTools = expected.split(",");
  const responses = await runStdioSession([
    rpcRequest(1, "server/discover"),
    rpcRequest(2, "tools/list"),
    {
      jsonrpc: "2.0",
      id: 3,
      method: "initialize",
      params: { protocolVersion: "2025-11-25" },
    },
  ]);

  const discover = responses.get(1);
  const listResp = responses.get(2);
  const stdioTools = assertDiscoverAndTools(discover, listResp, expectedTools);

  const legacyInit = responses.get(3);
  assert.equal(legacyInit?.error?.code, -32601, "Legacy initialize must fail with -32601 Method not found");

  const httpRes = await runHttpSession(expectedTools);
  assert.equal(
    httpRes.discover.result._meta["io.modelcontextprotocol/serverInfo"].name,
    discover.result._meta["io.modelcontextprotocol/serverInfo"].name,
    "HTTP and stdio serverInfo.name must match"
  );
  assert.equal(httpRes.tools.size, stdioTools.size, "HTTP and stdio tool counts must match");

  assert.deepEqual(readdirSync(temporary), [], "Server created files during discovery");
  console.log(
    `MCP 2026-07-28 stdio+http passed: ${discover.result._meta["io.modelcontextprotocol/serverInfo"].name}, ${stdioTools.size} tools`
  );
} finally {
  rmSync(temporary, { recursive: true, force: true });
}
