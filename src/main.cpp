#include "mainwindow.h"
#include "remoteapiclient.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("ChattingApp");
    app.setOrganizationName("TreeX");
    app.setStyle("Fusion");

    RemoteApiClient apiClient;
    MainWindow window(&apiClient);
    window.show();

    return app.exec();
}
