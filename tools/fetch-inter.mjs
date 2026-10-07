#!/usr/bin/env node
// Downloads the Inter font (SIL OFL 1.1) into assets/fonts: InterVariable.ttf and LICENSE.txt.
// Usage: node tools/fetch-inter.mjs
import { mkdtempSync, writeFileSync, copyFileSync, existsSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const out = join(root, "assets", "fonts");
const url = "https://github.com/rsms/inter/releases/download/v4.0/Inter-4.0.zip";

const work = mkdtempSync(join(tmpdir(), "inter-"));
try {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`download failed: ${res.status} ${res.statusText}`);
  const zip = join(work, "inter.zip");
  writeFileSync(zip, Buffer.from(await res.arrayBuffer()));
  // on Windows use the system bsdtar (reads zip files); a GNU tar from Git Bash would treat "C:" as a remote host
  const tar = process.platform === "win32" ? join(process.env.SystemRoot ?? "C:\Windows", "System32", "tar.exe") : "tar";
  const x = spawnSync(tar, ["-xf", "inter.zip"], { cwd: work, stdio: "inherit" });
  if (x.status !== 0) throw new Error("could not extract the zip (needs a tar that reads zip files)");
  for (const f of ["InterVariable.ttf", "LICENSE.txt"]) {
    const src = join(work, f);
    if (!existsSync(src)) throw new Error(`${f} not found in the archive`);
    copyFileSync(src, join(out, f));
  }
  console.log("Inter written to assets/fonts");
} finally {
  rmSync(work, { recursive: true, force: true });
}
