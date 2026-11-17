import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { mkdtempSync, readdirSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const [executable, expected] = process.argv.slice(2);
assert(executable && expected, "Usage: test-mcp.ts <executable> <comma-separated-tools>");
const binary = path.resolve(executable);
const expectedTools = expected.split(",").filter(Boolean);
const temporary = mkdtempSync(path.join(tmpdir(), "mcp-july-conformance-"));

const JULY_VERSION = "2026-07-28";

function isolatedEnv(): Record<string, string> {
  return {
    HOME: temporary,
    PATH: "/usr/bin:/bin",
    XDG_CONFIG_HOME: path.join(temporary, "config"),
    XDG_DATA_HOME: path.join(temporary, "data"),
    XDG_STATE_HOME: path.join(temporary, "state"),
    BUGSINK_DSN: "",
  };
}

function rpcRequest(id: number, method: string, params: Record<string, unknown> = {}) {
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

function assertDiscoverResult(resp: any, label: string): string {
  assert(resp?.result, `${label}: missing server/discover result`);
  assert.equal(resp.result.resultType, "complete", `${label}: missing resultType=complete on discover`);
  assert.deepEqual(resp.result.supportedVersions, [JULY_VERSION], `${label}: server must support only ${JULY_VERSION}`);
  const serverName = resp.result._meta?.["io.modelcontextprotocol/serverInfo"]?.name;
  assert(serverName, `${label}: missing serverInfo.name`);
  assert(typeof resp.result.ttlMs === "number" && resp.result.ttlMs >= 0, `${label}: missing CacheableResult.ttlMs`);
  return serverName;
}

function assertToolsListResult(resp: any, label: string): Map<string, any> {
  assert(Array.isArray(resp?.result?.tools), `${label}: missing tools array`);
  assert.equal(resp.result.resultType, "complete", `${label}: missing resultType=complete on tools/list`);
  assert(typeof resp.result.ttlMs === "number" && resp.result.ttlMs >= 0, `${label}: missing CacheableResult.ttlMs on tools/list`);
  const tools = new Map<string, any>(resp.result.tools.map((tool: any) => [tool.name, tool]));
  for (const name of expectedTools) {
    assert(tools.has(name), `${label}: missing tool: ${name}`);
    assert.equal(tools.get(name).inputSchema?.type, "object", `${label}: invalid schema: ${name}`);
  }
  return tools;
}

async function runHttpChecks(): Promise<{ serverName: string; toolCount: number }> {
  const child = spawn(binary, ["--skip-checks", "http", "--host", "127.0.0.1", "--port", "0"], {
    cwd: temporary,
    env: isolatedEnv(),
    stdio: ["ignore", "pipe", "pipe"],
  });

  let stderr = "";
  const endpointPromise = new Promise<string>((resolve, reject) => {
    const timer = setTimeout(() => {
      reject(new Error(`Timed out waiting for HTTP server startup; stderr: ${stderr}`));
    }, 10000);

    child.stderr.setEncoding("utf8").on("data", chunk => {
      stderr += chunk;
      const match = /listening on (http:\/\/(?:127\.0\.0\.1|0\.0\.0\.0):\d+\/mcp)/.exec(stderr);
      if (match?.[1]) {
        clearTimeout(timer);
        resolve(match[1].replace("http://0.0.0.0:", "http://127.0.0.1:"));
      }
    });
    child.once("error", err => {
      clearTimeout(timer);
      reject(err);
    });
    child.once("close", (code, signal) => {
      clearTimeout(timer);
      reject(new Error(`HTTP server exited before listening (code=${code}, signal=${signal}): ${stderr}`));
    });
  });

  try {
    const endpoint = await endpointPromise;

    const getResp = await fetch(endpoint, { method: "GET" });
    assert.equal(getResp.status, 405, `HTTP GET ${endpoint} should return 405`);

    const openapiUrl = new URL("/openapi.json", endpoint).toString();
    const openapiResp = await fetch(openapiUrl, { method: "GET" });
    assert.equal(openapiResp.status, 200, `HTTP GET ${openapiUrl} status: ${openapiResp.status}`);
    const openapi = await openapiResp.json();
    assert.equal(openapi?.openapi, "3.1.0", `HTTP GET ${openapiUrl} must return OpenAPI 3.1.0`);
    for (const name of expectedTools) {
      assert(openapi?.paths?.[`/${name}`]?.post, `HTTP GET ${openapiUrl} missing POST /${name}`);
    }

    const postMcp = async (method: string, body: unknown) => {
      const resp = await fetch(endpoint, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          "Accept": "application/json, text/event-stream",
          "MCP-Protocol-Version": JULY_VERSION,
          "Mcp-Method": method,
        },
        body: JSON.stringify(body),
      });
      const json = await resp.json();
      return { status: resp.status, json };
    };

    const discover = await postMcp("server/discover", rpcRequest(1, "server/discover"));
    assert.equal(discover.status, 200, `HTTP server/discover status: ${discover.status}`);
    const serverName = assertDiscoverResult(discover.json, "http");

    const list = await postMcp("tools/list", rpcRequest(2, "tools/list"));
    assert.equal(list.status, 200, `HTTP tools/list status: ${list.status}`);
    const tools = assertToolsListResult(list.json, "http");

    const legacyInit = await postMcp("initialize", {
      jsonrpc: "2.0",
      id: 3,
      method: "initialize",
      params: { protocolVersion: "2025-11-25" },
    });
    assert.equal(legacyInit.status, 404, "HTTP legacy initialize must return 404");
    assert.equal(legacyInit.json?.error?.code, -32601, "HTTP legacy initialize must fail with -32601");

    const exitPromise = new Promise<{ code: number | null; signal: NodeJS.Signals | null }>((resolve, reject) => {
      const killTimer = setTimeout(() => {
        child.kill("SIGKILL");
        reject(new Error("HTTP server did not exit cleanly on SIGTERM"));
      }, 10000);
      child.once("close", (code, signal) => {
        clearTimeout(killTimer);
        resolve({ code, signal });
      });
    });
    child.kill("SIGTERM");
    const exit = await exitPromise;
    assert.equal(exit.code, 0, `HTTP server exited ${exit.code} (signal=${exit.signal}); stderr: ${stderr}`);

    return { serverName, toolCount: tools.size };
  } catch (err) {
    child.kill("SIGKILL");
    throw err;
  }
}

try {
  const { serverName, toolCount } = await runHttpChecks();
  assert.deepEqual(readdirSync(temporary), [], "Server created files during discovery");
  console.log(`MCP 2026-07-28 http conformance passed: ${serverName}, ${toolCount} tools`);
} finally {
  rmSync(temporary, { recursive: true, force: true });
}
