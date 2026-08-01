#pragma once

#include <QString>

namespace forkmesh {






bool isPlatformSizeHintNoise(const QString &message);









bool isFontDatabaseNoise(const QString &message);





void installPlatformLogFilter();

}
