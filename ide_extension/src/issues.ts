// Parse ForkMesh's file-based issue tracker (see issues/README.md):
//   issues/<N>/issue.md          frontmatter (metadata + open event) + body
//   issues/<N>/NNNN-status.md     append-only later events (status changes, ...)
//
// We only need a light read here: title, current status, labels and the body,
// so a small frontmatter parser is enough — no need to verify signatures.
import * as fs from "fs";
import * as path from "path";

export interface ForkMeshIssue {
  number: number;
  title: string;
  status: string; // "open" | "closed"
  labels: string[];
  priority: number;
  assignees: string[];
  body: string;
  issueFile: string; // absolute path to issue.md
  repoRoot: string; // the directory containing issues/
}

interface Frontmatter {
  fields: Record<string, string>;
  body: string;
}

function parseFrontmatter(text: string): Frontmatter {
  const fields: Record<string, string> = {};
  if (!text.startsWith("---")) {
    return { fields, body: text };
  }
  const end = text.indexOf("\n---", 3);
  if (end < 0) {
    return { fields, body: text };
  }
  const header = text.slice(3, end).trim();
  for (const line of header.split("\n")) {
    const i = line.indexOf(":");
    if (i < 0) {
      continue;
    }
    const key = line.slice(0, i).trim();
    fields[key] = line.slice(i + 1).trim();
  }
  // Body starts after the closing "---" line.
  const afterDashes = text.indexOf("\n", end + 1);
  const body = afterDashes < 0 ? "" : text.slice(afterDashes + 1);
  return { fields, body: body.trim() };
}

// Parse a YAML-ish inline list: "[a, b]" or "a, b" -> ["a","b"].
function parseList(value: string | undefined): string[] {
  if (!value) {
    return [];
  }
  return value
    .replace(/^\[/, "")
    .replace(/\]$/, "")
    .split(",")
    .map((s) => s.trim().replace(/^["']|["']$/g, ""))
    .filter((s) => s.length > 0);
}

// The current status is whatever the latest signed `*-status.md` event says,
// falling back to the status in issue.md's frontmatter.
function currentStatus(issueDir: string, fallback: string): string {
  let bestTs = -1;
  let status = fallback;
  let entries: string[] = [];
  try {
    entries = fs.readdirSync(issueDir);
  } catch {
    return fallback;
  }
  for (const name of entries) {
    if (!/^\d+-status\.md$/.test(name)) {
      continue;
    }
    try {
      const fm = parseFrontmatter(
        fs.readFileSync(path.join(issueDir, name), "utf8")
      );
      const ts = parseInt(fm.fields["ts"] ?? "0", 10) || 0;
      if (ts >= bestTs && fm.fields["status"]) {
        bestTs = ts;
        status = fm.fields["status"];
      }
    } catch {
      /* skip unreadable events */
    }
  }
  return status;
}

function readIssue(issuesDir: string, repoRoot: string, dirName: string): ForkMeshIssue | undefined {
  const issueDir = path.join(issuesDir, dirName);
  const issueFile = path.join(issueDir, "issue.md");
  let text: string;
  try {
    text = fs.readFileSync(issueFile, "utf8");
  } catch {
    return undefined;
  }
  const { fields, body } = parseFrontmatter(text);
  const number = parseInt(fields["number"] ?? dirName, 10);
  if (!Number.isFinite(number)) {
    return undefined;
  }
  return {
    number,
    title: fields["title"] || `Issue ${number}`,
    status: currentStatus(issueDir, fields["status"] || "open"),
    labels: parseList(fields["labels"]),
    priority: parseInt(fields["priority"] ?? "0", 10) || 0,
    assignees: parseList(fields["assignees"]),
    body,
    issueFile,
    repoRoot,
  };
}

// Locate the issues directory: an explicit override, else <root>/issues for the
// first workspace root that has one.
export function findIssuesDir(roots: string[], override: string): string | undefined {
  if (override && override.trim()) {
    const dir = override.trim();
    return fs.existsSync(dir) ? dir : undefined;
  }
  for (const root of roots) {
    const dir = path.join(root, "issues");
    try {
      if (fs.statSync(dir).isDirectory()) {
        return dir;
      }
    } catch {
      /* keep looking */
    }
  }
  return undefined;
}

export function loadIssues(issuesDir: string, includeClosed: boolean): ForkMeshIssue[] {
  const repoRoot = path.dirname(issuesDir);
  let entries: string[] = [];
  try {
    entries = fs.readdirSync(issuesDir);
  } catch {
    return [];
  }
  const issues: ForkMeshIssue[] = [];
  for (const name of entries) {
    if (!/^\d+$/.test(name)) {
      continue; // skip labels.json, milestones.json, README.md, ...
    }
    const issue = readIssue(issuesDir, repoRoot, name);
    if (issue && (includeClosed || issue.status === "open")) {
      issues.push(issue);
    }
  }
  // Open first, then by ascending priority (1 = highest, 0 = unset -> last),
  // then by number.
  issues.sort((a, b) => {
    const pa = a.priority || 999;
    const pb = b.priority || 999;
    if (pa !== pb) {
      return pa - pb;
    }
    return a.number - b.number;
  });
  return issues;
}

// Read a single issue by number from a known repo root (used for Qt requests).
export function loadIssueByNumber(repoRoot: string, n: number): ForkMeshIssue | undefined {
  return readIssue(path.join(repoRoot, "issues"), repoRoot, String(n));
}
