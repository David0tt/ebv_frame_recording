#include <QApplication>
#include <QCommandLineParser>
#include "player_window.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription("EBV Multi-Camera Player Mockup");
    parser.addHelpOption();
    parser.addPositionalArgument("recording_dir", "Optional path to a recording directory to load on startup.");
    parser.process(app);
    const QString recordingDir = parser.positionalArguments().isEmpty() ? QString() : parser.positionalArguments().first();

    PlayerWindow w;
    w.show();
    w.autoLoadIfProvided(recordingDir);
    return app.exec();
}
