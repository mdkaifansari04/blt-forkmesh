#pragma once

#include <QString>

namespace AccountCapability {

inline QString normalizedAccount(const QString &account)
{
    return account.trimmed().toLower();
}

inline bool allowsPasswordOnlyFallback(const QString &error)
{
    return error == QLatin1String("pubkey_mismatch") ||
           error == QLatin1String("device_proof_required") ||
           error == QLatin1String("device_key_conflict");
}

inline bool ownerSigningAllowed(
    bool activeAccount, bool desktopCapable,
    const QString &sessionAccount = QString(),
    const QString &signerAccount = QString())
{
    if (!activeAccount || !desktopCapable)
        return false;
    const QString session = normalizedAccount(sessionAccount);
    if (session.isEmpty())
        return false;
    if (signerAccount.trimmed().isEmpty())
        return true;
    return session == normalizedAccount(signerAccount);
}

inline bool persistedMarkerMatches(const QString &storedAccount,
                                   const QString &storedPublicKey,
                                   const QString &currentAccount,
                                   const QString &currentPublicKey)
{
    const QString stored = normalizedAccount(storedAccount);
    return !stored.isEmpty() && !storedPublicKey.isEmpty() &&
           stored == normalizedAccount(currentAccount) &&
           storedPublicKey == currentPublicKey;
}

} // namespace AccountCapability
