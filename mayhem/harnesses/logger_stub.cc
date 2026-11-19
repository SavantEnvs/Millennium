// Stand-in for src/system/logger.cc. The real logger pulls in millennium/environment.h
// (Steam install path discovery, log files under the Steam dir) which does not exist
// outside a Steam client. The parsers only call logger.print() through LOG_ERROR/warn/log
// to report rejected input, so a sink that discards messages keeps their behaviour.
#include "millennium/logger.h"

millennium_logger::millennium_logger() {}
void millennium_logger::print(std::string, const std::string&, std::string) {}
void millennium_logger::log_plugin_message(std::string, std::string) {}
millennium_logger& logger = millennium_logger::get_instance();
