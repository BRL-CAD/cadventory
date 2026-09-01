#include <QCoreApplication>
#include <QTextStream>
#include <QThread>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments().mid(1);

    if (arguments.size() == 2 && arguments.front() == "--sleep") {
        QThread::msleep(arguments.back().toULong());
        return 0;
    }

    if (arguments.size() == 2 && arguments.front() == "--exit")
        return arguments.back().toInt();

    if (arguments.size() >= 3 && arguments.front() == "-c") {
        QTextStream output(stdout);
        if (arguments.back() == "title") {
            output << "Fixture title\n";
            return 0;
        }
        if (arguments.value(2) == "search") {
            output << "fixture.r\n";
            return 0;
        }
        return 2;
    }

    QTextStream output(stdout);
    for (const auto& argument : arguments)
        output << argument << '\n';
    return 0;
}
