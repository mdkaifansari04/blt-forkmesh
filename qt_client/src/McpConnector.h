#pragma once

#include <QString>
















namespace forkmesh::mcp {


struct Connector
{
    QString token;
    QString node;
    QString label;
    qint64 createdMs = 0;

    bool isValid() const { return !token.isEmpty(); }
};



QString connectorPath(const QString &appDataDir);


QString generateToken();
bool isWellFormedToken(const QString &token);



Connector loadConnector(const QString &appDataDir);



bool saveConnector(const QString &appDataDir, const Connector &connector,
                   QString *error);


bool revokeConnector(const QString &appDataDir, QString *error);



QString configJson(const QString &python, const QString &serverScript,
                   const QString &reposDir, const QString &token);


QString cliCommand(const QString &python, const QString &serverScript,
                   const QString &reposDir, const QString &token);



QString maskToken(const QString &token);

}
