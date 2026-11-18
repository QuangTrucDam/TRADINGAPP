#pragma once
#include <cstddef>
#include <deque>
#include <optional>
#include <stdexcept>
namespace trading_app_cli::live {
class IndicatorSeries {
public:
    explicit IndicatorSeries(std::size_t period=14) : n_(period) {
        if (period<2 || period>10000) throw std::invalid_argument("indicator period outside [2,10000]");
    }
    void add(double close) {
        ++count_; closes_.push_back(close); sum_+=close;
        if (closes_.size()>n_) { sum_-=closes_.front(); closes_.pop_front(); }
        if (closes_.size()==n_) {
            sma=sum_/n_;
            ema=ema ? *ema+(2.0/(n_+1))*(close-*ema) : *sma;
        }
        if (last_) {
            const double delta=close-*last_, gain=delta>0?delta:0, loss=delta<0?-delta:0;
            if (count_<=n_+1) { gains_+=gain; losses_+=loss; }
            else { gains_=(gains_*(n_-1)+gain)/n_; losses_=(losses_*(n_-1)+loss)/n_; }
            if (count_==n_+1) { gains_/=n_; losses_/=n_; }
            if (count_>=n_+1) rsi=losses_==0?(gains_==0?50:100):100-100/(1+gains_/losses_);
        }
        last_=close;
    }
    std::size_t count() const { return count_; }
    std::optional<double> sma,ema,rsi;
private:
    std::size_t n_,count_{};
    std::deque<double> closes_;
    std::optional<double> last_;
    double sum_{},gains_{},losses_{};
};
}
