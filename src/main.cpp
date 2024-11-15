#include <spdlog/sinks/basic_file_sink.h>

#include <windows.h>

#include <argparse/argparse.hpp>
#include <atomic>
#include <chrono>

#include <cstddef>
#include <ctime>
#include <filesystem>
#include <iomanip>

#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>


#include "AVSManager.h"
#include "spdlog/spdlog.h"
static const std::string versionOfAvsOperator = "1.0.0";

std::atomic<bool> running_flag(true), exit_thread_flag(false);
std::atomic<double> globalAngleOfMotor(90);
std::atomic<AVSManager::AdjustMethod> globalMethod(AVSManager::AdjustMethod::average);

std::shared_ptr<spdlog::logger> file_logger;
std::unique_ptr<AVSManager> avsManager;

void timerHookFunction(int timeVal, int averageNumber, const std::filesystem::path &outputFilePath);

int main(int argc, const char *argv[]) {
    argparse::ArgumentParser program("avs-operator", versionOfAvsOperator);
    program.add_argument("-o", "--output").help("set the output file path").default_value(std::string("."));
    program.add_argument("-g", "--logging").help("set the logging file path").default_value(std::string("None"));
    program.add_argument("-m", "--measure").help("set the measure times").default_value(int(5)).action([](const std::string &value) { return std::stoi(value); });
    program.add_argument("-i", "--intergraltime").help("set the intergral times").default_value(int(5)).action([](const std::string &value) { return std::stoi(value); });
    program.add_argument("-n", "--intergralnumber").help("set the intergral number").default_value(int(50)).action([](const std::string &value) { return std::stoi(value); });
    std::string outputFilePathStr, loggingFile;
    int measureTime, intergraltime, intergralnumber;
    try {
        program.parse_args(argc, argv);

        outputFilePathStr = program.get<std::string>("output");
        loggingFile = program.get<std::string>("logging");
        measureTime = program.get<int>("measure");
        intergraltime = program.get<int>("intergraltime");
        intergralnumber = program.get<int>("intergralnumber");
        if (loggingFile != "None") {
            file_logger = spdlog::basic_logger_mt("logger", loggingFile);
            spdlog::set_default_logger(file_logger);
        }
        spdlog::info("the output file of log is in {}", outputFilePathStr);
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
    auto nowTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto ret = std::put_time(localtime(&nowTime), "%Y-%m-%d");
    std::ostringstream oss;
    oss << ret;
    std::filesystem::path outputFilePath(outputFilePathStr);
    outputFilePath = outputFilePathStr / std::filesystem::path(oss.str());
    std::filesystem::create_directories(outputFilePath);
    spdlog::info("entry create {} path", outputFilePath.string());
    avsManager = std::make_unique<AVSManager>(0, 300, 460, std::string("Huai Bei"));
    auto numberID = avsManager->findDevice();
    if (numberID <= 0) {
        spdlog::error("plz check avs-line is linked?");
        return -1;
    }
    numberID -= 1;
    avsManager->activateDevice(numberID);
    while (measureTime--) {
        timerHookFunction(intergraltime, intergralnumber, outputFilePath);
    }
    return 0;
}

void timerHookFunction(int timeVal, int averageNumber, const std::filesystem::path &outputFilePath) {
    static unsigned int timeEntry = 0;
    int tick = timeVal * averageNumber;
    avsManager->measurePerpare(avsManager->getActivateID(), timeVal, averageNumber);
    spdlog::info("Start Adjust Measure");
    std::this_thread::sleep_for(std::chrono::milliseconds(tick));
    auto adjustData = avsManager->measureData(avsManager->getActivateID());
    timeVal = avsManager->adjustVal(std::get<0>(adjustData), globalAngleOfMotor, globalMethod);
    spdlog::info("Adjust Intergral-Time to {}", timeVal);
    time_t inputTimeT = avsManager->measurePerpare(avsManager->getActivateID(), timeVal, averageNumber);
    spdlog::info("Start Right Measure");
    std::this_thread::sleep_for(std::chrono::milliseconds(tick));
    auto retData = avsManager->measureData(avsManager->getActivateID());
    avsManager->saveDataInFile(outputFilePath, std::get<0>(retData), inputTimeT, std::get<1>(retData));
    spdlog::info("Save the {} times Measure in PATH {}", ++timeEntry, outputFilePath.string());
}

