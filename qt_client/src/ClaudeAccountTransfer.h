#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

// Portable transfer of the owner's Claude Code account onto another node.
//
// A ForkMesh node only runs "claude-code" agents when the real `claude` CLI on
// that machine is logged in: the CLI keeps a claude.ai subscription OAuth token
// in ~/.claude/.credentials.json (see claudeCodeOAuthToken() in
// MainWindowInternal.h). Headless hosts (a VPS node the owner brings online to
// take agent requests) start out with no such login, so they cannot service
// "start an agent" requests from a logged-in owner.
//
// This module packages that account — the claudeAiOauth object plus any Claude
// API key ForkMesh has configured — into a single self-describing blob the owner
// can move to a host (scp, paste over SSH, a keyfile), and installs it there by
// writing ~/.claude/.credentials.json. It deliberately touches only the account
// material; nothing here talks to the relay, so the token never leaves the two
// machines the owner controls.
//
// Kept free of Qt Widgets / MainWindow so it links into forkmesh-tests.
namespace ClaudeAccountTransfer {

// Absolute path to the Claude Code credential file under `home`.
QString credentialsPath(const QString &home);

// The `claudeAiOauth` object the `claude` CLI stores under `home`, or an empty
// object when the file is missing/unparseable or the user logged in with an API
// key instead of a subscription. Read fresh so a rotated token is picked up.
QJsonObject readOauthObject(const QString &home);

// True when `home` carries a usable Claude Code subscription login (an oauth
// object with a non-empty accessToken).
bool hasCredentials(const QString &home);

// Compact JSON describing the account bundle: {kind, v, claudeAiOauth?,
// anthropicApiKey?, exportedFrom?}. Either credential may be empty; at least one
// should be present for the bundle to be useful.
QByteArray buildBundle(const QJsonObject &oauth, const QString &apiKey,
                       const QString &exportedFrom = QString());

// buildBundle() base64-encoded to a single line — easy to paste over SSH or drop
// into a keyfile without newline mangling.
QString encodeBundle(const QJsonObject &oauth, const QString &apiKey,
                     const QString &exportedFrom = QString());

// Decode a bundle produced by buildBundle()/encodeBundle(). Accepts either the
// base64 form or the raw JSON (so an imported keyfile can be read as-is). Fills
// `oauthOut`/`apiKeyOut` and returns true on success; on failure returns false
// and sets `err`.
bool parseBundle(const QByteArray &input, QJsonObject &oauthOut,
                 QString &apiKeyOut, QString &err);

// Install `oauth` as the Claude Code login under `home`: merge it into an
// existing ~/.claude/.credentials.json (preserving any sibling keys) or create
// the file, then tighten permissions to owner-only. Returns false + `err` on any
// filesystem failure. A no-op success when `oauth` is empty.
bool installOauth(const QString &home, const QJsonObject &oauth, QString &err);

// One-line, token-safe summary of an oauth object for status output:
// subscription type, expiry, and a masked token tail — never the token itself.
QString describeOauth(const QJsonObject &oauth);

} // namespace ClaudeAccountTransfer
