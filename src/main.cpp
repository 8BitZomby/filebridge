#include "MainWindow.hpp"

#include <QApplication>

/**
 * main()
 * Starts FileBridge and displays the main application window.
 */
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    MainWindow window;
    window.show();

    return app.exec();
}
