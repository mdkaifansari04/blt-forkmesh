import * as vscode from "vscode";
import { ForkMeshIssue, findIssuesDir, loadIssues } from "./issues";

export class IssueItem extends vscode.TreeItem {
  constructor(public readonly issue: ForkMeshIssue) {
    super(`#${issue.number}  ${issue.title}`, vscode.TreeItemCollapsibleState.None);
    this.contextValue = "forkmeshIssue";
    this.tooltip = new vscode.MarkdownString(
      `**#${issue.number} ${issue.title}**\n\n` +
        (issue.labels.length ? `Labels: ${issue.labels.join(", ")}\n\n` : "") +
        (issue.body ? issue.body.slice(0, 600) : "_no description_")
    );
    const bits: string[] = [];
    if (issue.priority) {
      bits.push(`P${issue.priority}`);
    }
    if (issue.labels.length) {
      bits.push(issue.labels.join(", "));
    }
    this.description = bits.join(" · ");
    this.iconPath = new vscode.ThemeIcon(
      issue.status === "closed" ? "issue-closed" : "issues"
    );
    this.command = {
      command: "forkmesh.openIssue",
      title: "Open Issue",
      arguments: [this],
    };
  }
}

export class IssuesProvider implements vscode.TreeDataProvider<IssueItem> {
  private readonly _onDidChange = new vscode.EventEmitter<void>();
  readonly onDidChangeTreeData = this._onDidChange.event;

  refresh(): void {
    this._onDidChange.fire();
  }

  getTreeItem(element: IssueItem): vscode.TreeItem {
    return element;
  }

  getChildren(): IssueItem[] {
    const cfg = vscode.workspace.getConfiguration("forkmesh");
    const roots = (vscode.workspace.workspaceFolders ?? []).map(
      (f) => f.uri.fsPath
    );
    const dir = findIssuesDir(roots, cfg.get<string>("issuesPath") || "");
    if (!dir) {
      return [];
    }
    const issues = loadIssues(dir, cfg.get<boolean>("showClosedIssues") || false);
    return issues.map((i) => new IssueItem(i));
  }
}
