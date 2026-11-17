import { existsSync, mkdirSync, readFileSync, realpathSync, rmSync, writeFileSync } from "node:fs";
import { join, relative, resolve } from "node:path";
import { $ } from "bun";
import {
  applyBump, assertSemver, assetFilename, type Bump, bumpFromLabels, detectRepo, errorMessage,
  formatCommitEntries, insertChangelogSection, isDocsOnlyFiles, maxBump, type PlatformAsset,
  readPlatforms, type RepoInfo, resolveGitHubRepo, sha256File, updateCaskRuby, updateFormulaRuby,
} from "./formula-shared.ts";

type Command =
  | { kind: "classify"; files: string[] }
  | { kind: "plan"; labels?: string[] }
  | { kind: "publish"; version: string; assetsDir: string };

export function parseArgs(args: string[]): Command {
  if (args[0] === "classify") {
    return { kind: "classify", files: args.slice(1) };
  }
  if (args[0] === "plan") {
    let labels: string[] | undefined;
    for (let i = 1; i < args.length; i++) {
      if (args[i] === "--labels") {
        const raw = args[++i] ?? "";
        labels = raw.split(",").map((s) => s.trim()).filter(Boolean);
      } else {
        throw new Error(`Unknown plan argument: ${args[i]}`);
      }
    }
    return { kind: "plan", labels };
  }
  if (args[0] === "publish" && (args.length === 2 || args.length === 3)) {
    return { kind: "publish", version: args[1], assetsDir: args[2] ?? "dist" };
  }
  throw new Error(
    "Usage: release.ts <classify <files...> | plan [--labels <csv>] | publish <version> [assets-dir]>",
  );
}

function readCurrentVersion(repoRoot: string, repo: RepoInfo): string {
  const versionPath = repo.kind === "library" ? repo.portManifestPath! : join(repoRoot, "VERSION");
  const raw = readFileSync(versionPath, "utf8");
  if (repo.kind === "library") {
    const manifest = JSON.parse(raw) as { version?: unknown };
    if (typeof manifest.version !== "string") {
      throw new Error(`${versionPath} must contain a string "version" field.`);
    }
    return assertSemver(manifest.version, versionPath);
  }
  return assertSemver(raw, versionPath);
}

async function latestVersionTag(repoRoot: string): Promise<string | null> {
  const res = await $`git tag --merged HEAD --sort=-v:refname`.cwd(repoRoot).nothrow().quiet();
  if (res.exitCode !== 0) return null;
  const match = res.stdout
    .toString()
    .split("\n")
    .map((t) => t.trim())
    .find((t) => /^v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.test(t));
  return match ?? null;
}

async function isHeadTagged(repoRoot: string): Promise<boolean> {
  const res = await $`git tag --points-at HEAD`.cwd(repoRoot).nothrow().quiet();
  if (res.exitCode !== 0) return false;
  return res.stdout
    .toString()
    .split("\n")
    .some((t) => /^v\d/.test(t.trim()));
}

async function changedFilesSinceTag(repoRoot: string, lastTag: string | null): Promise<string[]> {
  if (lastTag) {
    const res = await $`git diff --name-only ${`${lastTag}..HEAD`}`.cwd(repoRoot).quiet();
    return res.stdout.toString().split("\n").map((s) => s.trim()).filter(Boolean);
  }
  const res = await $`git log --format= --name-only HEAD`.cwd(repoRoot).quiet();
  const unique = new Set(res.stdout.toString().split("\n").map((s) => s.trim()).filter(Boolean));
  return [...unique];
}

async function commitsSinceTag(repoRoot: string, lastTag: string | null): Promise<Array<{ sha: string; subject: string; body: string }>> {
  const range = lastTag ? `${lastTag}..HEAD` : "HEAD";
  const res = await $`git log --format=%H%x1f%s%x1f%b%x1e ${range}`.cwd(repoRoot).quiet();
  return res.stdout
    .toString()
    .split("\x1e")
    .map((rec) => rec.trim())
    .filter(Boolean)
    .map((rec) => {
      const [sha = "", subject = "", body = ""] = rec.split("\x1f");
      return { sha: sha.trim(), subject: subject.trim(), body: body.trim() };
    })
    .filter((c) => c.subject.length > 0);
}

async function fetchPrLabels(repoRoot: string, ownerRepo: string, sha: string): Promise<string[]> {
  const res = await $`gh api ${`repos/${ownerRepo}/commits/${sha}/pulls`} --jq ${".[].labels[].name"}`
    .cwd(repoRoot)
    .nothrow()
    .quiet();
  if (res.exitCode !== 0) {
    throw new Error(`Failed to fetch PR labels for ${sha}: ${res.stderr.toString().trim() || `exit ${res.exitCode}`}`);
  }
  return res.stdout.toString().split("\n").map((s) => s.trim()).filter(Boolean);
}

async function runPlan(repoRoot: string, labelsOverride?: string[]): Promise<void> {
  const repo = detectRepo(repoRoot);
  if (await isHeadTagged(repoRoot)) {
    process.stdout.write(`${JSON.stringify({ should_release: false, version: "" })}\n`);
    return;
  }

  const lastTag = await latestVersionTag(repoRoot);
  const changed = await changedFilesSinceTag(repoRoot, lastTag);
  if (changed.length === 0 || isDocsOnlyFiles(changed)) {
    process.stdout.write(`${JSON.stringify({ should_release: false, version: "" })}\n`);
    return;
  }

  let bump: Bump = "patch";
  if (labelsOverride !== undefined) {
    bump = bumpFromLabels(labelsOverride);
  } else {
    const originRes = await $`git remote get-url origin`.cwd(repoRoot).nothrow().quiet();
    const ownerRepo = originRes.exitCode === 0 ? resolveGitHubRepo(originRes.stdout.toString()) : null;
    if (ownerRepo) {
      const commits = await commitsSinceTag(repoRoot, lastTag);
      for (const c of commits) {
        const prLabels = await fetchPrLabels(repoRoot, ownerRepo, c.sha);
        if (prLabels.length > 0) {
          bump = maxBump(bump, bumpFromLabels(prLabels));
        }
      }
    }
  }

  const current = readCurrentVersion(repoRoot, repo);
  const nextVersion = applyBump(current, bump);
  process.stdout.write(`${JSON.stringify({ should_release: true, version: nextVersion })}\n`);
}

async function runPublish(repoRoot: string, versionArg: string, assetsDirArg: string): Promise<void> {
  const version = assertSemver(versionArg, "Release version");
  const tag = `v${version}`;
  const repo = detectRepo(repoRoot);

  if (await isHeadTagged(repoRoot)) {
    console.log(`Already released (${tag})`);
    return;
  }

  const top = (await $`git rev-parse --show-toplevel`.cwd(repoRoot).quiet().text()).trim();
  if (realpathSync(top) !== realpathSync(repoRoot)) {
    throw new Error("Run releases from the repository root.");
  }
  await $`git rev-parse --verify HEAD`.cwd(repoRoot).quiet();
  const origin = (await $`git remote get-url origin`.cwd(repoRoot).quiet().text()).trim();
  const branch = (await $`git symbolic-ref --short HEAD`.cwd(repoRoot).quiet().text()).trim();

  const localTag = await $`git show-ref --verify --quiet ${`refs/tags/${tag}`}`.cwd(repoRoot).nothrow().quiet();
  if (localTag.exitCode === 0) throw new Error(`${tag} already exists locally.`);
  if (localTag.exitCode !== 1) throw new Error(`Cannot check local tag ${tag}.`);
  const remoteTag = await $`git ls-remote --exit-code origin ${`refs/tags/${tag}`}`.cwd(repoRoot).nothrow().quiet();
  if (remoteTag.exitCode === 0) throw new Error(`${tag} already exists on origin.`);
  if (remoteTag.exitCode !== 2) throw new Error(`Cannot check remote tag ${tag}: ${remoteTag.stderr.toString()}`);

  const lastTag = await latestVersionTag(repoRoot);
  const commits = await commitsSinceTag(repoRoot, lastTag);
  const notes = commits.length > 0 ? `${formatCommitEntries(commits)}\n` : `- Release ${tag}\n`;

  const changelogPath = join(repoRoot, "CHANGELOG.md");
  const changelog = insertChangelogSection(
    readFileSync(changelogPath, "utf8"),
    version,
    new Date().toISOString().slice(0, 10),
    notes,
  );

  const assetsDir = resolve(repoRoot, assetsDirArg);
  const assetPaths: string[] = [];
  let ownerRepo: string | null = null;

  if (repo.kind === "formula") {
    const platforms = readPlatforms(repoRoot, "formula");
    const rubyPath = repo.rubyPath!;
    const rubyContent = readFileSync(rubyPath, "utf8");
    ownerRepo = resolveGitHubRepo(origin, rubyContent);
    if (!ownerRepo) throw new Error("Cannot determine GitHub owner/repo from origin or Formula homepage.");
    const repoUrl = `https://github.com/${ownerRepo}`;

    let macosAsset: PlatformAsset | undefined;
    let linuxAsset: PlatformAsset | undefined;
    for (const platform of platforms) {
      const assetName = assetFilename(repo.name, "formula", platform);
      const assetPath = join(assetsDir, assetName);
      if (!existsSync(assetPath)) throw new Error(`Missing release asset: ${assetPath}`);
      assetPaths.push(assetPath);
      const asset = { assetName, sha256: sha256File(assetPath) };
      if (platform === "macos") macosAsset = asset;
      else linuxAsset = asset;
    }

    writeFileSync(join(repoRoot, "VERSION"), `${version}\n`);
    writeFileSync(changelogPath, changelog);
    writeFileSync(rubyPath, updateFormulaRuby(rubyContent, { version, repoUrl, macos: macosAsset, linux: linuxAsset }));
    await $`git add -- VERSION CHANGELOG.md ${`Formula/${repo.name}.rb`}`.cwd(repoRoot).quiet();
    await $`git commit -m ${`Release ${tag}`}`.cwd(repoRoot).quiet();
  } else if (repo.kind === "cask") {
    readPlatforms(repoRoot, "cask");
    const rubyPath = repo.rubyPath!;
    const rubyContent = readFileSync(rubyPath, "utf8");
    ownerRepo = resolveGitHubRepo(origin, rubyContent);
    if (!ownerRepo) throw new Error("Cannot determine GitHub owner/repo from origin or Cask homepage.");
    const repoUrl = `https://github.com/${ownerRepo}`;

    const matches = (["pkg", "dmg", "zip"] as const)
      .map((ext) => assetFilename(repo.name, "cask", "macos", ext))
      .filter((name) => existsSync(join(assetsDir, name)));
    if (matches.length !== 1) {
      throw new Error(`Expected exactly 1 cask asset (.pkg/.dmg/.zip) in ${assetsDir}, found ${matches.length}.`);
    }
    const assetName = matches[0];
    const assetPath = join(assetsDir, assetName);
    assetPaths.push(assetPath);

    writeFileSync(join(repoRoot, "VERSION"), `${version}\n`);
    writeFileSync(changelogPath, changelog);
    writeFileSync(rubyPath, updateCaskRuby(rubyContent, { version, repoUrl, assetName, sha256: sha256File(assetPath) }));
    await $`git add -- VERSION CHANGELOG.md ${`Casks/${repo.name}.rb`}`.cwd(repoRoot).quiet();
    await $`git commit -m ${`Release ${tag}`}`.cwd(repoRoot).quiet();
  } else {
    ownerRepo = resolveGitHubRepo(origin);
    const manifestPath = repo.portManifestPath!;
    const manifestRaw = readFileSync(manifestPath, "utf8");
    if (!/"version"\s*:\s*"[^"]*"/.test(manifestRaw)) {
      throw new Error(`${manifestPath} must contain a "version" field.`);
    }
    writeFileSync(manifestPath, manifestRaw.replace(/("version"\s*:\s*)"[^"]*"/, `$1${JSON.stringify(version)}`));
    writeFileSync(changelogPath, changelog);
    await $`git add -- ${relative(repoRoot, manifestPath)} CHANGELOG.md`.cwd(repoRoot).quiet();
    await $`git commit -m ${`Release ${tag}`}`.cwd(repoRoot).quiet();

    mkdirSync(join(repoRoot, "versions"), { recursive: true });
    await $`vcpkg x-add-version ${repo.name} ${`--x-builtin-ports-root=${join(repoRoot, "ports")}`} ${`--x-builtin-registry-versions-dir=${join(repoRoot, "versions")}`} --verbose`
      .cwd(repoRoot)
      .quiet();
    await $`git add -- versions`.cwd(repoRoot).quiet();
    await $`git commit --amend --no-edit`.cwd(repoRoot).quiet();
  }

  await $`git tag -a ${tag} -m ${tag}`.cwd(repoRoot).quiet();
  await $`git push --atomic origin ${`HEAD:refs/heads/${branch}`} ${`refs/tags/${tag}`}`.cwd(repoRoot).quiet();

  const notesPath = join(repoRoot, ".git", "release-notes.md");
  writeFileSync(notesPath, notes);
  try {
    const ghArgs = [
      "release",
      "create",
      tag,
      ...assetPaths,
      ...(ownerRepo ? ["--repo", ownerRepo] : []),
      "--verify-tag",
      "--title",
      tag,
      "--notes-file",
      notesPath,
    ];
    await $`gh ${ghArgs}`.cwd(repoRoot).quiet();
  } finally {
    rmSync(notesPath, { force: true });
  }
  console.log(`Released ${tag}`);
}

async function main(): Promise<void> {
  const command = parseArgs(process.argv.slice(2));
  const repoRoot = process.cwd();
  if (command.kind === "classify") {
    process.stdout.write(`${isDocsOnlyFiles(command.files)}\n`);
  } else if (command.kind === "plan") {
    await runPlan(repoRoot, command.labels);
  } else {
    await runPublish(repoRoot, command.version, command.assetsDir);
  }
}

if (import.meta.main) {
  await main().catch((error: unknown) => {
    console.error(`Release stopped: ${errorMessage(error)}`);
    process.exitCode = 1;
  });
}
