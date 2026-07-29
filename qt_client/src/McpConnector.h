#pragma once

#include <QString>

// MCP connector (adhoc #16). tools/forkmesh_mcp_server.py exposes the mesh —
// repos, issues, projects, PRs — to any MCP-capable agent over stdio. Anything
// that only reads is harmless, but the write tools sign with this node's
// identity key, so an agent holding them acts *as this node*. The connector is
// the capability that grants that: a locally minted bearer token, stored beside
// the identity key, handed to the agent through its MCP config.
//
// Without a connector file the server stays in its historical open local mode
// (a subprocess you launched yourself, on your own machine). Once a connector
// exists the server refuses every write tool unless the caller presents the
// matching token in FORKMESH_MCP_TOKEN, so revoking is a one-click operation
// that instantly demotes every agent config still holding the old string.
//
// Deliberately Qt Core only (no widgets): the Settings page renders it, but the
// headless tests link it on its own.
namespace forkmesh::mcp {

// One minted connector, as persisted in <appData>/mcp/connector.json.
struct Connector
{
    QString token;      // fmcp_<base64url>; the string the agent presents
    QString node;       // base64url identity public key that minted it
    QString label;      // free-form ("Claude Code on this machine")
    qint64 createdMs = 0;

    bool isValid() const { return !token.isEmpty(); }
};

// <appDataDir>/mcp/connector.json — the same file the Python server reads.
// appDataDir is QStandardPaths::AppDataLocation (…/ForkMesh/ForkMesh).
QString connectorPath(const QString &appDataDir);

// A fresh token: "fmcp_" + 32 CSPRNG bytes, base64url, unpadded.
QString generateToken();
bool isWellFormedToken(const QString &token);

// Missing/unreadable/malformed files all read back as an invalid Connector —
// callers treat "no connector" and "broken connector" the same way.
Connector loadConnector(const QString &appDataDir);

// Writes connector.json 0600 (it is a bearer credential). Returns false and
// fills error on any failure.
bool saveConnector(const QString &appDataDir, const Connector &connector,
                   QString *error);

// Deletes connector.json. Succeeds if it was already gone.
bool revokeConnector(const QString &appDataDir, QString *error);

// The .mcp.json / claude_desktop_config.json block for this machine. reposDir
// and token may be empty — they are simply omitted from the env map.
QString configJson(const QString &python, const QString &serverScript,
                   const QString &reposDir, const QString &token);

// The equivalent one-liner for `claude mcp add`, shell-quoted.
QString cliCommand(const QString &python, const QString &serverScript,
                   const QString &reposDir, const QString &token);

// Show a token as fmcp_abcd…wxyz so screenshots and screen shares do not leak
// the whole thing.
QString maskToken(const QString &token);

} // namespace forkmesh::mcp
