#ifndef FILEBRIDGE_APPLICATION_PATHS_HPP
#define FILEBRIDGE_APPLICATION_PATHS_HPP

#include <QString>


/**
 * ApplicationPaths
 * Provides stable platform-appropriate locations for FileBridge's persistent application data.
 */
namespace ApplicationPaths {

    /**
     * dataDirectory()
     * Returns the root directory used for persistent FileBridge application data.
     */
    QString dataDirectory();

    /**
     * identityDirectory()
     * Returns the directory reserved for this FileBridge installation's cryptographic identity.
     */
    QString identityDirectory();

    /**
     * trustDirectory()
     * Returns the directory reserved for persistent trusted-peer information.
     */
    QString trustDirectory();
}


#endif
