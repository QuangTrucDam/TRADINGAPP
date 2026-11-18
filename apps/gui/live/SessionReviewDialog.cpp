#include "SessionReviewDialog.hpp"
#include "core/FixedPoint.hpp"
#include "live/SyntheticSession.hpp"
#include <QCloseEvent>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <stdexcept>
namespace trading_app_desktop {
using namespace trading_app_cli;
using namespace trading_app_cli::live;
SessionReviewDialog::SessionReviewDialog(SharedEngineGate gate, QWidget *parent)
    : QDialog(parent), gate_(std::move(gate)) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("M3 session review / replay / ledger audit");
    resize(1000, 700);
    auto *layout = new QVBoxLayout(this);
    status_ = new QLabel(
        "Open a sealed schema-3 recording. Review never connects a provider or resumes an account.", this);
    status_->setObjectName("reviewStatus");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto *row = new QHBoxLayout;
    open_ = new QPushButton("Open recording", this);
    replay_ = new QPushButton("Decision replay", this);
    replay_->setObjectName("decisionReplay");
    audit_ = new QPushButton("Ledger audit", this);
    audit_->setObjectName("ledgerAudit");
    cancel_ = new QPushButton("Cancel", this);
    export_ = new QPushButton("Export report", this);
    faults_ = new QPushButton("Run fault scenarios", this);
    speed_ = new QDoubleSpinBox(this);
    speed_->setRange(0, 10000);
    speed_->setValue(0);
    speed_->setSuffix(" × (0 = unpaced)");
    for (auto *button : {open_, replay_, audit_, cancel_, export_, faults_})
        row->addWidget(button);
    row->addWidget(speed_);
    layout->addLayout(row);
    output_ = new QPlainTextEdit(this);
    output_->setReadOnly(true);
    layout->addWidget(output_, 1);
    cancel_->setEnabled(false);
    export_->setEnabled(false);
    connect(open_, &QPushButton::clicked, this, [this] {
        auto path = QFileDialog::getOpenFileName(this, "Open recorded session", "recordings",
                                                 "JSONL sessions (*.jsonl)");
        if (!path.isEmpty()) {
            path_ = path;
            status_->setText(path);
            output_->setPlainText("Ready for decision replay or independent ledger audit. "
                                  "Unfinished/corrupted files are rejected; they are never resumed.");
        }
    });
    connect(replay_, &QPushButton::clicked, this, [this] { run(false); });
    connect(audit_, &QPushButton::clicked, this, [this] { run(true); });
    connect(cancel_, &QPushButton::clicked, this, &SessionReviewDialog::requestCancel);
    connect(faults_, &QPushButton::clicked, this, &SessionReviewDialog::runFaults);
    connect(export_, &QPushButton::clicked, this, [this] {
        auto path = QFileDialog::getSaveFileName(this, "Export review report", "M3_review_report.md",
                                                 "Markdown (*.md)");
        if (path.isEmpty())
            return;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            file.write(report_.toUtf8()) != report_.toUtf8().size())
            status_->setText("Could not write report");
        else
            status_->setText("Report saved: " + path);
    });
    connect(&watcher_, &QFutureWatcher<ReviewResult>::finished, this, &SessionReviewDialog::finished);
}
SessionReviewDialog::~SessionReviewDialog() {
    requestCancel();
    watcher_.disconnect(this);
    watcher_.waitForFinished();
    if (claimed_)
        gate_->release("review");
}
void SessionReviewDialog::setRecordingPath(const QString &path) {
    if (!isBusy()) {
        path_ = path;
        status_->setText(path);
    }
}
void SessionReviewDialog::requestCancel() {
    if (stop_)
        stop_->store(true);
}
void SessionReviewDialog::closeEvent(QCloseEvent *event) {
    if (isBusy()) {
        closing_ = true;
        requestCancel();
        event->ignore();
    } else
        event->accept();
}
void SessionReviewDialog::run(bool audit) {
    if (isBusy() || path_.isEmpty())
        return;
    if (!gate_->acquire("review")) {
        status_->setText("Another engine job is active: " + QString::fromStdString(gate_->owner()));
        return;
    }
    claimed_ = true;
    stop_ = std::make_shared<std::atomic<bool>>(false);
    const auto stop = stop_;
    const auto path = path_.toStdString();
    double speed = speed_->value();
    for (auto *b : {open_, replay_, audit_, faults_})
        b->setEnabled(false);
    cancel_->setEnabled(true);
    export_->setEnabled(false);
    status_->setText(audit ? "Auditing ledger…" : "Replaying recorded inputs…");
    watcher_.setFuture(QtConcurrent::run([path, audit, speed, stop] {
        ReviewResult result;
        try {
            auto archive = loadArchive(path);
            result = audit ? ledgerAudit(archive, stop.get()) : decisionReplay(archive, speed, stop.get());
            if (!audit)
                result.report = replayPerformanceMarkdown(archive, result).toStdString();
        } catch (const std::exception &e) {
            result.message = e.what();
        }
        return result;
    }));
}
void SessionReviewDialog::finished() {
    auto r = watcher_.result();
    if (claimed_) {
        gate_->release("review");
        claimed_ = false;
    }
    for (auto *b : {open_, replay_, audit_, faults_})
        b->setEnabled(true);
    cancel_->setEnabled(false);
    status_->setText(QString(r.cancelled ? "CANCELLED · "
                             : r.passed  ? "PASS · "
                                         : "FAILED · ") +
                     QString::fromStdString(r.message));
    report_ = QString("# M3 session review\n\n%1\n\nInput steps: %2. Audited/generated executions: %3.\n\n")
                  .arg(QString::fromStdString(r.message))
                  .arg(r.steps)
                  .arg(r.executions);
    if (r.state.paper.account) {
        const auto &a = *r.state.paper.account;
        report_ += QString("Final account: cash %1; fees %2; realized PnL %3; state %4.\n\n")
                       .arg(QString::fromStdString(formatMoney(a.cash)),
                            QString::fromStdString(formatMoney(a.total_fees)),
                            QString::fromStdString(formatMoney(a.realized_pnl)),
                            QString::fromStdString(r.state.paper.state));
        report_ += "| Execution | Order | Side | Quantity | Price | Fee |\n|---|---|---|---:|---:|---:|\n";
        for (const auto &e : r.state.paper.recent_executions)
            report_ += QString("| %1 | %2 | %3 | %4 | %5 | %6 |\n")
                           .arg(e.execution_id)
                           .arg(e.order_id)
                           .arg(e.side == Side::Buy ? "Buy" : "Sell")
                           .arg(e.quantity)
                           .arg(QString::fromStdString(formatPrice(e.price)),
                                QString::fromStdString(formatMoney(e.fee)));
    }
    if (!r.report.empty())
        report_ += "\n" + QString::fromStdString(r.report);
    output_->setPlainText(report_);
    export_->setEnabled(!report_.isEmpty());
    if (closing_)
        close();
}
void SessionReviewDialog::runFaults() {
    if (isBusy())
        return;
    if (!gate_->acquire("review")) {
        status_->setText("Another engine job is active");
        return;
    }
    auto results = runFaultScenarios();
    gate_->release("review");
    report_ =
        "# M3 deterministic fault scenarios\n\n| Scenario | Expected outcome | Result |\n|---|---|---|\n";
    bool ok = true;
    for (const auto &r : results) {
        ok = ok && r.passed;
        report_ += QString("| %1 | %2 | %3 |\n")
                       .arg(QString::fromStdString(r.scenario), QString::fromStdString(r.expected),
                            r.passed ? "PASS" : "FAIL");
    }
    output_->setPlainText(report_);
    status_->setText(ok ? "Fault scenarios passed" : "Fault scenario failed");
    export_->setEnabled(true);
}
} // namespace trading_app_desktop
