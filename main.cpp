#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include "DisplayImg.h"
#include <opencv2/opencv.hpp>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>
#include <X11/Xatom.h>
#include "json.hpp"
#include <fstream>

#include <iostream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <Windows.h>
#endif // _WIN32

using namespace std;
using json = nlohmann::json;

//bool mouseClicked = false;
int clickX = 0, clickY = 0;
std::chrono::steady_clock::time_point clickTime;
bool showClickEffect = false;

int globalTimer = 30;
std::vector<std::string> globalFilters;
bool globalEnableTouch = true;
bool globalShowDate = true;
bool globalShowImgCount = true;
bool globalShowFolderName = true;

bool isPressed = false;
bool pendingClick = false;

int screenWidth = 1920;
int screenHeight = 1080;

void onMouse(int event, int x, int y, int flags, void* userdata)
{
    if(globalEnableTouch){
        if (event == cv::EVENT_LBUTTONDOWN || event == cv::EVENT_RBUTTONDOWN ) {
            isPressed = true;
            clickX = x;
            clickY = y;
            clickTime = std::chrono::steady_clock::now();
            showClickEffect = true;
        }

        if(event == cv::EVENT_LBUTTONUP || event == cv::EVENT_RBUTTONUP)
        {
            if(isPressed){
                isPressed = false;
                pendingClick = true;
            }
        }
    }
}

void hideCursorInWindow(Display* display, Window window) {
    // Create an empty (invisible) Pixmap
    Pixmap pixmap = XCreatePixmap(display, window, 1, 1, 1); // 1x1 pixel, 1-bit depth
    XColor color;
    color.pixel = 0;
    color.red = color.green = color.blue = 0;
    color.flags = DoRed | DoGreen | DoBlue;

    // Create an invisible cursor using the Pixmap
    Cursor invisibleCursor = XCreatePixmapCursor(display, pixmap, pixmap, &color, &color, 0, 0);

    // Set the invisible cursor on the OpenCV window
    XDefineCursor(display, window, invisibleCursor);
    XFlush(display); // Make sure the change is applied immediately

    // Cleanup
    XFreePixmap(display, pixmap);
}


void getOpenCVWindowHandle(const std::string& windowName) {
    // Open the display connection
    Display* display = XOpenDisplay(NULL);
    if (!display) {
        std::cerr << "Unable to open X display!" << std::endl;
        return;
    }

    // Create an OpenCV window
    cv::namedWindow(windowName, cv::WINDOW_NORMAL);
    cv::imshow(windowName, cv::Mat::zeros(1, 1, CV_8UC3));  // Create the window

    // Sleep briefly to ensure that OpenCV initializes the window
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Query the list of windows from the X server
    Window root = DefaultRootWindow(display);
    Window parent;
    Window* children;
    unsigned int numChildren;

    if (XQueryTree(display, root, &root, &parent, &children, &numChildren)) {
        for (unsigned int i = 0; i < numChildren; ++i) {
            char* windowNameFromX = nullptr;
            if (XFetchName(display, children[i], &windowNameFromX)) {
                if (windowName == windowNameFromX) {
                    std::cout << "Found OpenCV window with name: " << windowNameFromX << std::endl;
                    std::cout << "Window handle: " << children[i] << std::endl;
                    hideCursorInWindow(display, children[i]);
                    XFree(windowNameFromX);
                    break;
                }else{cout<< "no window"<<std::endl;}
                XFree(windowNameFromX);
            }
        }
    }

    Screen*  screen = DefaultScreenOfDisplay(display);
    screenWidth = screen->width;
    screenHeight = screen->height;

    // Close the X display connection
    XCloseDisplay(display);
}

bool loadSettings(std::string configPath) {
    try {
        // Open the config file
        std::ifstream configFile(configPath);
        if (!configFile.is_open()) {
            std::cerr << "Failed to open config file: " << configPath << std::endl;
            return false;
        }

        // Parse JSON
        json configJson;
        configFile >> configJson;

        // Example: read values
        if (configJson.contains("timer")) {
            int timer = configJson["timer"];
            std::cout << "Timer: " << timer << std::endl;
            globalTimer = timer;
        }

        if (configJson.contains("filter") && configJson["filter"].is_array()) {
            std::vector<std::string> filters;
            for (const auto& item : configJson["filter"]) {
                filters.push_back(item.get<std::string>());
            }

            std::cout << "Filters: ";
            for (const auto& f : filters)
                std::cout << f << " ";
            std::cout << std::endl;
            globalFilters = filters;
        }

        if (configJson.contains("enableTouch")) {
            bool enableTouch = configJson["enableTouch"];
            std::cout << "Enable Touch: " << (enableTouch ? "true" : "false") << std::endl;
            globalEnableTouch = enableTouch;
        }

        if (configJson.contains("showDate")) {
            bool showDate = configJson["showDate"];
            std::cout << "Show Date: " << (showDate ? "true" : "false") << std::endl;
            globalShowDate = showDate;
        }

        if (configJson.contains("showImgCount")) {
            bool showImgCount = configJson["showImgCount"];
            std::cout << "Show Image Count: " << (showImgCount ? "true" : "false") << std::endl;
            globalShowImgCount = showImgCount;
        }

        if (configJson.contains("showFolderName")) {
            bool showFolderName = configJson["showFolderName"];
            std::cout << "Show Folder Name: " << (showFolderName ? "true" : "false") << std::endl;
            globalShowFolderName = showFolderName;
        }

        return true; // Success!
    } catch (const std::exception& ex) {
        std::cerr << "Error loading settings: " << ex.what() << std::endl;
        return false;
    }
}

int main(){

    loadSettings("config.json");
    cv::namedWindow("Window", cv::WINDOW_NORMAL);
    cv::setWindowProperty("Window", cv::WND_PROP_FULLSCREEN, cv::WINDOW_FULLSCREEN);

    cv::setMouseCallback("Window", onMouse, nullptr);

    getOpenCVWindowHandle("Window");


    DisplayImg display;
    display.setFolderFilters(globalFilters);
    display.setShowDate(globalShowDate);
    display.setShowImgCount(globalShowImgCount);
    display.setShowFolderName(globalShowFolderName);

    std::vector<std::string> result = display.findImages();
    if(result.empty()){
        std::cout << "No Images found" <<std::endl;
        return -1;
    }else{
        std::cout << result.size() << " images have benn found" <<std::endl;
    }
    display.startPreloading();

    cv::Mat img = display.getNextImage();
    cv::imshow("Window", img);
    

    // Timer setup
    auto lastSwitchTime = std::chrono::steady_clock::now();
    const std::chrono::seconds switchInterval(globalTimer);  // 10 seconds

    while (true)
    {
        int key = cv::waitKey(10); // Wait for 10ms

        if (key == 27) // ESC key ASCII code
        {
            std::cout << "ESC pressed!" << std::endl;
            break;
        }

        auto now = std::chrono::steady_clock::now();
        bool timeElapsed = (now - lastSwitchTime) >= switchInterval;

        bool freezeTimer = isPressed; // freeze if mouse/touch is held
        bool triggerChange = pendingClick || (timeElapsed && !freezeTimer);

        if (triggerChange)
        {
            bool rightSide = true;

            if(clickX >= screenWidth/2){
                // Right side has been clicked
                rightSide = true;
            }else{
                // Lef side has been clicked
                rightSide = false;
            }

            if (pendingClick) {
                std::cout << "Mouse or touch clicked!" << std::endl;
                pendingClick = false;
                // Visual feedback: draw a small circle at click position
                cv::Mat feedback = img.clone();
                if(rightSide){
                    cv::circle(feedback, cv::Point(clickX, clickY), 10, cv::Scalar(255, 255, 255,0.5), 2); 
                }else{
                    cv::circle(feedback, cv::Point(clickX, clickY), 10, cv::Scalar(255, 0, 0,0.5), 2); 
                }

                cv::imshow("Window", feedback);
                cv::waitKey(100); // Show for 100ms

            } else {
                std::cout << "Auto-switch after 10 seconds!" << std::endl;
            }

            if(rightSide){
                img = display.getNextImage();
            }else{
                img = display.getNextImage(true);
            }
            if (!img.empty())
            {
                cv::imshow("Window", img);
                
                lastSwitchTime = std::chrono::steady_clock::now(); // Reset timer
            }
        }
    }

    cv::destroyAllWindows();

    return 0;
}