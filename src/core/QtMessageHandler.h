#pragma once

#include <QMessageLogContext>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <cstdio>

inline void cadventoryMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
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
    if (outFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
        QTextStream ts(&outFile);
        ts << txt;
        outFile.close();
    }

    // Also print to standard error for immediate visibility if running from console
    fprintf(stderr, "%s", txt.toLocal8Bit().constData());
    fflush(stderr);

    // if we get a fatal message, make sure we abort
    if (type == QtFatalMsg)
        std::abort();
}


/* install the handler. NOTE: this MUST be called before constructing the application */
inline void initLogging()
{
    qInstallMessageHandler(cadventoryMessageHandler);
}