#pragma once
#include "live/LiveProcessor.hpp"
#include "live/Performance.hpp"
#include "recording/SessionRecorder.hpp"
#include <QJsonObject>
#include <atomic>
#include <functional>
#include <memory>
namespace trading_app_cli::live {
QJsonObject configurationJson(const SessionConfiguration &);
SessionConfiguration configurationFromJson(const QJsonObject &);
QJsonObject inputJson(const Input &);
Input inputFromJson(const QJsonObject &);
QJsonObject resultJson(const ProcessingResult &);
QJsonObject stateJson(const ProcessorState &);
struct Archive {
    QJsonObject metadata, terminal;
    std::vector<QJsonObject> steps;
};
class ArchiveWriter {
  public:
    ArchiveWriter(const std::string &path, QJsonObject metadata);
    void step(const Input &, const ProcessingResult &, const ProcessorState &);
    void finish(const ProcessorState &);
    bool failed() const { return recorder_.failed(); }
    bool done() const { return recorder_.done(); }
    std::size_t highWater() const;
    std::size_t depth() const { return recorder_.depth(); }
    std::uint64_t overflows() const { return recorder_.overflows(); }

  private:
    void write(QJsonObject);
    SessionRecorder recorder_;
    QByteArray previous_ = QByteArray(64, '0');
    std::uint64_t records_{};
};
Archive loadArchive(const std::string &path);
struct ReviewResult {
    bool passed{}, cancelled{};
    std::string message, report;
    ProcessorState state;
    std::uint64_t steps{}, executions{};
    TimingSummary processing;
    double elapsed_seconds{};
};
ReviewResult decisionReplay(const Archive &, double speed = 0, std::atomic<bool> *stop = nullptr,
                            const std::function<void(const ProcessorState &)> &progress = {});
ReviewResult ledgerAudit(const Archive &, std::atomic<bool> *stop = nullptr);
QString replayPerformanceMarkdown(const Archive &, const ReviewResult &);
} // namespace trading_app_cli::live
