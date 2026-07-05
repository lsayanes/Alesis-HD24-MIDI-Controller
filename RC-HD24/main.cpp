#include <QApplication>
#include <QStyleFactory>
#include <QIcon>
#include <QFile>
#include <string>
#include <iostream>

#include "RCHD24.h"


int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const char *appName =  "RC-HD24";

    app.setApplicationName(appName);
    app.setApplicationVersion("1.0");
    app.setOrganizationName("iDev - JalaGames");

    app.setStyle(QStyleFactory::create("Fusion"));

    // El ícono se empaqueta vía qrc (chordy.qrc) y funciona en cualquier
    // plataforma (desktop y Android) sin asumir un layout de filesystem.
    const QString iconPath = QStringLiteral(":/resources/rc-hd24.png");
    if (QFile::exists(iconPath))
        app.setWindowIcon(QIcon(iconPath));

    RCHD24 App;
    bool bCreated = App.create(appName);

    if(bCreated)
    {
#ifdef Q_OS_ANDROID
        // En Android la app no es una "ventana": tiene que ocupar toda la pantalla
        // del device para que el grid use todo el alto disponible.
        App.showMaximized();
#else
        App.show();
#endif
        return app.exec();
    }

    return -1;
}
