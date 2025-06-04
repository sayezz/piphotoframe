#include "DisplayImg.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <unordered_map>
using json = nlohmann::json;
namespace fs = std::filesystem;

DisplayImg::DisplayImg()
: stopThread(false)
{
    std::cout << "DisplayImg object created." << std::endl;
    folderFilter.push_back("Weihnachten");
    std::srand(static_cast<unsigned int>(std::time(0)));
    loadVisitedPathsFromJson();
}

DisplayImg::~DisplayImg()
{
    stopThread = true;
    if (preloadThread.joinable()){
        preloadThread.join();
    }
    std::cout << "DisplayImg object destroyed." << std::endl;
}

void DisplayImg::loadVisitedPathsFromJson() {
    std::ifstream file(dbFilePath);
    if (file.is_open()) {
        try {
            json j;
            file >> j;
            file.close();
            auto list = j.value("visitedPaths", std::vector<std::string>{});
            std::lock_guard<std::mutex> lock(visitedPathsMutex);
            visitedPaths.insert(list.begin(), list.end());
            std::cout << "Loaded " << visitedPaths.size() << " visited paths from db.json\n";
        } catch (const std::exception& e) {
            std::cerr << "Failed to parse db.json: " << e.what() << std::endl;
        }
    } else {
        std::cerr << "db.json not found. Starting with empty visitedPaths.\n";
    }
}

void DisplayImg::setFolderFilters(std::vector<std::string> folderFilter){
    this->folderFilter = folderFilter;
}


void DisplayImg::setShowDate(bool value){
    this->showDate = value;
}

std::vector<std::string> DisplayImg::findImages(){
    imagePaths.clear();
    std::vector<std::string> imageExtensions = { ".jpg", ".jpeg", ".png", ".bmp", ".tiff" };

    if(!fs::exists(folderPath)){
        std::cout <<"Folderpath: " << folderPath << " not found."<<std::endl;
        return imagePaths;
    }

    for (const auto& entry : fs::directory_iterator(folderPath)) {
        if (entry.is_directory()) {
            std::string folderName = entry.path().filename().string();
            
            // Check if folder is in the folderFilter
            if (folderFilter.empty() || std::find(folderFilter.begin(), folderFilter.end(), folderName) != folderFilter.end()) {
                // Scan this subfolder
                for (const auto& fileEntry : fs::recursive_directory_iterator(entry.path())) {
                    if (fileEntry.is_regular_file()) {
                        std::string ext = fileEntry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower); // lowercase extension
                        
                        if (std::find(imageExtensions.begin(), imageExtensions.end(), ext) != imageExtensions.end()) {
                            imagePaths.push_back(fileEntry.path().string());
                        }
                    }
                }
            }
        }
    }

    return imagePaths;
}

void DisplayImg::startPreloading()
{
    preloadThread = std::thread(&DisplayImg::preloadThreadFunc, this);
}

void DisplayImg::preloadThreadFunc()
{
    std::random_device rd;
    std::mt19937 gen(rd());

    while (!stopThread)
    {
        resetVisitedPathsIfNeeded();
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            if (imageBuffer.size() >= bufferSize)
            {
                queueCondVar.wait_for(lock, std::chrono::milliseconds(100));
                continue;
            }
        }

        if (imagePaths.empty())
            continue;

        // Select random unvisited image
        std::vector<std::string> availableImages;
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            for (const auto& path : imagePaths)
            {
                if (visitedPaths.find(path) == visitedPaths.end())
                {
                    availableImages.push_back(path);
                }
            }
        }

        if (availableImages.empty())
        {
            // No unvisited images left
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        std::uniform_int_distribution<> distr(0, availableImages.size() - 1);
        std::string randomPath = availableImages[distr(gen)];

        cv::Mat img = cv::imread(randomPath, cv::IMREAD_COLOR);
        if (!img.empty())
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            imageBuffer.push_back({randomPath, img});
            visitedPaths.insert(randomPath); // Mark as visited
            saveVisitedPathToJson(randomPath);
            queueCondVar.notify_one();
        }
        else
        {
            std::cerr << "Failed to load image: " << randomPath << std::endl;
        }
    }
}

void DisplayImg::saveVisitedPathToJson(const std::string& newPath) {
    std::ifstream inFile(dbFilePath);
    json j;

    // Load existing JSON if possible
    if (inFile.is_open()) {
        try {
            inFile >> j;
        } catch (...) {
            // If corrupted or empty, reinitialize
            j["visitedPaths"] = json::array();
        }
        inFile.close();
    } else {
        j["visitedPaths"] = json::array();
    }

    // Append only if not already present
    auto& visitedArray = j["visitedPaths"];
    if (std::find(visitedArray.begin(), visitedArray.end(), newPath) == visitedArray.end()) {
        visitedArray.push_back(newPath);

        std::ofstream outFile(dbFilePath);
        if (outFile.is_open()) {
            outFile << j.dump(4);
            outFile.close();
        } else {
            std::cerr << "Error: Cannot write to db.json\n";
        }
    }
}

void DisplayImg::resetVisitedPathsIfNeeded()
{
    if (visitedPaths.size() == imagePaths.size())
    {
        std::cout << "All images have been visited. Resetting visitedPaths." << std::endl;
        visitedPaths.clear(); // Reset visited paths after all images are visited

        // Clear visitedPaths in db.json
        try
        {
            std::ifstream inFile("db.json");
            if (!inFile.is_open()) {
                std::cerr << "Failed to open db.json for reading." << std::endl;
                return;
            }

            nlohmann::json db;
            inFile >> db;
            inFile.close();

            db["visitedPaths"] = nlohmann::json::array();  // Empty the array

            std::ofstream outFile("db.json");
            if (!outFile.is_open()) {
                std::cerr << "Failed to open db.json for writing." << std::endl;
                return;
            }

            outFile << db.dump(4); // Pretty print with 4-space indent
            outFile.close();
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error updating db.json: " << e.what() << std::endl;
        }
    
    }
}

cv::Mat DisplayImg::getNextImage(bool prev)
{
    std::unique_lock<std::mutex> lock(queueMutex);

    queueCondVar.wait(lock, [this]() { 
        return imageBuffer.size() >= 5 || stopThread; 
    });
    

    if (imageBuffer.empty())
    {
        return cv::Mat();
    }

    if(prev){
        if(currentBufferIndex > 0){
            currentBufferIndex--;
        }
    }else{
        if (currentBufferIndex < static_cast<int>(imageBuffer.size()) - 1) {
            currentBufferIndex++;
        } else {
            // Optionally block until more images loaded
            queueCondVar.wait_for(lock, std::chrono::milliseconds(100));
        }

        // Optional: Trim oldest images when we move too far forward
        if (currentBufferIndex >= bufferSize - 1 && imageBuffer.size() >= bufferSize) {
            imageBuffer.erase(imageBuffer.begin(), imageBuffer.begin() + 5);
            currentBufferIndex -= 5;
        }
    }

    if (currentBufferIndex >= static_cast<int>(imageBuffer.size()))
        return cv::Mat(); // Shouldn't happen, but safe guard

    auto pair = imageBuffer[currentBufferIndex];

    cv::Mat img = pair.second; // Return the cv::Mat
    std::string filePath = pair.first;
    
    if (!img.empty())
    {
        // Define your desired screen size here
        int screenWidth = 1920;
        int screenHeight = 1080;

        // Calculate aspect ratios
        double imgAspect = static_cast<double>(img.cols) / img.rows;
        double screenAspect = static_cast<double>(screenWidth) / screenHeight;

        int newWidth, newHeight;
        if (imgAspect > screenAspect)
        {
            // Image is wider
            newWidth = screenWidth;
            newHeight = static_cast<int>(screenWidth / imgAspect);
        }
        else
        {
            // Image is taller
            newHeight = screenHeight;
            newWidth = static_cast<int>(screenHeight * imgAspect);
        }

        // Resize the image
        cv::Mat resizedImg;
        cv::resize(img, resizedImg, cv::Size(newWidth, newHeight));

        // Create a black background image
        cv::Mat outputImg = cv::Mat::zeros(screenHeight, screenWidth, resizedImg.type());

        // Center the resized image onto the black background
        int x = (screenWidth - newWidth) / 2;
        int y = (screenHeight - newHeight) / 2;
        resizedImg.copyTo(outputImg(cv::Rect(x, y, newWidth, newHeight)));
        
        if(showDate){
            // This one shows the date and the folder name as one box
            writeDate(outputImg, filePath);
        }

        if(showImgCount){
            showImageCount(outputImg);
        }
        
        return outputImg;
    }else{
        return cv::Mat(); 
    }

}

void DisplayImg::showImageCount(cv::Mat& mat){
    if (mat.empty()) return; // Safety check

    // 1. Prepare the text
    std::string countText = std::to_string(visitedPaths.size()) + "/" + std::to_string(imagePaths.size());

    // 2. Text properties
    int fontFace = cv::FONT_HERSHEY_SIMPLEX;
    double fontScale = 0.7;  
    int thickness = 1;       
    int baseline = 0;

    // 3. Measure text size
    cv::Size textSize = cv::getTextSize(countText, fontFace, fontScale, thickness, &baseline);

    // 4. Random shift generator between -20 and +20
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(-20, 20);
    int shiftX = dist(rng);
    int shiftY = dist(rng);

    // 5. Calculate bottom-right position with padding
    int padding = 10;
    int x = mat.cols - textSize.width - padding + shiftX;
    int y = mat.rows - padding + shiftY;

    // 6. Clamp to stay inside visible area
    x = std::max(padding, std::min(x, mat.cols - textSize.width - padding));
    y = std::max(textSize.height + padding, std::min(y, mat.rows - padding));

    // 7. Draw a black transparent background (optional)
    cv::rectangle(mat,
                  cv::Point(x - 5, y - textSize.height - 5),
                  cv::Point(x + textSize.width + 5, y + 5),
                  cv::Scalar(0, 0, 0, 150), // Black, semi-transparent
                  cv::FILLED);

    // 8. Draw the text in white
    cv::putText(mat, countText, cv::Point(x, y), fontFace, fontScale, cv::Scalar(255, 255, 255), thickness, cv::LINE_AA);
}

void DisplayImg::setShowImgCount(bool value){
    this->showImgCount = value;
}

void DisplayImg::writeDate(cv::Mat& mat, std::string filePath)
{
    if (mat.empty() || filePath.empty()) return;

    std::string dateText = "Unknown date";

    try
    {
        Exiv2::Image::AutoPtr image = Exiv2::ImageFactory::open(filePath);
        if (image.get() != nullptr)
        {
            image->readMetadata();
            Exiv2::ExifData& exifData = image->exifData();

            if (!exifData.empty())
            {
                auto pos = exifData.findKey(Exiv2::ExifKey("Exif.Photo.DateTimeOriginal"));
                if (pos == exifData.end()) {
                    pos = exifData.findKey(Exiv2::ExifKey("Exif.Image.DateTime"));
                }
                if (pos != exifData.end())
                {
                    dateText = pos->toString();

                    // Format date into DD.MM.YYYY
                    std::istringstream iss(dateText);
                    std::tm tm = {};
                    iss >> std::get_time(&tm, "%Y:%m:%d %H:%M:%S");

                    if (!iss.fail()) {
                        std::ostringstream formattedDate;
                        formattedDate << std::setw(2) << std::setfill('0') << tm.tm_mday << "."
                                      << std::setw(2) << std::setfill('0') << (tm.tm_mon + 1) << "."
                                      << (tm.tm_year + 1900);
                        dateText = formattedDate.str();
                    }
                    else {
                        dateText = "Unknown date";
                    }
                }
            }
        }
    }
    catch (const Exiv2::Error& e)
    {
        std::cerr << "EXIF read error: " << e.what() << std::endl;
    }

    // --- Now extract the folder name ---
    std::string folderName;
    {
        std::filesystem::path path(filePath);
        folderName = path.parent_path().filename().string();
    }

    folderName = replaceUmlauts(folderName);

    // --- Small random shift ---
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(-20, 20);

    int shiftX = dis(gen);
    int shiftY = dis(gen);

    int baseX = 30;
    int baseY = 50;

    int posX = std::max(0, baseX + shiftX);
    int posY = std::max(30, baseY + shiftY);

    // --- Font settings ---
    int fontFace = cv::FONT_HERSHEY_SIMPLEX;
    double fontScale = 0.8;
    int thickness = 1;
    cv::Scalar textColor(255, 255, 255);

    // --- Measure text sizes ---
    int baseline1 = 0, baseline2 = 0;
    cv::Size dateSize = cv::getTextSize(dateText, fontFace, fontScale, thickness, &baseline1);
    cv::Size folderSize = cv::getTextSize(folderName, fontFace, fontScale, thickness, &baseline2);

    // Calculate the width of the larger text
    int fullWidth = std::max(dateSize.width, folderSize.width);
    int fullHeight = dateSize.height + folderSize.height + 10; // 10px gap between lines

    // --- Background rectangle ---
    cv::Rect backgroundRect(posX - 5, posY - dateSize.height - 5, fullWidth + 10, fullHeight + 10);
    backgroundRect &= cv::Rect(0, 0, mat.cols, mat.rows);

    // --- Draw semi-transparent black rounded background ---
    cv::Mat roi = mat(backgroundRect);
    cv::Mat overlay;
    roi.copyTo(overlay);

    int cornerRadius = 10;
    drawRoundedRectangle(mat, backgroundRect, cv::Scalar(0, 0, 0), cornerRadius, 0.5);

    double alpha = 0.5;
    cv::addWeighted(overlay, alpha, roi, 1.0 - alpha, 0, roi);

    // --- Draw the texts ---
    int lineSpacing = 5; // space between lines

    cv::putText(mat, dateText, cv::Point(posX, posY), fontFace, fontScale, textColor, thickness, cv::LINE_AA);

    if(showFldrName){
        cv::putText(mat, folderName, cv::Point(posX, posY + dateSize.height + lineSpacing), fontFace, fontScale, textColor, thickness, cv::LINE_AA);
    }

}

std::string DisplayImg::replaceUmlauts(const std::string& input) {
    std::unordered_map<char, std::string> replacements = {
        {'ä', "ae"}, {'ö', "oe"}, {'ü', "ue"},
        {'Ä', "Ae"}, {'Ö', "Oe"}, {'Ü', "Ue"},
        {'ß', "ss"}  // Optional: handle sharp S
    };

    std::string result;
    for (char c : input) {
        auto it = replacements.find(c);
        if (it != replacements.end()) {
            result += it->second;
        } else {
            result += c;
        }
    }
    return result;
}

void DisplayImg::setShowFolderName(bool value){
    this->showFldrName = value;
}
/*
void DisplayImg::showFolderName(cv::Mat& mat, std::string filePath){
    if (mat.empty() || filePath.empty()) return;

    // 1. Extract folder name
    std::filesystem::path path(filePath);
    std::string folderName = path.parent_path().filename().string(); // Get the parent folder name

    if (folderName.empty()) return; // Safety

    // 2. Random small shift
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(-20, 20);

    int shiftX = dis(gen);
    int shiftY = dis(gen);

    // 3. Base position (aligned with date text, but slightly lower)
    int baseX = 30;
    int baseY = 80; // lower than the date text (which was at ~50)

    int posX = std::max(0, baseX + shiftX);
    int posY = std::max(30, baseY + shiftY);

    // 4. Text style
    int fontFace = cv::FONT_HERSHEY_SIMPLEX;
    double fontScale = 0.8; // Slightly smaller than the date
    int thickness = 1;
    cv::Scalar textColor(255, 255, 255);

    // 5. Measure text size
    int baseline = 0;
    cv::Size textSize = cv::getTextSize(folderName, fontFace, fontScale, thickness, &baseline);

    // 6. Background rectangle
    cv::Rect backgroundRect(posX - 5, posY - textSize.height - 5, textSize.width + 10, textSize.height + 10);
    backgroundRect &= cv::Rect(0, 0, mat.cols, mat.rows); // Keep inside image

    // 7. Semi-transparent black rounded rectangle
    cv::Mat roi = mat(backgroundRect);
    cv::Mat overlay;
    roi.copyTo(overlay);

    int cornerRadius = 10;
    drawRoundedRectangle(mat, backgroundRect, cv::Scalar(0, 0, 0), cornerRadius, 0.5);

    double alpha = 0.5; // 50% transparent
    cv::addWeighted(overlay, alpha, roi, 1.0 - alpha, 0, roi);

    // 8. Draw the text
    cv::putText(mat, folderName, cv::Point(posX, posY), fontFace, fontScale, textColor, thickness, cv::LINE_AA);

}
*/
void DisplayImg::drawRoundedRectangle(cv::Mat& img, const cv::Rect& rect, const cv::Scalar& color, int radius, double alpha)
{
    cv::Mat roi = img(rect);

    // Create overlay for transparency
    cv::Mat overlay;
    roi.copyTo(overlay);

    // Create black background
    overlay.setTo(cv::Scalar(0, 0, 0));

    // Create a mask for rounded rectangle
    cv::Mat mask = cv::Mat::zeros(rect.height, rect.width, CV_8UC1);
    cv::rectangle(mask, cv::Rect(radius, 0, mask.cols - 2 * radius, mask.rows), 255, cv::FILLED);
    cv::rectangle(mask, cv::Rect(0, radius, mask.cols, mask.rows - 2 * radius), 255, cv::FILLED);

    cv::circle(mask, cv::Point(radius, radius), radius, 255, cv::FILLED);
    cv::circle(mask, cv::Point(mask.cols - radius, radius), radius, 255, cv::FILLED);
    cv::circle(mask, cv::Point(radius, mask.rows - radius), radius, 255, cv::FILLED);
    cv::circle(mask, cv::Point(mask.cols - radius, mask.rows - radius), radius, 255, cv::FILLED);

    // Apply mask to overlay
    overlay.copyTo(roi, mask);

    // Blend overlay and original roi
    cv::addWeighted(overlay, alpha, img(rect), 1.0 - alpha, 0, img(rect));
}