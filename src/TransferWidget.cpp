#include "TransferWidget.hpp"

#include <QString>


/**
 * Constructor: IncomingTransferWidget()
 * Constructs the visual row for one file transfer
 */
TransferWidget::TransferWidget(const QString& fileName, std::uint64_t fileSize, QWidget *parent) 
      : QWidget(parent),
        statusLabel_(new QLabel(this)),
        progressBar_(new QProgressBar(this)) {

    QVBoxLayout *layout = new QVBoxLayout(this);

    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(2);

    // Start with the transfer in a simple pending state.
    statusLabel_->setText(
        fileName +
        "    " +
        QString::number(fileSize) +
        " bytes    Pending"
    );

    progressBar_->setRange(0, 100);
    progressBar_->setTextVisible(true);
    progressBar_->hide();

    layout->addWidget(statusLabel_);
    layout->addWidget(progressBar_);

    // Let QListWidget continue handling row selection and mouse input.
    setAttribute(Qt::WA_TransparentForMouseEvents);
}


/**
 * setStatus()
 * Updates the visible lifecycle status text for this transfer
 */
void TransferWidget::setStatus(const QString& status) {
    QString currentText = statusLabel_->text();

    const int separatorIndex = currentText.lastIndexOf("    ");

    if(separatorIndex >= 0) {
        currentText = currentText.left(separatorIndex);
    }

    statusLabel_->setText(
        currentText +
        "    " +
        status
    );
}


/**
 * setProgress()
 * Updates the transfer progress and makes the progress bar visible
 */
void TransferWidget::setProgress(std::uint64_t bytesReceived, std::uint64_t fileSize) {

    int progressPercent = 100;

    if(fileSize > 0) {
        progressPercent = static_cast<int>(
            (static_cast<double>(bytesReceived) /
             static_cast<double>(fileSize)) *
            100.0
        );
    }

    progressBar_->setValue(progressPercent);
    progressBar_->show();
}


/**
 * hideProgress()
 * Hides the progress bar for non-receiving transfer states
 */
void TransferWidget::hideProgress() {
    progressBar_->hide();
}
