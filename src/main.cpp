#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScrollArea>
#include <QTextCursor>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTextStream>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>
#include <memory>

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow() {
        setWindowTitle("ktikz");
        resize(1400, 900);

        editor_ = new QPlainTextEdit(this);
        editor_->setPlainText(defaultDocument());

        previewLabel_ = new QLabel(this);
        previewLabel_->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        previewLabel_->setText("Compile to preview output");

        auto *previewScroll = new QScrollArea(this);
        previewScroll->setWidgetResizable(true);
        previewScroll->setWidget(previewLabel_);

        output_ = new QPlainTextEdit(this);
        output_->setReadOnly(true);
        output_->setMaximumBlockCount(3000);

        auto *topSplitter = new QSplitter(Qt::Horizontal, this);
        topSplitter->addWidget(editor_);
        topSplitter->addWidget(previewScroll);
        topSplitter->setSizes({700, 700});

        auto *mainSplitter = new QSplitter(Qt::Vertical, this);
        mainSplitter->addWidget(topSplitter);
        mainSplitter->addWidget(output_);
        mainSplitter->setSizes({700, 200});

        auto *central = new QWidget(this);
        auto *layout = new QVBoxLayout(central);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(mainSplitter);
        setCentralWidget(central);

        createActions();

        compileProc_ = new QProcess(this);
        connect(compileProc_, &QProcess::readyReadStandardOutput, this, &MainWindow::onCompileOutput);
        connect(compileProc_, &QProcess::readyReadStandardError, this, &MainWindow::onCompileOutput);
        connect(compileProc_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                this, &MainWindow::onCompileFinished);

        convertProc_ = new QProcess(this);
        connect(convertProc_, &QProcess::readyReadStandardOutput, this, &MainWindow::onConvertOutput);
        connect(convertProc_, &QProcess::readyReadStandardError, this, &MainWindow::onConvertOutput);
        connect(convertProc_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                this, &MainWindow::onConvertFinished);

        statusBar()->showMessage("Ready");
    }

private slots:
    void loadFile() {
        const QString path = QFileDialog::getOpenFileName(
            this,
            "Load TikZ/LaTeX File",
            QDir::homePath(),
            "TeX files (*.tex *.tikz);;All files (*)"
        );
        if (path.isEmpty()) {
            return;
        }

        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::warning(this, "Load failed", "Cannot open file: " + path);
            return;
        }

        QTextStream in(&f);
        editor_->setPlainText(in.readAll());
        currentFilePath_ = path;
        output_->appendPlainText("[Load] " + path);
        statusBar()->showMessage("Loaded " + QFileInfo(path).fileName(), 3000);
    }

    void compile() {
        if (compileProc_->state() != QProcess::NotRunning || convertProc_->state() != QProcess::NotRunning) {
            output_->appendPlainText("[Compile] Already running");
            return;
        }

        if (!ensureWorkDir()) {
            QMessageBox::critical(this, "Compile failed", "Could not create temporary working directory");
            return;
        }

        const QString texPath = workDirPath_ + "/document.tex";
        QFile f(texPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            QMessageBox::critical(this, "Compile failed", "Could not write temporary TeX file");
            return;
        }

        QTextStream out(&f);
        out << editor_->toPlainText();
        f.close();

        output_->appendPlainText("\n[Compile] " + QDateTime::currentDateTime().toString(Qt::ISODate));
        output_->appendPlainText("[Compile] Running pdflatex...");

        QStringList args;
        args << "-interaction=nonstopmode"
             << "-halt-on-error"
             << "-file-line-error"
             << "document.tex";

        compileProc_->setWorkingDirectory(workDirPath_);
        compileProc_->setProcessEnvironment(QProcessEnvironment::systemEnvironment());
        compileProc_->start("pdflatex", args);

        if (!compileProc_->waitForStarted(1500)) {
            output_->appendPlainText("[Error] Unable to start pdflatex. Is it installed and in PATH?");
            statusBar()->showMessage("Compile failed", 3000);
            return;
        }

        statusBar()->showMessage("Compiling...");
    }

    void onCompileOutput() {
        output_->moveCursor(QTextCursor::End);
        const QByteArray out = compileProc_->readAllStandardOutput();
        const QByteArray err = compileProc_->readAllStandardError();
        if (!out.isEmpty()) {
            output_->insertPlainText(QString::fromLocal8Bit(out));
        }
        if (!err.isEmpty()) {
            output_->insertPlainText(QString::fromLocal8Bit(err));
        }
        output_->moveCursor(QTextCursor::End);
    }

    void onCompileFinished(int exitCode, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || exitCode != 0) {
            output_->appendPlainText("[Compile] Failed");
            statusBar()->showMessage("Compile failed", 3000);
            return;
        }

        output_->appendPlainText("[Compile] OK. Converting first PDF page to PNG for preview...");

        QStringList args;
        args << "-f" << "1"
             << "-singlefile"
             << "-png"
             << "document.pdf"
             << "preview";

        convertProc_->setWorkingDirectory(workDirPath_);
        convertProc_->setProcessEnvironment(QProcessEnvironment::systemEnvironment());
        convertProc_->start("pdftoppm", args);

        if (!convertProc_->waitForStarted(1500)) {
            output_->appendPlainText("[Error] pdftoppm not found. Install poppler-utils for image preview.");
            statusBar()->showMessage("Compiled, but no preview converter", 4000);
        }
    }

    void onConvertOutput() {
        output_->moveCursor(QTextCursor::End);
        const QByteArray out = convertProc_->readAllStandardOutput();
        const QByteArray err = convertProc_->readAllStandardError();
        if (!out.isEmpty()) {
            output_->insertPlainText(QString::fromLocal8Bit(out));
        }
        if (!err.isEmpty()) {
            output_->insertPlainText(QString::fromLocal8Bit(err));
        }
        output_->moveCursor(QTextCursor::End);
    }

    void onConvertFinished(int exitCode, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || exitCode != 0) {
            output_->appendPlainText("[Preview] Conversion failed");
            statusBar()->showMessage("Preview conversion failed", 3000);
            return;
        }

        const QString imagePath = workDirPath_ + "/preview.png";
        QPixmap pix(imagePath);
        if (pix.isNull()) {
            output_->appendPlainText("[Preview] Could not load generated preview image");
            statusBar()->showMessage("Preview load failed", 3000);
            return;
        }

        previewLabel_->setPixmap(pix);
        previewLabel_->adjustSize();
        output_->appendPlainText("[Preview] Updated");
        statusBar()->showMessage("Compile successful", 2500);
    }

private:
    void createActions() {
        auto *fileMenu = menuBar()->addMenu("File");
        auto *buildMenu = menuBar()->addMenu("Build");

        auto *loadAct = new QAction("Load", this);
        auto *compileAct = new QAction("Compile", this);

        loadAct->setShortcut(QKeySequence("Ctrl+O"));
        compileAct->setShortcut(QKeySequence("F5"));

        connect(loadAct, &QAction::triggered, this, &MainWindow::loadFile);
        connect(compileAct, &QAction::triggered, this, &MainWindow::compile);

        fileMenu->addAction(loadAct);
        buildMenu->addAction(compileAct);

        auto *tb = addToolBar("Main");
        tb->setMovable(false);
        tb->addAction(loadAct);
        tb->addAction(compileAct);
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

    QString defaultDocument() const {
        return R"(\documentclass[tikz,border=10pt]{standalone}
\usepackage{tikz}
\begin{document}
\begin{tikzpicture}
  \draw[thick, blue] (0,0) circle (1.2);
  \fill[red] (0,0) circle (2pt);
  \node at (0,-1.7) {Hello TikZ};
\end{tikzpicture}
\end{document}
)";
    }

    QPlainTextEdit *editor_ = nullptr;
    QLabel *previewLabel_ = nullptr;
    QPlainTextEdit *output_ = nullptr;

    QProcess *compileProc_ = nullptr;
    QProcess *convertProc_ = nullptr;

    QString currentFilePath_;
    QString workDirPath_;
    std::unique_ptr<QTemporaryDir> workTempDir_;
};

#include "main.moc"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    return app.exec();
}
