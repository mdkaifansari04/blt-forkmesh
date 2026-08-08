#pragma once


#include <QString>
#include <QStringList>

#if defined(__GNUC__) || defined(__clang__) || \
    (defined(_MSC_VER) && _MSC_VER >= 1926)
#define FORKMESH_LOG_SOURCE_FILE __builtin_FILE()
#define FORKMESH_LOG_SOURCE_LINE __builtin_LINE()
#else
#define FORKMESH_LOG_SOURCE_FILE nullptr
#define FORKMESH_LOG_SOURCE_LINE 0
#endif

namespace forkmesh {

QString logSourceRelativePath(const char *absolutePath);

QString logSourceSuffix(const QString &relativePath, int line);

bool splitLogSource(const QString &message, QString *body, QString *path,
                    int *line);

QString logMessageBody(const QString &message);

QString logSourceLabel(const QString &relativePath, int line);

QString resolveLogSourcePath(const QString &relativePath,
                             const QStringList &extraRoots = {});

QString logSourceBuildRoot();

} // namespace forkmesh
