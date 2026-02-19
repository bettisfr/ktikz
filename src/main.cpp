#include <KMainWindow>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QIcon>
#include <QImage>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPdfDocument>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSplitter>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextStream>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

#include <cmath>
#include <memory>

class PdfCanvas : public QWidget {
    Q_OBJECT

public:
    explicit PdfCanvas(QWidget *parent = nullptr) : QWidget(parent) {
        setFocusPolicy(Qt::StrongFocus);
    }

    bool loadPdf(const QString &pdfPath) {
        renderedImage_ = QImage();
        renderedSize_ = QSize();
        const QPdfDocument::Error err = pdfDocument_.load(pdfPath);
        update();
        return err == QPdfDocument::Error::None;
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event)

        QPainter painter(this);
        painter.fillRect(rect(), QColor("#ffffff"));

        if (pdfDocument_.status() != QPdfDocument::Status::Ready || pdfDocument_.pageCount() <= 0) {
            painter.setPen(QColor("#666666"));
            painter.drawText(rect(), Qt::AlignCenter, "Compile to preview output");
            return;
        }

        const QSizeF pageSize = pdfDocument_.pagePointSize(0);
        if (pageSize.width() <= 0 || pageSize.height() <= 0) {
            return;
        }

        const double fit = 0.95 * qMin(width() / pageSize.width(), height() / pageSize.height());
        const double scale = fit * viewScale_;
        const int w = qMax(1, static_cast<int>(pageSize.width() * scale));
        const int h = qMax(1, static_cast<int>(pageSize.height() * scale));
        const QRect targetRect(static_cast<int>((width() - w) * 0.5 + panOffset_.x()),
                               static_cast<int>((height() - h) * 0.5 + panOffset_.y()),
                               w,
                               h);

        if (renderedImage_.isNull() || renderedSize_ != targetRect.size()) {
            renderedImage_ = pdfDocument_.render(0, targetRect.size());
            renderedSize_ = targetRect.size();
        }

        if (!renderedImage_.isNull()) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(targetRect, renderedImage_);
        }
    }

    void wheelEvent(QWheelEvent *event) override {
        const QPoint delta = event->angleDelta();
        if (delta.y() == 0) {
            event->ignore();
            return;
        }

        const double steps = static_cast<double>(delta.y()) / 120.0;
        viewScale_ *= std::pow(1.12, steps);
        viewScale_ = qBound(0.2, viewScale_, 12.0);
        renderedImage_ = QImage();
        renderedSize_ = QSize();
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
        if (!dragging_) {
            QWidget::mouseMoveEvent(event);
            return;
        }
        const QPointF delta = event->position() - lastDragPos_;
        panOffset_ += delta;
        lastDragPos_ = event->position();
        update();
        event->accept();
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
    QPdfDocument pdfDocument_;
    QImage renderedImage_;
    QSize renderedSize_;
    double viewScale_ = 1.0;
    QPointF panOffset_{0.0, 0.0};
    bool dragging_ = false;
    QPointF lastDragPos_{0.0, 0.0};
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

        previewCanvas_ = new PdfCanvas(this);

        auto *splitter = new QSplitter(Qt::Horizontal, this);
        splitter->addWidget(editorView_);
        splitter->addWidget(previewCanvas_);
        splitter->setSizes({600, 600});

        output_ = new QPlainTextEdit(this);
        output_->setReadOnly(true);
        output_->setPlaceholderText("Compilation output will appear here...");

        auto *mainSplitter = new QSplitter(Qt::Vertical, this);
        mainSplitter->addWidget(splitter);
        mainSplitter->addWidget(output_);
        mainSplitter->setSizes({650, 150});

        auto *central = new QWidget(this);
        auto *layout = new QVBoxLayout(central);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(mainSplitter);
        setCentralWidget(central);

        compileProc_ = new QProcess(this);
        connect(compileProc_, &QProcess::readyReadStandardOutput, this, &MainWindow::onCompileOutput);
        connect(compileProc_, &QProcess::readyReadStandardError, this, &MainWindow::onCompileOutput);
        connect(compileProc_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                this, &MainWindow::onCompileFinished);

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

        connect(loadAct, &QAction::triggered, this, &MainWindow::loadFile);
        connect(compileAct, &QAction::triggered, this, &MainWindow::compile);
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

    void loadFile() {
        const QString path = QFileDialog::getOpenFileName(
            this,
            "Load TikZ/LaTeX File",
            QDir::homePath(),
            "TeX files (*.tex *.tikz);;All files (*)");
        if (path.isEmpty()) {
            return;
        }

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            statusBar()->showMessage("Load failed", 3000);
            return;
        }

        QTextStream in(&file);
        editorDoc_->setText(in.readAll());
        currentFilePath_ = path;
        statusBar()->showMessage("Loaded " + QFileInfo(path).fileName(), 3000);
    }

    bool ensureWorkDir() {
        if (!workDirPath_.isEmpty()) {
            return true;
        }
        workTempDir_ = std::make_unique<QTemporaryDir>();
        if (!workTempDir_->isValid()) {
            return false;
        }
        workDirPath_ = workTempDir_->path();
        return true;
    }

    static QString withInjectedGrid(const QString &source) {
        static const QRegularExpression beginTikzPattern(R"(\\begin\{tikzpicture\}(?:\[[^\]]*\])?)");
        const QRegularExpressionMatch m = beginTikzPattern.match(source);
        if (!m.hasMatch()) {
            return source;
        }

        const QString gridBlock =
            "\n  % ktikz preview grid\n"
            "  \\draw[step=1, gray!35, very thin] (-10,-10) grid (10,10);\n"
            "  \\draw[gray!60, thin] (-10,0) -- (10,0);\n"
            "  \\draw[gray!60, thin] (0,-10) -- (0,10);\n";

        QString out = source;
        out.insert(m.capturedEnd(0), gridBlock);
        return out;
    }

    void compile() {
        if (compileProc_->state() != QProcess::NotRunning) {
            output_->appendPlainText("[Compile] Already running");
            return;
        }

        if (!ensureWorkDir()) {
            output_->appendPlainText("[Compile] Could not create temporary directory");
            return;
        }

        const QString compileSource = withInjectedGrid(editorDoc_->text());

        QFile texFile(workDirPath_ + "/document.tex");
        if (!texFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            output_->appendPlainText("[Compile] Could not write document.tex");
            return;
        }
        QTextStream out(&texFile);
        out << compileSource;
        texFile.close();

        output_->appendPlainText("\n[Compile] " + QDateTime::currentDateTime().toString(Qt::ISODate));
        output_->appendPlainText("[Compile] Running pdflatex...");

        compileProc_->setWorkingDirectory(workDirPath_);
        compileProc_->setProcessEnvironment(QProcessEnvironment::systemEnvironment());
        compileProc_->start("pdflatex",
                            QStringList() << "-interaction=nonstopmode"
                                          << "-halt-on-error"
                                          << "-file-line-error"
                                          << "document.tex");
        if (!compileProc_->waitForStarted(1500)) {
            output_->appendPlainText("[Error] Unable to start pdflatex");
            return;
        }

        statusBar()->showMessage("Compiling...");
    }

    void onCompileOutput() {
        output_->moveCursor(QTextCursor::End);
        const QByteArray stdOut = compileProc_->readAllStandardOutput();
        const QByteArray stdErr = compileProc_->readAllStandardError();
        if (!stdOut.isEmpty()) {
            output_->insertPlainText(QString::fromLocal8Bit(stdOut));
        }
        if (!stdErr.isEmpty()) {
            output_->insertPlainText(QString::fromLocal8Bit(stdErr));
        }
        output_->moveCursor(QTextCursor::End);
    }

    void onCompileFinished(int exitCode, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || exitCode != 0) {
            output_->appendPlainText("[Compile] Failed");
            statusBar()->showMessage("Compile failed", 3000);
            return;
        }

        const QString pdfPath = workDirPath_ + "/document.pdf";
        if (!previewCanvas_->loadPdf(pdfPath)) {
            output_->appendPlainText("[Preview] Failed to load generated PDF");
            statusBar()->showMessage("Preview load failed", 3000);
            return;
        }

        output_->appendPlainText("[Preview] PDF updated (with injected grid)");
        statusBar()->showMessage("Compile successful", 2500);
    }

    KTextEditor::Document *editorDoc_ = nullptr;
    KTextEditor::View *editorView_ = nullptr;
    PdfCanvas *previewCanvas_ = nullptr;
    QPlainTextEdit *output_ = nullptr;
    QProcess *compileProc_ = nullptr;

    QString currentFilePath_;
    QString workDirPath_;
    std::unique_ptr<QTemporaryDir> workTempDir_;
};

#include "main.moc"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    return app.exec();
}
