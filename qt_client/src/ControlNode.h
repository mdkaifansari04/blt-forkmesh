#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QUrl>

class QSettings;

namespace forkmesh::control {





struct CloudflareBootstrapRequest {
    QString hostname;
    QString zoneName;
    QString accountId;
    QString nodeName;
    QString relayLabel;
    QString mainRelayUrl;
    bool dryRun = false;
};

struct CloudflareBootstrapCommand {
    QString program;
    QStringList arguments;
    QProcessEnvironment environment;
};





struct MirrorActionsConfigurationRequest {
    QString requestId;
    QString host;
    QString sshUser;
    QString nodeName;
    bool actionsEnabled = false;
    bool replaceVariables = false;
    QMap<QString, QString> variables;
};

struct MirrorActionsSshCommand {
    QString program;
    QStringList arguments;
    QProcessEnvironment environment;
    QByteArray standardInput;
};







struct HostSshCommand {
    QString program;
    QStringList arguments;
    QProcessEnvironment environment;
};






HostSshCommand buildHostSshCommand(const QString &host,
                                   const QString &sshUser,
                                   const QString &sshPassword,
                                   const QString &remoteCommand,
                                   QString *error = nullptr,
                                   const QString &identityFile = QString());





HostSshCommand buildHostInteractiveSshCommand(
    const QString &host, const QString &sshUser, const QString &sshPassword,
    const QString &remoteCommand, QString *error = nullptr,
    const QString &identityFile = QString());





QString hostSshCommandLine(const HostSshCommand &command);







QString buildHostAgentLoginRemoteCommand();



QString savedHostCredentialKey(const QString &nodeName, const QString &host,
                               const QString &sshUser);



struct HostDiskEntry {
    QString name;
    QString path;
    qint64 bytes = 0;
    bool directory = false;
};


struct HostDiskUsage {
    QString path;
    qint64 totalBytes = 0;
    QList<HostDiskEntry> entries;
    QString error;
    bool complete = false;
};



struct HostMountUsage {
    QString path;
    qint64 totalBytes = 0;
    qint64 usedBytes = 0;
    qint64 availableBytes = 0;
};

struct HostMountUsageList {
    QList<HostMountUsage> mounts;
    QString error;
    bool complete = false;
};




QString normalizeRemoteDiskPath(const QString &path);






QString buildHostDiskUsageCommand(const QString &path,
                                  QString *error = nullptr);



QString buildHostMountUsageCommand();
HostMountUsageList parseHostMountUsage(const QByteArray &output);




HostDiskUsage parseHostDiskUsage(const QByteArray &output,
                                 const QString &path);


QString formatDiskSize(qint64 bytes);












QString sshConnectionFailureHint(int exitCode, const QString &outputTail,
                                 const QString &host = QString());





bool sshFailureNeedsPassword(int exitCode, const QString &outputTail);




QString nonRoutableAddressNote(const QString &host);




QString sshFailureSummary(int exitCode, const QString &outputTail);





QJsonArray loadSavedHosts(QSettings &settings, const QString &settingsKey,
                          QHash<QString, QString> *sessionPasswords = nullptr);
void saveSavedHosts(QSettings &settings, const QString &settingsKey,
                    const QJsonArray &hosts);



QString validateMirrorActionsConfigurationRequest(
    const MirrorActionsConfigurationRequest &request);




QByteArray buildMirrorActionsConfigurationPayload(
    const MirrorActionsConfigurationRequest &request,
    QString *error = nullptr);





MirrorActionsSshCommand buildMirrorActionsSshCommand(
    const MirrorActionsConfigurationRequest &request,
    const QString &sshPassword,
    QString *error = nullptr,
    const QString &identityFile = QString());



QJsonObject parseMirrorActionsConfigurationResult(
    const QByteArray &output, const QString &expectedRequestId,
    const QString &expectedNodeName, QString *error = nullptr);



QString validateCloudflareBootstrapRequest(
    const CloudflareBootstrapRequest &request,
    bool allowAutomaticTopology = false);





QString findCloudflareBootstrapScript(
    const QString &sourceDir = QString(),
    const QString &applicationDir = QString());




QString findCloudflareWorkerDirectory(
    const QString &sourceDir = QString(),
    const QString &applicationDir = QString());




QString findSiteDeployScript(
    const QString &sourceDir = QString(),
    const QString &applicationDir = QString());


QString findCloudflareTunnelBootstrapScript(
    const QString &sourceDir = QString(),
    const QString &applicationDir = QString());
QString findMirrorGatewayScript(
    const QString &sourceDir = QString(),
    const QString &applicationDir = QString());

QString findMcpServerScript(
    const QString &sourceDir = QString(),
    const QString &applicationDir = QString());


QString findCloudflaredInstallerScript(
    const QString &sourceDir = QString(),
    const QString &applicationDir = QString());





CloudflareBootstrapCommand buildCloudflareTunnelBootstrapCommand(
    const CloudflareBootstrapRequest &request,
    const QString &mirrorHostname,
    const QString &apiToken,
    const QString &scriptPath,
    const QString &pythonProgram,
    const QString &signerProgram,
    const QString &nodePublicKey,
    const QString &gatewayConfigPath,
    const QString &manifestOutputPath,
    const QString &connectorTokenPath);



CloudflareBootstrapCommand buildCloudflareBootstrapCommand(
    const CloudflareBootstrapRequest &request,
    const QString &apiToken,
    const QString &scriptPath,
    const QString &pythonProgram,
    const QString &signerProgram,
    const QString &nodePublicKey);







QString cloudflareApiTokenFromVariables(
    const QMap<QString, QString> &variables);
QString cloudflareAccountIdFromVariables(
    const QMap<QString, QString> &variables);




CloudflareBootstrapCommand buildCloudflareTailCommand(
    const QString &apiToken,
    const QString &accountId,
    const QString &npxProgram);




QJsonObject parseCloudflareBootstrapResult(const QByteArray &output,
                                           QString *error = nullptr);



QString redactProcessOutput(const QString &text,
                            const QStringList &exactSecrets = {});




bool isValidSolanaPublicAddress(const QString &address);


QUrl worldUrlForRelay(const QString &relayUrl);






QUrl worldDevServerUrl(const QString &configured);






QStringList directMirrorRepositoryOwners(const QString &canonicalOwner,
                                         const QString &catalogOwner);




QByteArray mirrorManifestSigningPayload(const QJsonObject &request,
                                        const QString &expectedPublicKey,
                                        QString *error = nullptr);



QString shlexQuote(const QString &value);




QByteArray pythonCanonicalJson(const QJsonValue &value,
                               QString *error = nullptr);



QByteArray catalogV2SigningPayload(QJsonObject normalizedRecord,
                                   QString *error = nullptr);





QJsonObject normalizedCatalogHostTelemetry(const QJsonObject &data);


QByteArray privateReplicaRouteSigningPayload(
    const QString &owner, const QString &repository, const QString &node,
    const QString &opaqueId, const QString &replicaSha256, quint64 keyEpoch,
    bool active, qint64 issuedAtMs, QString *error = nullptr);


QByteArray httpsMirrorRegistrationSigningPayload(
    const QString &node, const QString &baseUrl,
    const QString &publicKey, qint64 issuedAtMs,
    QString *error = nullptr);











QString validateVultrMirrorRequest(const QString &apiKey,
                                   const QString &nodeName);





bool vultrPlanHasIpv4(const QJsonObject &plan);





QJsonObject cheapestVultrPlan(const QJsonArray &plans);




QString vultrPlanRegion(const QJsonObject &plan);



QJsonObject latestVultrDebianOs(const QJsonArray &osList);






bool localBinaryRunsOnVultrMirror(const QString &kernelType,
                                  const QString &cpuArch);




QJsonObject vultrInstanceCreatePayload(const QString &nodeName,
                                       const QString &planId,
                                       const QString &regionId,
                                       int osId,
                                       const QString &sshKeyId);




QString vultrInstanceReadyIp(const QJsonObject &instance);




bool vultrInstanceIsIpv6Only(const QJsonObject &instance);





QString nextMirrorNodeName(const QStringList &existingNames);




QStringList vultrApiKeyVariableNames();



QString vultrApiKeyFromVariables(const QMap<QString, QString> &variables);






bool vultrInstallNeedsLocalBinary(const QString &installOutput);










QString savedHostVultrInstanceId(const QJsonObject &host);






QString vultrInstanceIdForAddress(const QJsonArray &instances,
                                  const QString &address);




QString validateVultrDestroyRequest(const QString &apiKey,
                                    const QString &instanceId);










struct AgentCliCredentials {
    QByteArray claudeCredentials;
    QByteArray codexAuth;




    QMap<QString, QString> env;
};



bool agentCliCredentialsAreEmpty(const AgentCliCredentials &credentials);



QString describeAgentCliCredentials(const AgentCliCredentials &credentials);




QByteArray agentCliEnvFileContents(const QMap<QString, QString> &env);





QByteArray buildAgentCliBootstrapPayload(const AgentCliCredentials &credentials,
                                         QString *error = nullptr);





QString agentCliBootstrapRemoteCommand(bool withCredentials);











QString cloudflareZoneNameFromVariables(const QMap<QString, QString> &variables);




QString vultrMirrorDnsHostname(const QString &nodeName,
                               const QString &zoneName);




QJsonObject vultrMirrorDnsRecordPayload(const QString &hostname,
                                        const QString &ip);



QString cloudflareZoneId(const QJsonArray &zones, const QString &zoneName);




QString cloudflareDnsRecordId(const QJsonArray &records,
                              const QString &hostname,
                              const QString &recordType);









struct CloudflareTokenRequirement {
    QString key;
    QString label;
    QString purpose;
    QStringList groupNames;
    QString scope;



    QString probePath;
    bool required = true;
};



QList<CloudflareTokenRequirement> cloudflareTokenRequirements();



QString cloudflareTokenProbePath(const CloudflareTokenRequirement &requirement,
                                 const QString &accountId,
                                 const QString &zoneId);





bool isPlausibleCloudflareApiToken(const QString &token);



QString cloudflareTokenVerifyStatus(const QJsonObject &verifyResult);
QString cloudflareTokenVerifyId(const QJsonObject &verifyResult);



QStringList cloudflareTokenPermissionGroupNames(const QJsonObject &tokenDetail);


bool cloudflareTokenGrantsRequirement(
    const CloudflareTokenRequirement &requirement,
    const QStringList &grantedGroupNames);





QStringList cloudflareTokenAccountIds(const QJsonObject &tokenDetail);
QString cloudflareTokenUserResourceKey(const QJsonObject &tokenDetail);








QJsonObject cloudflareTokenCreatePayload(
    const QString &tokenName, const QString &accountId, const QString &zoneId,
    const QString &userResourceKey, const QJsonArray &permissionGroupCatalog,
    QString *error = nullptr);


QString cloudflareCreatedTokenValue(const QJsonObject &createResult);





QString updatedEnvAssignment(const QString &contents, const QString &name,
                             const QString &value);




QString siteDeployEnvFilePath(const QString &sourceDir = QString(),
                              const QString &applicationDir = QString());



QString maskedTokenSuffix(const QString &token);

}
