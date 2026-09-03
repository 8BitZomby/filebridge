#ifndef FILEBRIDGE_TRANSFER_WIDGET_HPP
#define FILEBRIDGE_TRANSFER_WIDGET_HPP

#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdint>


/**
 * TransferWidget
 * Displays one file transfer with status text and optional progress
 */
class TransferWidget : public QWidget {
    Q_OBJECT

    public:

        /**
         * Constructor: IncomingTransferWidget()
         * Constructs the visual row for one file transfer
         */
        explicit TransferWidget(const QString& fileName, std::uint64_t fileSize, QWidget *parent = nullptr);

        /**
         * setStatus()
         * Updates the visible lifecycle status text for this transfer
         */
        void setStatus(const QString& status);

        /**
         * setProgress()
         * Updates the transfer progress and makes the progress bar visible
         */
        void setProgress(std::uint64_t bytesReceived, std::uint64_t fileSize);

        /**
         * hideProgress()
         * Hides the progress bar for non-receiving transfer states
         */
        void hideProgress();

    private:

        // Displays the filename, file size, and current lifecycle status
        QLabel *statusLabel_;

        // Displays receive progress while file data is actively arriving
        QProgressBar *progressBar_;
};


#endif
