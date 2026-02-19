#include <KMainWindow>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPen>
#include <QSplitter>
#include <QStatusBar>
#include <Qt>
#include <QToolBar>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QWidget>
#include <cmath>

class GridCanvas : public QWidget {
    Q_OBJECT

public:
    explicit GridCanvas(QWidget *parent = nullptr) : QWidget(parent) {
        setFocusPolicy(Qt::StrongFocus);
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event)

        QPainter painter(this);
        painter.fillRect(rect(), QColor("#0f1115"));

        const int major = qMax(20, static_cast<int>(80.0 * zoomFactor_));
        const int minor = qMax(5, static_cast<int>(20.0 * zoomFactor_));
        const double x0 = width() * 0.5 + panOffset_.x();
        const double y0 = height() * 0.5 + panOffset_.y();

        painter.setRenderHint(QPainter::Antialiasing, false);

        QPen minorPen(QColor(255, 255, 255, 20));
        minorPen.setWidth(1);
        painter.setPen(minorPen);
        int startXMinor = static_cast<int>(std::fmod(x0, minor));
        if (startXMinor < 0) {
            startXMinor += minor;
        }
        int startYMinor = static_cast<int>(std::fmod(y0, minor));
        if (startYMinor < 0) {
            startYMinor += minor;
        }
        for (int x = startXMinor; x < width(); x += minor) {
            painter.drawLine(x, 0, x, height());
        }
        for (int y = startYMinor; y < height(); y += minor) {
            painter.drawLine(0, y, width(), y);
        }

        QPen majorPen(QColor(255, 255, 255, 55));
        majorPen.setWidth(1);
        painter.setPen(majorPen);
        int startXMajor = static_cast<int>(std::fmod(x0, major));
        if (startXMajor < 0) {
            startXMajor += major;
        }
        int startYMajor = static_cast<int>(std::fmod(y0, major));
        if (startYMajor < 0) {
            startYMajor += major;
        }
        for (int x = startXMajor; x < width(); x += major) {
            painter.drawLine(x, 0, x, height());
        }
        for (int y = startYMajor; y < height(); y += major) {
            painter.drawLine(0, y, width(), y);
        }

        QPen axisPen(QColor("#ffb703"));
        axisPen.setWidth(2);
        painter.setPen(axisPen);
        painter.drawLine(0, static_cast<int>(y0), width(), static_cast<int>(y0));
        painter.drawLine(static_cast<int>(x0), 0, static_cast<int>(x0), height());
    }

    void wheelEvent(QWheelEvent *event) override {
        const QPoint angleDelta = event->angleDelta();
        if (angleDelta.y() == 0) {
            event->ignore();
            return;
        }

        const double steps = static_cast<double>(angleDelta.y()) / 120.0;
        zoomFactor_ *= std::pow(1.12, steps);
        zoomFactor_ = qBound(0.25, zoomFactor_, 6.0);
        update();
        event->accept();
    }

    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            dragging_ = true;
            lastDragPos_ = event->position();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        if (dragging_) {
            const QPointF delta = event->position() - lastDragPos_;
            panOffset_ += delta;
            lastDragPos_ = event->position();
            update();
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && dragging_) {
            dragging_ = false;
            unsetCursor();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    double zoomFactor_ = 1.0;
    QPointF panOffset_{0.0, 0.0};
    QPointF lastDragPos_{0.0, 0.0};
    bool dragging_ = false;
};

class MainWindow : public KMainWindow {
    Q_OBJECT

public:
    MainWindow() {
        setWindowTitle("ktikz");
        resize(1200, 800);

        auto *editorBackend = KTextEditor::Editor::instance();
        if (!editorBackend) {
            statusBar()->showMessage("KTextEditor backend unavailable");
            return;
        }

        editorDoc_ = editorBackend->createDocument(this);
        editorView_ = editorDoc_->createView(this);
        editorDoc_->setMode(QStringLiteral("LaTeX"));
        QFont editorFont;
        editorFont.setFamily(QStringLiteral("Monospace"));
        editorFont.setStyleHint(QFont::Monospace);
        editorFont.setPointSize(14);
        editorView_->setConfigValue(QStringLiteral("font"), editorFont);
        editorDoc_->setText(
            "\\documentclass[tikz,border=10pt]{standalone}\n"
            "\\usepackage{tikz}\n"
            "\\begin{document}\n"
            "\\begin{tikzpicture}\n"
            "  \\draw (0,0) -- (2,1);\n"
            "\\end{tikzpicture}\n"
            "\\end{document}\n");

        auto *splitter = new QSplitter(Qt::Horizontal, this);
        splitter->addWidget(editorView_);
        splitter->addWidget(new GridCanvas(this));
        splitter->setSizes({600, 600});

        auto *output = new QPlainTextEdit(this);
        output->setReadOnly(true);
        output->setPlaceholderText("Compilation output will appear here...");

        auto *mainSplitter = new QSplitter(Qt::Vertical, this);
        mainSplitter->addWidget(splitter);
        mainSplitter->addWidget(output);
        mainSplitter->setSizes({650, 150});

        auto *central = new QWidget(this);
        auto *layout = new QVBoxLayout(central);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(mainSplitter);
        setCentralWidget(central);

        createMenuAndToolbar();
        statusBar()->showMessage("Ready");
    }

private:
    void createMenuAndToolbar() {
        auto *fileMenu = menuBar()->addMenu("File");
        auto *buildMenu = menuBar()->addMenu("Build");

        auto *loadAct = new QAction(QIcon::fromTheme("document-open"), "Load", this);
        auto *compileAct = new QAction(QIcon::fromTheme("system-run"), "Compile", this);
        auto *quitAct = new QAction(QIcon::fromTheme("application-exit"), "Quit", this);

        loadAct->setIconVisibleInMenu(true);
        compileAct->setIconVisibleInMenu(true);
        quitAct->setIconVisibleInMenu(true);

        loadAct->setShortcut(QKeySequence("Ctrl+O"));
        compileAct->setShortcut(QKeySequence("F5"));
        quitAct->setShortcut(QKeySequence::Quit);

        connect(quitAct, &QAction::triggered, this, &QWidget::close);

        fileMenu->addAction(loadAct);
        fileMenu->addSeparator();
        fileMenu->addAction(quitAct);
        buildMenu->addAction(compileAct);

        auto *toolbar = addToolBar("Main");
        toolbar->setMovable(false);
        toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toolbar->addAction(loadAct);
        toolbar->addAction(compileAct);
    }

    KTextEditor::Document *editorDoc_ = nullptr;
    KTextEditor::View *editorView_ = nullptr;
};

#include "main.moc"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    return app.exec();
}
