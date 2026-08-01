#pragma once

#include <QList>
#include <QString>
#include <QStringList>









struct AiReviewFinding {
    QString path;
    int lineStart = 0;
    int lineEnd = 0;
    QString severity;
    QString comment;




    QString suggestionPatch;
};




QString buildAiReviewPrompt(const QString &title, const QString &description,
                            const QString &patch, int maxDiffChars = 120000);








QList<AiReviewFinding> parseAiReviewFindings(const QString &text,
                                             bool *ok = nullptr);






QString buildSuggestionPatch(const QStringList &original,
                             const QStringList &replacement, int lineStart);
bool parseSuggestionPatch(const QString &patch, QStringList *original,
                          QStringList *replacement);






bool applySuggestionToContent(QString *content, int lineStart,
                              const QString &patch, QString *error = nullptr);
