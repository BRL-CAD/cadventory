#include "CADventory.h"

#include <QDir>
#include <QTimer>
#include <QPixmap>
#include <QString>
#include <QSplashScreen>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDebug>

#include <stdlib.h>
#include <stdlib.h>
#include <iostream>
#include <filesystem>
#include <string>

void myMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QString txt;
    QDateTime dateTime = QDateTime::currentDateTime();
    QString dateTimeString = dateTime.toString("yyyy-MM-dd hh:mm:ss.zzz");

    switch (type) {
    case QtDebugMsg:
        txt = QString("%1 Debug: %2 (%3:%4, %5)\n")
                  .arg(dateTimeString)
                  .arg(msg)
                  .arg(context.file)
                  .arg(context.line)
                  .arg(context.function);
        break;
    case QtInfoMsg:
        txt = QString("%1 Info: %2 (%3:%4, %5)\n")
                  .arg(dateTimeString)
                  .arg(msg)
                  .arg(context.file)
                  .arg(context.line)
                  .arg(context.function);
        break;
    case QtWarningMsg:
        txt = QString("%1 Warning: %2 (%3:%4, %5)\n")
                  .arg(dateTimeString)
                  .arg(msg)
                  .arg(context.file)
                  .arg(context.line)
                  .arg(context.function);
        break;
    case QtCriticalMsg:
        txt = QString("%1 Critical: %2 (%3:%4, %5)\n")
                  .arg(dateTimeString)
                  .arg(msg)
                  .arg(context.file)
                  .arg(context.line)
                  .arg(context.function);
        break;
    case QtFatalMsg:
        txt = QString("%1 Fatal: %2 (%3:%4, %5)\n")
                  .arg(dateTimeString)
                  .arg(msg)
                  .arg(context.file)
                  .arg(context.line)
                  .arg(context.function);
        break;
    }

    QFile outFile("cadventory_debug.log");
    outFile.open(QIODevice::WriteOnly | QIODevice::Append);
    QTextStream ts(&outFile);
    ts << txt;
    outFile.close();

    // Also print to standard error for immediate visibility if running from console
    fprintf(stderr, "%s", txt.toLocal8Bit().constData());
    fflush(stderr);
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>

int APIENTRY
WinMain(HINSTANCE /*hInstance*/,
	HINSTANCE /*hPrevInstance*/,
	LPSTR /*lpszCmdLine*/,
	int /*nCmdShow*/)
{
    int argc = __argc;
    char **argv = __argv;
#else
int
main(int argc, char **argv)
{
#endif
  // Install the custom message handler
  qInstallMessageHandler(myMessageHandler);
  
  CADventory app(argc, argv);
  app.showSplash();

  QTimer::singleShot(250, [&app]() {
    std::string home = QDir::homePath().toStdString();
    const char *homestr = home.c_str();
    if (!homestr)
      homestr = ".";
    app.processEvents();
    std::string localDir = std::string(home);
    app.indexDirectory(localDir.c_str());
  });
  printf("Starting CADventory\n");

  return app.exec();
}
