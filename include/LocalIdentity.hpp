#ifndef FILEBRIDGE_LOCAL_IDENTITY_HPP
#define FILEBRIDGE_LOCAL_IDENTITY_HPP

#include <QSslCertificate>
#include <QSslKey>
#include <QString>


/**
 * LocalIdentity
 * Owns the persistent cryptographic identity used to authenticate this FileBridge installation.
 */
class LocalIdentity {

    public:

        /**
         * LoadResult
         * Describes whether a persistent local identity was loaded, absent, or invalid.
         */
        enum class LoadResult : int {
            Loaded = 0,
            NotFound = 1,
            Failed = 2
        };

        /**
         * load()
         * Loads the existing persistent certificate and private key without creating a new identity.
         */
        LoadResult load(QString *errorMessage = nullptr);

        /**
         * generate()
         * Creates a new in-memory FileBridge identity without persisting it.
         */
        bool generate(QString *errorMessage = nullptr);

        /**
         * persist()
         * Atomically stores the currently loaded FileBridge identity in persistent application data.
         */
        bool persist(QString *errorMessage = nullptr);

        /**
         * isLoaded()
         * Returns whether a valid certificate and private key are currently loaded.
         */
        bool isLoaded() const;

        /**
         * certificate()
         * Returns the certificate associated with this FileBridge installation.
         */
        const QSslCertificate& certificate() const;

        /**
         * privateKey()
         * Returns the private key associated with this FileBridge installation.
         */
        const QSslKey& privateKey() const;

    private:

        // Certificate representing this FileBridge installation during TLS authentication.
        QSslCertificate certificate_;

        // Private key retained only by this FileBridge installation.
        QSslKey privateKey_;
};


#endif
