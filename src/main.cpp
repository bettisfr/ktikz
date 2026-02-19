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
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPdfDocument>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QComboBox>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextStream>
#include <QToolBar>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

#include <cmath>
#include <memory>
#include <vector>

struct CoordPair {
    double x = 0.0;
    double y = 0.0;
};

struct CoordRef {
    int start = 0;
    int end = 0;
    double x = 0.0;
    double y = 0.0;
};

class PdfCanvas : public QWidget {
    Q_OBJECT

public:
    explicit PdfCanvas(QWidget *parent = nullptr) : QWidget(parent) {
        setFocusPolicy(Qt::StrongFocus);
    }

    void setCoordinates(const std::vector<CoordPair> &coords) {
        coordinates_ = coords;
        update();
    }

    void setSnapMm(int mm) {
        snapMm_ = qMax(0, mm);
    }

signals:
    void coordinateDragged(int index, double x, double y);
    void calibrationDebug(const QString &line);

public:
    bool loadPdf(const QString &pdfPath) {
        renderedImage_ = QImage();
        renderedSize_ = QSize();
        calibrationDebugPending_ = true;
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

        updateCalibration(targetRect);
        drawCoordinateMarkers(painter);
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
            if (calibrationValid_) {
                const int idx = hitTestMarker(event->position());
                if (idx >= 0) {
                    markerDragging_ = true;
                    activeMarkerIndex_ = idx;
                    setCursor(Qt::CrossCursor);
                    event->accept();
                    return;
                }
            }
            dragging_ = true;
            lastDragPos_ = event->position();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        if (markerDragging_ && activeMarkerIndex_ >= 0 && activeMarkerIndex_ < static_cast<int>(coordinates_.size())) {
            QPointF world;
            if (screenToWorld(event->position(), world)) {
                if (snapMm_ > 0) {
                    const double step = static_cast<double>(snapMm_) / 10.0; // 10mm == 1cm == 1 TikZ unit
                    world.setX(std::round(world.x() / step) * step);
                    world.setY(std::round(world.y() / step) * step);
                }
                coordinates_[activeMarkerIndex_].x = world.x();
                coordinates_[activeMarkerIndex_].y = world.y();
                update();
            }
            event->accept();
            return;
        }

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
        if (event->button() == Qt::LeftButton && markerDragging_) {
            markerDragging_ = false;
            unsetCursor();
            if (activeMarkerIndex_ >= 0 && activeMarkerIndex_ < static_cast<int>(coordinates_.size())) {
                const CoordPair &c = coordinates_[activeMarkerIndex_];
                emit coordinateDragged(activeMarkerIndex_, c.x, c.y);
            }
            activeMarkerIndex_ = -1;
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton && dragging_) {
            dragging_ = false;
            unsetCursor();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    static bool isNearColor(int r, int g, int b, int tr, int tg, int tb, int maxDistSq) {
        const int dr = r - tr;
        const int dg = g - tg;
        const int db = b - tb;
        return (dr * dr + dg * dg + db * db) <= maxDistSq;
    }

    static bool findColorCentroid(const QImage &img, char target, QPointF &centroidOut) {
        if (img.isNull()) {
            return false;
        }
        double sx = 0.0;
        double sy = 0.0;
        int count = 0;

        int tr = 0;
        int tg = 0;
        int tb = 0;
        // Use uncommon signature colors so user drawings won't collide.
        if (target == 'r') {         // origin marker
            tr = 253; tg = 17; tb = 251;
        } else if (target == 'g') {  // x-axis marker
            tr = 19; tg = 251; tb = 233;
        } else if (target == 'b') {  // y-axis marker
            tr = 241; tg = 251; tb = 17;
        }

        const int maxDistSq = 55 * 55;
        for (int y = 0; y < img.height(); ++y) {
            const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                const int r = qRed(row[x]);
                const int g = qGreen(row[x]);
                const int b = qBlue(row[x]);
                const bool hit = isNearColor(r, g, b, tr, tg, tb, maxDistSq);
                if (hit) {
                    sx += static_cast<double>(x);
                    sy += static_cast<double>(y);
                    ++count;
                }
            }
        }
        if (count < 1) {
            return false;
        }
        centroidOut = QPointF(sx / count, sy / count);
        return true;
    }

    void updateCalibration(const QRect &targetRect) {
        calibrationValid_ = false;
        if (renderedImage_.isNull() || !targetRect.isValid()) {
            if (calibrationDebugPending_) {
                emit calibrationDebug("[Calib] No rendered image/target rect");
                calibrationDebugPending_ = false;
            }
            return;
        }
        QPointF redLocal, greenLocal, blueLocal;
        const bool okR = findColorCentroid(renderedImage_, 'r', redLocal);
        const bool okG = findColorCentroid(renderedImage_, 'g', greenLocal);
        const bool okB = findColorCentroid(renderedImage_, 'b', blueLocal);
        if (!okR || !okG || !okB) {
            if (calibrationDebugPending_) {
                emit calibrationDebug(
                    "[Calib] Marker detection failed "
                    "(R=" + QString(okR ? "ok" : "miss") +
                    ", G=" + QString(okG ? "ok" : "miss") +
                    ", B=" + QString(okB ? "ok" : "miss") + ")");
                calibrationDebugPending_ = false;
            }
            return;
        }
        const QPointF topLeft = targetRect.topLeft();
        originPx_ = topLeft + redLocal;   // (0,0)
        axisXPx_ = topLeft + greenLocal;  // (1,0)
        axisYPx_ = topLeft + blueLocal;   // (0,1)

        const QPointF u = axisXPx_ - originPx_;
        const QPointF v = axisYPx_ - originPx_;
        const double det = u.x() * v.y() - u.y() * v.x();
        calibrationValid_ = std::abs(det) > 1e-6;
        if (calibrationDebugPending_) {
            const QString msg =
                "[Calib] R=(" + QString::number(originPx_.x(), 'f', 1) + "," + QString::number(originPx_.y(), 'f', 1) + ") "
                "G=(" + QString::number(axisXPx_.x(), 'f', 1) + "," + QString::number(axisXPx_.y(), 'f', 1) + ") "
                "B=(" + QString::number(axisYPx_.x(), 'f', 1) + "," + QString::number(axisYPx_.y(), 'f', 1) + ") "
                "det=" + QString::number(det, 'f', 2) +
                (calibrationValid_ ? " OK" : " INVALID");
            emit calibrationDebug(msg);
            calibrationDebugPending_ = false;
        }
    }

    QPointF worldToScreen(double x, double y) const {
        const QPointF u = axisXPx_ - originPx_;
        const QPointF v = axisYPx_ - originPx_;
        return originPx_ + u * x + v * y;
    }

    bool screenToWorld(const QPointF &p, QPointF &worldOut) const {
        if (!calibrationValid_) {
            return false;
        }
        const QPointF u = axisXPx_ - originPx_;
        const QPointF v = axisYPx_ - originPx_;
        const double det = u.x() * v.y() - u.y() * v.x();
        if (std::abs(det) < 1e-9) {
            return false;
        }
        const QPointF d = p - originPx_;
        const double x = (d.x() * v.y() - d.y() * v.x()) / det;
        const double y = (u.x() * d.y() - u.y() * d.x()) / det;
        worldOut = QPointF(x, y);
        return true;
    }

    int hitTestMarker(const QPointF &pos) const {
        constexpr double threshold = 10.0;
        for (int i = 0; i < static_cast<int>(coordinates_.size()); ++i) {
            const QPointF p = worldToScreen(coordinates_[i].x, coordinates_[i].y);
            if (QLineF(pos, p).length() <= threshold) {
                return i;
            }
        }
        return -1;
    }

    void drawCoordinateMarkers(QPainter &painter) {
        if (!calibrationValid_ || coordinates_.empty()) {
            return;
        }
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor("#dc2626"), 2.0));
        constexpr int half = 6;
        for (const CoordPair &c : coordinates_) {
            const QPointF p = worldToScreen(c.x, c.y);
            painter.drawLine(QPointF(p.x() - half, p.y()), QPointF(p.x() + half, p.y()));
            painter.drawLine(QPointF(p.x(), p.y() - half), QPointF(p.x(), p.y() + half));
        }
        painter.restore();
    }

    QPdfDocument pdfDocument_;
    QImage renderedImage_;
    QSize renderedSize_;
    double viewScale_ = 1.0;
    QPointF panOffset_{0.0, 0.0};
    bool dragging_ = false;
    QPointF lastDragPos_{0.0, 0.0};
    bool markerDragging_ = false;
    int activeMarkerIndex_ = -1;
    std::vector<CoordPair> coordinates_;
    bool calibrationValid_ = false;
    QPointF originPx_{0.0, 0.0};
    QPointF axisXPx_{1.0, 0.0};
    QPointF axisYPx_{0.0, -1.0};
    int snapMm_ = 10;
    bool calibrationDebugPending_ = false;
};

class MainWindow : public KMainWindow {
    Q_OBJECT

public:
    MainWindow() {
        setWindowTitle("KTikZ");
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
        editorFont.setPointSize(12);
        editorView_->setConfigValue(QStringLiteral("font"), editorFont);

        editorDoc_->setText(
            "\\documentclass[tikz,border=10pt]{standalone}\n"
            "\\usepackage{tikz}\n"
            "\\begin{document}\n"
            "\\begin{tikzpicture}\n"
            "  \\draw[->,thick] (-1,0) -- (7,0);\n"
            "  \\draw[->,thick] (0,-1) -- (0,5);\n"
            "  \\draw[blue,very thick] (0,0) -- (4,2);\n"
            "  \\draw[red,thick] (1,1) -- (3,4);\n"
            "  \\draw[green!50!black,thick] (2,0.5) circle (1.2);\n"
            "  \\draw[orange,thick] (0,0) .. controls (2,3) .. (5,1);\n"
            "  \\fill[black] (0,0) circle (1.5pt) node[below left] {O};\n"
            "  \\fill[black] (4,2) circle (1.5pt) node[above right] {(4,2)};\n"
            "  \\fill[black] (3,4) circle (1.5pt) node[above right] {(3,4)};\n"
            "  \\fill[black] (2,0.5) circle (1.5pt) node[below] {(2,0.5)};\n"
            "  \\node at (5.5,3.8) {KTikZ demo};\n"
            "\\end{tikzpicture}\n"
            "\\end{document}\n");

        previewCanvas_ = new PdfCanvas(this);

        auto *splitter = new QSplitter(Qt::Horizontal, this);
        splitter->addWidget(editorView_);
        auto *rightPane = new QWidget(this);
        auto *rightLayout = new QVBoxLayout(rightPane);
        rightLayout->setContentsMargins(0, 0, 0, 0);
        rightLayout->setSpacing(6);
        rightLayout->addWidget(previewCanvas_, 1);

        auto *controlsRow = new QWidget(rightPane);
        auto *controlsLayout = new QHBoxLayout(controlsRow);
        controlsLayout->setContentsMargins(6, 0, 6, 6);
        controlsLayout->setSpacing(8);
        auto *stepLabel = new QLabel("Grid/Snap:", controlsRow);
        gridStepCombo_ = new QComboBox(controlsRow);
        gridStepCombo_->addItem("10 mm", 10);
        gridStepCombo_->addItem("5 mm", 5);
        gridStepCombo_->addItem("2 mm", 2);
        gridStepCombo_->addItem("1 mm", 1);
        gridStepCombo_->addItem("0 (free)", 0);
        gridStepCombo_->setCurrentIndex(0);
        controlsLayout->addWidget(stepLabel);
        controlsLayout->addWidget(gridStepCombo_);
        controlsLayout->addStretch(1);
        rightLayout->addWidget(controlsRow, 0);
        splitter->addWidget(rightPane);
        splitter->setSizes({600, 600});

        output_ = new QPlainTextEdit(this);
        output_->setReadOnly(true);
        output_->setPlaceholderText("Compilation output will appear here...");
        connect(previewCanvas_, &PdfCanvas::calibrationDebug, this, [this](const QString &line) {
            output_->appendPlainText(line);
        });

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
        connect(previewCanvas_, &PdfCanvas::coordinateDragged, this, &MainWindow::onCoordinateDragged);
        connect(gridStepCombo_, &QComboBox::currentIndexChanged, this, &MainWindow::onGridStepChanged);
        previewCanvas_->setSnapMm(gridSnapMm_);

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

    static QString formatStep(double step) {
        QString s = QString::number(step, 'f', 4);
        while (s.contains('.') && (s.endsWith('0') || s.endsWith('.'))) {
            s.chop(1);
        }
        return s;
    }

    static QString withInjectedGrid(const QString &source, int gridStepMm) {
        static const QRegularExpression beginTikzPattern(R"(\\begin\{tikzpicture\}(?:\[[^\]]*\])?)");
        static const QRegularExpression endTikzPattern(R"(\\end\{tikzpicture\})");
        const QRegularExpressionMatch m = beginTikzPattern.match(source);
        const QRegularExpressionMatch mEnd = endTikzPattern.match(source);
        if (!m.hasMatch() || !mEnd.hasMatch()) {
            return source;
        }

        const bool drawGrid = gridStepMm > 0;
        const QString stepExpr = drawGrid ? formatStep(static_cast<double>(gridStepMm) / 10.0) : QStringLiteral("1");
        QString gridBlock = "\n  % ktikz preview grid\n";
        if (drawGrid) {
            // Always keep 10mm (1 TikZ unit) lines prominent as major references.
            if (gridStepMm != 10) {
                gridBlock += "  \\draw[step=" + stepExpr + ", gray!18, very thin] (-10,-10) grid (10,10);\n";
            }
            gridBlock += "  \\draw[step=1, gray!38, thin] (-10,-10) grid (10,10);\n";
            gridBlock += "  \\draw[gray!50, thin] (-10,0) -- (10,0);\n";
            gridBlock += "  \\draw[gray!50, thin] (0,-10) -- (0,10);\n";
        }
        QString markerBlock;
        markerBlock += "\n  % ktikz calibration markers (top layer)\n";
        markerBlock += "  \\fill[draw=none,fill={rgb,255:red,253;green,17;blue,251}] (0,0) circle[radius=1.2pt];\n";
        markerBlock += "  \\fill[draw=none,fill={rgb,255:red,19;green,251;blue,233}] (1,0) circle[radius=1.2pt];\n";
        markerBlock += "  \\fill[draw=none,fill={rgb,255:red,241;green,251;blue,17}] (0,1) circle[radius=1.2pt];\n";

        QString out = source;
        out.insert(m.capturedEnd(0), gridBlock);
        // Insert markers right before \end{tikzpicture} so user elements cannot hide them.
        const int endPosAfterGrid = out.lastIndexOf("\\end{tikzpicture}");
        if (endPosAfterGrid >= 0) {
            out.insert(endPosAfterGrid, markerBlock);
        }
        return out;
    }

    static std::vector<CoordPair> extractCoordinates(const QString &source) {
        std::vector<CoordPair> coords;
        const std::vector<CoordRef> refs = extractCoordinateRefs(source);
        coords.reserve(refs.size());
        for (const CoordRef &r : refs) {
            coords.push_back({r.x, r.y});
        }
        return coords;
    }

    static std::vector<CoordRef> extractCoordinateRefs(const QString &source) {
        static const QRegularExpression coordPattern(
            R"(\(\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*,\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*\))");

        std::vector<CoordRef> refs;
        QRegularExpressionMatchIterator it = coordPattern.globalMatch(source);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            bool okX = false;
            bool okY = false;
            const double x = m.captured(1).toDouble(&okX);
            const double y = m.captured(2).toDouble(&okY);
            if (!okX || !okY) {
                continue;
            }
            CoordRef ref;
            ref.start = m.capturedStart(0);
            ref.end = ref.start + m.capturedLength(0);
            ref.x = x;
            ref.y = y;
            refs.push_back(ref);
        }
        return refs;
    }

    static QString formatNumber(double value) {
        QString s = QString::number(value, 'f', 4);
        while (s.contains('.') && (s.endsWith('0') || s.endsWith('.'))) {
            s.chop(1);
        }
        if (s == "-0") {
            s = "0";
        }
        return s;
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

        const QString sourceText = editorDoc_->text();
        const QString compileSource = withInjectedGrid(sourceText, gridDisplayMm_);
        coordinateRefs_ = extractCoordinateRefs(sourceText);
        previewCanvas_->setCoordinates(extractCoordinates(sourceText));

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

    void onCoordinateDragged(int index, double x, double y) {
        if (index < 0 || index >= static_cast<int>(coordinateRefs_.size())) {
            return;
        }
        if (compileProc_->state() != QProcess::NotRunning) {
            return;
        }

        const CoordRef ref = coordinateRefs_[index];
        QString text = editorDoc_->text();
        if (ref.end <= ref.start || ref.end > text.size()) {
            return;
        }

        const QString replacement = "(" + formatNumber(x) + "," + formatNumber(y) + ")";
        text.replace(ref.start, ref.end - ref.start, replacement);
        editorDoc_->setText(text);
        compile();
    }

    void onGridStepChanged(int) {
        const int selected = gridStepCombo_ ? gridStepCombo_->currentData().toInt() : 10;
        gridSnapMm_ = qMax(0, selected);
        gridDisplayMm_ = (gridSnapMm_ == 0) ? 10 : gridSnapMm_;
        previewCanvas_->setSnapMm(gridSnapMm_);
        statusBar()->showMessage(
            gridSnapMm_ == 0
                ? "Grid: 10 mm, Snap: free hand"
                : ("Grid/Snap step: " + QString::number(gridSnapMm_) + " mm"),
            1500);
        if (compileProc_ && compileProc_->state() == QProcess::NotRunning) {
            compile();
        }
    }

    KTextEditor::Document *editorDoc_ = nullptr;
    KTextEditor::View *editorView_ = nullptr;
    PdfCanvas *previewCanvas_ = nullptr;
    QPlainTextEdit *output_ = nullptr;
    QComboBox *gridStepCombo_ = nullptr;
    QProcess *compileProc_ = nullptr;
    std::vector<CoordRef> coordinateRefs_;
    int gridSnapMm_ = 10;
    int gridDisplayMm_ = 10;

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
