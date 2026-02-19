#ifndef COMPILESERVICE_H
#define COMPILESERVICE_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTemporaryDir>
#include <memory>

class compileservice : public QObject {
    Q_OBJECT

public:
    explicit compileservice(QObject *parent = nullptr);

    bool is_busy() const;
    void compile(const QString &source_text, int grid_step_mm);

signals:
    void output_text(const QString &text);
    void compile_finished(bool success, const QString &pdf_path, const QString &message);

private slots:
    void on_ready_output();
    void on_finished(int exit_code, QProcess::ExitStatus status);

private:
    bool ensure_work_dir();
    static QString format_step(double step);
    static QString inject_grid(const QString &source, int grid_step_mm);

    QProcess proc_;
    QString work_dir_path_;
    std::unique_ptr<QTemporaryDir> temp_dir_;
};

#endif
