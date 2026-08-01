#pragma once

#include <QString>

#include <functional>










namespace forkmesh {






bool acquireSingleInstance(const QString &activationTarget = QString());




void onSingleInstanceActivation(
    std::function<void(const QString &activationTarget)> handler);






void releaseSingleInstance();

}
