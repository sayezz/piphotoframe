#pragma once

#include <string>
#include <vector>
#include <queue>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <opencv2/opencv.hpp>
#include <random>
#include <chrono>
#include <unordered_set>
#include <sys/stat.h> 
#include <ctime>
#include <exiv2/exiv2.hpp>
#include <iomanip>
#include <sstream>
#include "json.hpp"
class DisplayImg {
public:
    DisplayImg();
    ~DisplayImg();


    std::vector<std::string> findImages();
    void startPreloading();
    cv::Mat getNextImage();
    cv::Mat getPrevImage();

    void resetVisitedPathsIfNeeded();

    void setFolderFilters(std::vector<std::string> folderFilter);
    void setShowDate(bool value);
    void setShowImgCount(bool value);
    void setShowFolderName(bool value);
private:
    std::string replaceUmlauts(const std::string& input);
    void preloadThreadFunc();
    void loadVisitedPathsFromJson();
    void saveVisitedPathToJson(const std::string& newPath);
    void writeDate(cv::Mat& mat, std::string filePath);
    //void showFolderName(cv::Mat& mat, std::string filePath);
    void drawRoundedRectangle(cv::Mat& img, const cv::Rect& rect, const cv::Scalar& color, int radius, double alpha);
    void showImageCount(cv::Mat& mat);
    cv::Mat showImage(std::pair<std::string, cv::Mat> pair);
    std::string folderPath = "/mnt/paulNAS/Bilder/";
    std::vector<std::string> folderFilter;

    std::mutex visitedPathsMutex;
    const std::string dbFilePath = "db.json";

    std::vector<std::string> imagePaths;
    std::unordered_set<std::string> visitedPaths;
    std::queue<std::pair<std::string, cv::Mat>> imageQueue;
    std::deque<std::pair<std::string, cv::Mat>> pastImages;

    std::mutex queueMutex;
    std::condition_variable queueCondVar;
    std::thread preloadThread;
    bool stopThread;
    bool showDate = true;
    bool showImgCount = true;
    bool showFldrName = true;

    const int bufferSize = 10;
    const int prevImageBufferSize = 5;
    int currentBufferIndex = 99;   
    bool first = true;
    bool x = false;
    
    std::pair<std::string, cv::Mat> currentImg;
 
};