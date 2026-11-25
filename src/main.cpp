#include "common/temp_file.hpp"
#include "http/http_api.hpp"
#include "sdk/sdk.hpp"
#include <filesystem>
#include <csignal>
#include <pthread.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/logger.h>
int main(){try{
 sigset_t sigs;sigemptyset(&sigs);sigaddset(&sigs,SIGINT);sigaddset(&sigs,SIGTERM);sigaddset(&sigs,SIGUSR1);pthread_sigmask(SIG_BLOCK,&sigs,nullptr);std::signal(SIGPIPE,SIG_IGN);
 hk::Config cfg;std::filesystem::create_directories(cfg.temp);std::filesystem::create_directories(cfg.logdir);auto probe=hk::TempFile(cfg.temp);
 auto console=std::make_shared<spdlog::sinks::stdout_color_sink_mt>();auto file=std::make_shared<spdlog::sinks::rotating_file_sink_mt>(cfg.logdir+"/hk-recording-service.log",100*1024*1024,30);auto logger=std::make_shared<spdlog::logger>("hk-recording-service",spdlog::sinks_init_list{console,file});logger->set_level(spdlog::level::from_str(cfg.level));logger->flush_on(spdlog::level::info);spdlog::set_default_logger(logger);
 hk::Runtime runtime(cfg);return hk::run_server(cfg);
 }catch(const std::exception& e){spdlog::critical("startup failed: {}",e.what());return 1;}}
