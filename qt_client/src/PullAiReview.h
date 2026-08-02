#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// AI code review of a pull request (adhoc #82). The "Review with AI" button
// sends the PR's diff to a model and turns its reply into line-anchored review
// threads; findings that carry a safe mechanical fix also get a suggestion
// patch the reviewer can apply and commit in one click. These helpers are pure
// (Qt Core only) so forkmesh-tests can exercise them without Widgets.

// One issue reported by the model. Lines are 1-based and refer to the head
// ("new") side of the diff — the file as it reads after the change.
struct AiReviewFinding {
    QString path;      // repo-relative file the issue is in
    int lineStart = 0; // first affected line (new side)
    int lineEnd = 0;   // last affected line (>= lineStart)
    QString severity;  // bug | warning | nit
    QString comment;   // what is wrong and why
    // Committable quick fix: a suggestion patch (see buildSuggestionPatch)
    // replacing lines lineStart..lineEnd. Empty when the model offered no safe
    // self-contained fix — those findings are left to a human or the
    // "Fix all with AI" agent.
    QString suggestionPatch;
};

// The instruction block + PR context sent to the model. The diff is capped at
// maxDiffChars (cut on a line boundary, with a truncation note in the prompt)
// so a huge PR can't blow the request budget.
QString buildAiReviewPrompt(const QString &title, const QString &description,
                            const QString &patch, int maxDiffChars = 120000);

// Parse the model's reply into findings. Accepts a bare JSON array, a
// {"findings": [...]} wrapper, or a reply with surrounding chatter/code fences
// (the outermost [...] block is used). Entries missing a path, comment or
// positive line number are dropped; a finding's quick fix is kept only when
// the model supplied both the original lines and their replacement. When `ok`
// is non-null it reports whether a JSON array was found at all, so callers can
// tell "no issues" from "unparseable reply".
QList<AiReviewFinding> parseAiReviewFindings(const QString &text,
                                             bool *ok = nullptr);

// A suggestion patch is a minimal diff-style block — "-" lines are the exact
// current text being replaced, "+" lines the replacement — so the UI can render
// it as a diff and applySuggestionToContent can verify the anchor before
// touching the file. `original` must not be empty (pure insertions are not
// representable); `replacement` may be (the fix deletes the lines).
QString buildSuggestionPatch(const QStringList &original,
                             const QStringList &replacement, int lineStart);
bool parseSuggestionPatch(const QString &patch, QStringList *original,
                          QStringList *replacement);

// Apply a suggestion patch to a file's contents: verify the "-" lines match at
// lineStart (1-based); when they don't (the review's line numbers drifted),
// fall back to the unique occurrence of that block anywhere in the file.
// Returns false with *error set when the original text can't be found or is
// ambiguous — the file has changed since the review.
bool applySuggestionToContent(QString *content, int lineStart,
                              const QString &patch, QString *error = nullptr);
