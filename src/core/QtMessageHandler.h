#pragma once

#include <QMessageLogContext>
#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <QDateTime>
#include <cstdio>

const QString DEFAULT_LOG_NAME("cadventory.log");

class MessageHandler {
public:
    // singleton
    static MessageHandler& instance() { static MessageHandler inst; return inst; }

    void init(const QString& _logPath = DEFAULT_LOG_NAME) {
        logPath = _logPath;
        qInstallMessageHandler(&MessageHandler::qtHandler);
    }

    //  -v  -> qInfo()
    //  -vv -> + qDebug()
    void applyVerbosityFlags(const QStringList& args) {
        int level = 0;
        for (const QString& a : args) {
            if (a == QLatin1String("-v"))
                level = std::max(level, 1);
            else if (a == QLatin1String("-vv"))
                level = std::max(level, 2);
        }

        verbosity_level = level;
    }

private:
    MessageHandler() = default;

    static void qtHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
        // filter verbosity level
        if ((type == QtInfoMsg  && instance().verbosity_level < 1) ||
            (type == QtDebugMsg && instance().verbosity_level < 2)) { 
            return;
        }

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

        QFile outFile(instance().logPath);
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

    int verbosity_level = 0;            // 0: warn, 1= +info, 2= +debug
    QString logPath = DEFAULT_LOG_NAME;
};

/* install the handler. NOTE: this MUST be called before constructing the application */
inline void initLogging()
{
    MessageHandler::instance().init();
}