#include "AVSManager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <memory>
#include <numeric>
#include <ostream>
#include <string>
#include <tuple>

#include <vector>

#include "Utility.h"
#include "lib/avaspecx64.h"

AVSManager::AVSManager(int port, int waveBegin, int waveEnding, std::string siteName): waveBegin_(waveBegin), waveEnding_(waveEnding), siteName_(siteName) {
    AVS_Init(port);  // use usb port
    char tmp[20];
    AVS_GetDLLVersion(tmp);
    versionInfo_ = std::string(tmp);
    spdlog::info("AVS DLL version: {}", versionInfo_);
}

int AVSManager::findDevice() {
    unsigned int numberOfDevices = AVS_UpdateUSBDevices();
    if (!numberOfDevices) {
        spdlog::warn("there is not devices is linked!");
        return -1;
    }
    this->avsIdentityList_ = std::make_unique<AvsIdentityType[]>(numberOfDevices);
    auto errorCode = AVS_GetList(numberOfDevices * sizeof(AvsIdentityType), &numberOfDevices, this->avsIdentityList_.get());
    if (LOG_ERROR(errorCode)) return errorCode;
    spdlog::info("Device of Number is {}", errorCode);
    return errorCode;
}

AVSManager::~AVSManager() {
    AVS_Done();
    if (this->lambdaArrayOfDevice_ != nullptr) delete[] this->lambdaArrayOfDevice_;
}

int AVSManager::activateDevice(int number) {
    // Deactivate of last device.
    if (this->activatedDeviceListMap_.find(number) != this->activatedDeviceListMap_.end()) {
        AVS_Deactivate(this->activatedDeviceListMap_[number]);
    }
    // activate the NUMBER device
    auto deviceHandle = AVS_Activate(&this->avsIdentityList_[number]);
    this->activatedDeviceListMap_.insert({number, deviceHandle});
    auto errorCode = AVS_UseHighResAdc(deviceHandle, AVS_ENABLE);
    if (LOG_ERROR(errorCode)) return errorCode;
    spdlog::info("Device {} activated, the Handle is {}", number, deviceHandle);
    errorCode = AVS_GetNumPixels(deviceHandle, &this->numPixelsOfDevice_);
    if (LOG_ERROR(errorCode)) return errorCode;
    this->activatedDeviceID_ = number;
    spdlog::info("Pixels of Device {} is {}", number, this->numPixelsOfDevice_);
    if (this->lambdaArrayOfDevice_ != nullptr) delete[] this->lambdaArrayOfDevice_;
    this->lambdaArrayOfDevice_ = new double[this->numPixelsOfDevice_];
    AVS_GetLambda(deviceHandle, this->lambdaArrayOfDevice_);  // Get \lambda list.
    return 0;
}
void measureHookFunction(AvsHandle *handle, int *val) { spdlog::debug("the handle of measure device is {}, the val is {}", *handle, *val); }
time_t AVSManager::measurePerpare(int numberID, double intergralTime, int averagesNum) {
    MeasConfigType measConfigure;
    this->intergralTime_ = intergralTime;
    this->averagesNum_ = averagesNum;
    measConfigure.m_StartPixel = 0;
    measConfigure.m_StopPixel = this->numPixelsOfDevice_ - 1;
    measConfigure.m_IntegrationTime = intergralTime;  // 积分时间
    measConfigure.m_IntegrationDelay = 0;
    measConfigure.m_NrAverages = averagesNum;  // 单次测量的平均值个数
    measConfigure.m_CorDynDark.m_Enable = 0;
    measConfigure.m_CorDynDark.m_ForgetPercentage = 0;
    measConfigure.m_Smoothing.m_SmoothPix = 0;
    measConfigure.m_Smoothing.m_SmoothModel = 0;
    measConfigure.m_SaturationDetection = 0;
    measConfigure.m_Trigger.m_Mode = 0;
    measConfigure.m_Trigger.m_Source = 0;
    measConfigure.m_Trigger.m_SourceType = 0;
    measConfigure.m_Control.m_StrobeControl = 0;
    measConfigure.m_Control.m_LaserDelay = 0;
    measConfigure.m_Control.m_LaserWidth = 0;
    measConfigure.m_Control.m_LaserWaveLength = 0;
    measConfigure.m_Control.m_StoreToRam = 0;
    int errorCode;
    try {
        errorCode = AVS_PrepareMeasure(this->activatedDeviceListMap_.at(numberID), &measConfigure);
    } catch (const std::out_of_range &e) {
        spdlog::error("mssage: DEVICE {} is not ACTIVATE occupid. FUNCTION {}, LINE {}", numberID, __FUNCTION__, __LINE__);
        return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    }
    errorCode = AVS_MeasureCallback(this->activatedDeviceListMap_.at(numberID), measureHookFunction, 1);
    if (LOG_ERROR(errorCode)) return errorCode;
    auto timeValue = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    spdlog::info("Perpare of Meassage has done");
    return timeValue;
}
#define AVS_POLLSCAN_NO_DATA_AVAILABLE 0
std::tuple<std::vector<double>, std::time_t> AVSManager::measureData(int numberID) {
    int errorCode;
    try {
        do {
            errorCode = AVS_PollScan(this->activatedDeviceListMap_.at(numberID));
        } while (errorCode == AVS_POLLSCAN_NO_DATA_AVAILABLE);
        if (LOG_ERROR(errorCode)) return {};
        unsigned int timeLabel;
        std::vector<double> arrayOfSpectrum(this->numPixelsOfDevice_);
        // 获取数据并存储到 vector 中
        errorCode = AVS_StopMeasure(this->activatedDeviceListMap_.at(numberID));
        if (LOG_ERROR(errorCode)) return {};
        errorCode = AVS_GetScopeData(this->activatedDeviceListMap_.at(numberID), &timeLabel, arrayOfSpectrum.data());
        if (LOG_ERROR(errorCode)) return {};
        auto timeVal = std::chrono::system_clock::now();

        return std::tuple(arrayOfSpectrum, std::chrono::system_clock::to_time_t(timeVal));
    } catch (const std::out_of_range &e) {
        spdlog::error("DEVICE {} not activate. FUNCTION {}, LINE {}", numberID, __FUNCTION__, __LINE__);
        return {};
    }
}

#include <iostream>
#include <fstream>
#include <iomanip>
#include <filesystem>
#include <ctime>
#include <vector>
#include <spdlog/spdlog.h>

int AVSManager::saveDataInFile(const std::filesystem::path &filePath, std::vector<double> data, time_t inputTimeT, time_t outputTimeT) {
    char buffer[8];
    
    // 获取 inputTime 和 outputTime 的本地时间
    struct tm inputTime = *localtime(&inputTimeT);  // 深拷贝 inputTime
    struct tm outputTime = *localtime(&outputTimeT); // 深拷贝 outputTime
    
    // 格式化 inputTime 到 buffer
    std::strftime(buffer, sizeof(buffer), "%H%M%S", &inputTime);
    
    // 获取经纬度信息
    this->getLonAndLat();
    
    // 构造文件名
    std::filesystem::path fileName = std::string(buffer) + ".std";
    fileName = filePath / fileName;
    
    spdlog::info("{} file has been created", fileName.filename().string());
    std::fstream fileStream(fileName, std::ios_base::trunc | std::ios_base::out);
    
    // 检查文件是否成功打开
    if (!fileStream.is_open()) {
        spdlog::error("message:error to open log file. function {}, line {}", __FUNCTION__, __LINE__);
        return -1;
    }
    
    // 写入文件内容
    fileStream << "zenith DOAS" << std::endl;
    fileStream << "1" << std::endl;
    fileStream << this->numPixelsOfDevice_ << std::endl;
    
    // 写入数据
    for (auto &v : data) {
        fileStream << std::fixed << std::setprecision(4) << v << std::endl;
    }
    
    // 写入文件名
    fileStream << fileName.filename().string() << std::endl;
    fileStream << "OceanOptics" << std::endl;
    fileStream << "HR2000+" << std::endl;
    
    // 格式化并写入时间
    fileStream << std::put_time(&inputTime, "%Y.%m.%d") << std::endl;
    fileStream << std::put_time(&inputTime, "%H:%M:%S") << std::endl;
    fileStream << std::put_time(&outputTime, "%H:%M:%S") << std::endl;
    
    // 其他信息
    fileStream << this->waveBegin_ << std::endl;
    fileStream << this->waveEnding_ << std::endl;
    fileStream << "SCANS " << this->averagesNum_ << std::endl;
    fileStream << "INT_TIME " << this->intergralTime_ << std::endl;
    fileStream << "SITE " << this->siteName_ << std::endl;
    fileStream << "LONGITUDE " << std::fixed << std::setprecision(8) << longitude_ << std::endl;
    fileStream << "LATITUDE " << std::fixed << std::setprecision(8) << latitude_ << std::endl;
    
    // 关闭文件
    fileStream.close();
    return 0;
}


static bool isApproximatelyEqual(double a, double b, double epsilon = 1e-9) { return std::abs(a - b) < epsilon; }

int AVSManager::adjustVal(const std::vector<double> &data, double angle, AVSManager::AdjustMethod method) {
    double adjustValue;
    double summ = std::accumulate(data.begin(), data.end(), 0.0);
    double maxi = *std::max_element(data.begin(), data.end());
    switch (method) {
        case AdjustMethod::average:
            if (isApproximatelyEqual(angle, 90)) {
                adjustValue = 3620400.61632 * std::exp(-(summ / 2048) / 91.29643) + 86.36638 * std::exp(-(summ / 2048) / 753.67786) +
                              1033.08165 * std::exp(-(summ / 2048) / 753.53241) + 39.21313;
            } else {
                adjustValue = 62854.56078 * std::exp(-(summ / this->numPixelsOfDevice_) / 142.29071) + 6781470000.0 * std::exp(-(summ / this->numPixelsOfDevice_) / 40.54059) +
                              824.25339 * std::exp(-(summ / this->numPixelsOfDevice_) / 636.74826) + 39.99177;
            }
            break;
        case AdjustMethod::maximum:
            adjustValue = 1759198.71151 * std::exp(-maxi / 116.34418) + 3681.8905 * std::exp(-maxi / 445.74025) + 218.68263 * std::exp(-maxi / 2556.71114) + 14.01163;
            if (isApproximatelyEqual(angle, 90)) {
                adjustValue = 0.6534 * adjustValue - 1.45531;
            }
            break;
    }
    return int(adjustValue);
}
#define AVS_POLLSCAN_NO_DATA_AVAILABLE 0
std::vector<double> AVSManager::measureData(int numberID) {
    int errorCode;
    try {
        do {
            errorCode = AVS_PollScan(this->activatedDeviceListMap_.at(numberID));
        } while (errorCode == AVS_POLLSCAN_NO_DATA_AVAILABLE);
        if (LOG_ERROR(errorCode)) return {};
        unsigned int timeLabel;
        std::vector<double> arrayOfSpectrum(this->numPixelsOfDevice_);

        // 获取数据并存储到 vector 中
        errorCode = AVS_GetScopeData(this->activatedDeviceListMap_.at(numberID), &timeLabel, arrayOfSpectrum.data());

        if (LOG_ERROR(errorCode)) return {};

        return arrayOfSpectrum;
    } catch (const std::out_of_range &e) {
        spdlog::error("DEVICE {} not activate. FUNCTION {}, LINE {}", numberID, __FUNCTION__, __LINE__);
        return {};
    }
}
int AVSManager::getLonAndLat() {
    this->longitude_ = 116.81764367;
    this->latitude_ = 34.00224967;
    return 0;
}
