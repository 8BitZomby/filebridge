#include "MainWindow.hpp"

#include <QApplication>

/**
 * main()
 * Starts FileBridge and displays the main application window.
 */
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // Use a stable application identity for platform-specific persistent FileBridge data.
    QApplication::setOrganizationName("FileBridge");
    QApplication::setApplicationName("FileBridge");

    MainWindow window;
    window.show();

    return app.exec();
}
