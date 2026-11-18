#include "LiveConfiguration.hpp"
#include <cstdlib>
#include <fstream>
#include <stdexcept>
namespace trading_app_cli::live {
namespace {
std::string trim(std::string s) {
    auto a=s.find_first_not_of(" \t\r\n"), b=s.find_last_not_of(" \t\r\n");
    return a==std::string::npos?"":s.substr(a,b-a+1);
}
}
std::map<std::string,std::string> readDotEnv(const std::filesystem::path& path) {
    std::map<std::string,std::string> out;
    if (!std::filesystem::exists(path)) return out;
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot read local configuration");
    std::string line; std::size_t total=0;
    while (std::getline(file,line)) {
        total+=line.size(); if (total>65536) throw std::runtime_error("Configuration too large");
        line=trim(line); if (line.empty() || line[0]=='#') continue;
        auto eq=line.find('='); if (eq==std::string::npos) throw std::runtime_error("Invalid configuration assignment");
        auto name=trim(line.substr(0,eq)), value=trim(line.substr(eq+1));
        if (name.empty() || name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_")!=std::string::npos)
            throw std::runtime_error("Invalid configuration name");
        if (value.size()>=2 && (value.front()=='\'' || value.front()=='\"') && value.back()==value.front())
            value=value.substr(1,value.size()-2);
        out[name]=value;
    }
    return out;
}
std::string configuredValue(const std::map<std::string,std::string>& values,const std::string& name) {
    if (auto* v=std::getenv(name.c_str())) return v;
    auto it=values.find(name); return it==values.end()?"":it->second;
}
}
