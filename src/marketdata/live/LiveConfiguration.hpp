#pragma once
#include <filesystem>
#include <map>
#include <string>
namespace trading_app_cli::live {
// No shell execution, interpolation or export to the process environment.
std::map<std::string,std::string> readDotEnv(const std::filesystem::path& path);
std::string configuredValue(const std::map<std::string,std::string>& values,const std::string& name);
}
