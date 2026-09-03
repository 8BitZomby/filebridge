#include "ApplicationPaths.hpp"

#include <QDir>
#include <QStandardPaths>


/**
 * dataDirectory()
 * Returns the root directory used for persistent FileBridge application data.
 */
QString ApplicationPaths::dataDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}


/**
 * identityDirectory()
 * Returns the directory reserved for this FileBridge installation's cryptographic identity.
 */
QString ApplicationPaths::identityDirectory() {
    return QDir(dataDirectory()).filePath("identity");
}


/**
 * trustDirectory()
 * Returns the directory reserved for persistent trusted-peer information.
 */
QString ApplicationPaths::trustDirectory() {
    return QDir(dataDirectory()).filePath("trust");
}
