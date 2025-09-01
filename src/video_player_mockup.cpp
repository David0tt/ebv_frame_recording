#include <QApplication>
#include <QWidget>
#include <QGridLayout>
#include <QLabel>
#include <QFrame>
#include <QGroupBox>
#include <QSlider>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStyle>
#include <QTimer>
#include <QSpacerItem>
#include <QSizePolicy>
#include <QString>
#include <QCommandLineParser>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>

/*
 Mockup video player UI layout (no real data connection yet):

 +-----------------------------------------------------------+
 |  Frame Cam Left        |  Frame Cam Right                |
 |  (top-left)            |  (top-right)                    |
 |                        |                                 |
 |-----------------------------------------------------------|
 |  Event Cam Left        |  Event Cam Right                |
 |  (bottom-left)         |  (bottom-right)                 |
 +-----------------------------------------------------------+
 | [Timeline Slider.......................................] |
 | [ << ] [ Play/Pause ] [ >> ]   (future: timestamp etc)   |
 +-----------------------------------------------------------+

 Each quadrant is currently a QFrame containing a QLabel placeholder.
*/

struct Pane {
    QFrame *frame {nullptr};
    QLabel *content {nullptr};
};

static Pane createPane(const QString &title, const QColor &color) {
    Pane p;
    p.frame = new QFrame;
    p.frame->setFrameShape(QFrame::StyledPanel);
    p.frame->setLineWidth(1);
    p.frame->setAutoFillBackground(true);
    QPalette pal = p.frame->palette();
    QColor bg = color.lighter(170);
    bg.setAlpha(40);
    pal.setColor(QPalette::Window, bg);
    p.frame->setPalette(pal);

    auto *layout = new QVBoxLayout(p.frame);
    auto *labelTitle = new QLabel("<b>" + title + "</b>");
    p.content = new QLabel("(image/event view placeholder)");
    p.content->setAlignment(Qt::AlignCenter);
    layout->addWidget(labelTitle);
    layout->addWidget(p.content, 1);
    return p;
}

class PlayerWindow : public QWidget {
    Q_OBJECT
public:
    explicit PlayerWindow(QWidget *parent=nullptr) : QWidget(parent) {
        setWindowTitle("EBV Multi-Camera Player Mockup");
        resize(1400, 900);

        auto *rootLayout = new QVBoxLayout(this);

        // Top bar with Open button + path label
        auto *topBar = new QHBoxLayout();
        m_openButton = new QPushButton(tr("Open Folder…"));
        m_pathLabel = new QLabel(tr("No folder loaded"));
        m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        topBar->addWidget(m_openButton);
        topBar->addSpacing(12);
        topBar->addWidget(m_pathLabel, 1);
        rootLayout->addLayout(topBar);

        // Grid
        auto *grid = new QGridLayout();
        grid->setSpacing(4);
        auto frameLeft  = createPane("Frame Camera Left", QColor(70,120,200));
        auto frameRight = createPane("Frame Camera Right", QColor(70,120,200));
        auto eventLeft  = createPane("Event Camera Left", QColor(200,140,70));
        auto eventRight = createPane("Event Camera Right", QColor(200,140,70));
        m_panes = {frameLeft, frameRight, eventLeft, eventRight};
        grid->addWidget(frameLeft.frame, 0, 0);
        grid->addWidget(frameRight.frame, 0, 1);
        grid->addWidget(eventLeft.frame, 1, 0);
        grid->addWidget(eventRight.frame, 1, 1);
        grid->setColumnStretch(0, 1);
        grid->setColumnStretch(1, 1);
        grid->setRowStretch(0, 1);
        grid->setRowStretch(1, 1);
        rootLayout->addLayout(grid, 1);

        // Timeline slider
        m_timelineSlider = new QSlider(Qt::Horizontal);
        m_timelineSlider->setRange(0, 1000);
        m_timelineSlider->setSingleStep(1);
        m_timelineSlider->setPageStep(25);
        rootLayout->addWidget(m_timelineSlider);

        // Transport controls
        auto *controlsLayout = new QHBoxLayout();
        m_btnBack = new QPushButton("<<");
        m_btnPlay = new QPushButton("Play");
        m_btnFwd = new QPushButton(">>");
        controlsLayout->addSpacerItem(new QSpacerItem(20, 10, QSizePolicy::Expanding, QSizePolicy::Minimum));
        controlsLayout->addWidget(m_btnBack);
        controlsLayout->addWidget(m_btnPlay);
        controlsLayout->addWidget(m_btnFwd);
        controlsLayout->addSpacerItem(new QSpacerItem(20, 10, QSizePolicy::Expanding, QSizePolicy::Minimum));
        rootLayout->addLayout(controlsLayout);

        // Timer for mock playback
        m_timer.setInterval(30);
        connect(&m_timer, &QTimer::timeout, this, [this]{
            int v = m_timelineSlider->value();
            if (v < m_timelineSlider->maximum()) {
                m_timelineSlider->setValue(v + 1);
            } else {
                m_timer.stop();
                m_btnPlay->setText("Play");
            }
        });

        connect(m_btnPlay, &QPushButton::clicked, this, [this]{
            if (m_timer.isActive()) { m_timer.stop(); m_btnPlay->setText("Play"); }
            else { m_timer.start(); m_btnPlay->setText("Pause"); }
        });
        connect(m_btnBack, &QPushButton::clicked, this, [this]{
            int v = m_timelineSlider->value();
            m_timelineSlider->setValue(std::max(0, v - 50));
        });
        connect(m_btnFwd, &QPushButton::clicked, this, [this]{
            int v = m_timelineSlider->value();
            m_timelineSlider->setValue(std::min(m_timelineSlider->maximum(), v + 50));
        });

        connect(m_openButton, &QPushButton::clicked, this, [this]{ selectAndLoadFolder(); });
    }

    void selectAndLoadFolder() {
        QString dir = QFileDialog::getExistingDirectory(this, tr("Select Recording Folder"), QDir::homePath());
        if (!dir.isEmpty()) {
            loadRecording(dir);
        }
    }

    void loadRecording(const QString &dirPath) {
        QDir dir(dirPath);
        if (!dir.exists()) {
            QMessageBox::warning(this, tr("Folder Missing"), tr("Directory does not exist:\n%1").arg(dirPath));
            return;
        }
        m_loadedDir = dirPath;
        m_pathLabel->setText(tr("Loaded: %1").arg(dirPath));
        // Update pane placeholder content to reflect loaded directory (mock behavior)
        for (auto &p : m_panes) {
            p.content->setText(tr("Loaded folder:\n%1\n(placeholder view)").arg(dir.dirName()));
        }
        // Potential future: scan for number of frames/events and set slider maximum accordingly
    }

    void autoLoadIfProvided(const QString &dirPath) {
        if (!dirPath.isEmpty()) {
            loadRecording(dirPath);
        }
    }

private:
    QPushButton *m_openButton {nullptr};
    QLabel *m_pathLabel {nullptr};
    QSlider *m_timelineSlider {nullptr};
    QPushButton *m_btnBack {nullptr};
    QPushButton *m_btnPlay {nullptr};
    QPushButton *m_btnFwd {nullptr};
    QTimer m_timer;
    QString m_loadedDir;
    std::vector<Pane> m_panes;
};

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

#include "video_player_mockup.moc"
