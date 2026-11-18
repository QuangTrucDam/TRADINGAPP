#include "SessionRecorder.hpp"
#include <fstream>
namespace trading_app_cli::live {
SessionRecorder::SessionRecorder(const std::filesystem::path& path,std::size_t limit) {
    // File creation happens on writer thread: errors propagate through failed().
    writer_=std::thread([this,path,limit] {
        try {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream file(path,std::ios::out|std::ios::trunc);
            file.exceptions(std::ios::badbit|std::ios::failbit);
            std::size_t written=0; std::string line;
            while(!queue_.drained()) {
                if(!queue_.pop(line)) continue;
                if(line.size()+1>limit-written) throw std::runtime_error("recording size limit");
                file<<line<<'\n'; written+=line.size()+1;
                // Bound buffered data; any failed write makes the recording incomplete.
                file.flush();
            }
            file.close();
        } catch(...) { failed_=true; }
        done_=true;
    });
}
SessionRecorder::~SessionRecorder() { close(); if(writer_.joinable()) writer_.join(); }
bool SessionRecorder::append(std::string line) {
    if(failed_) return false;
    if(!queue_.push(std::move(line))) { ++overflows_; failed_=true; return false; }
    return true;
}
void SessionRecorder::close() { queue_.close(); }
}
