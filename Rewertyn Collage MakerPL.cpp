//echo install first this
//echo https://mirror.msys2.org/mingw/mingw64/mingw-w64-x86_64-libpng-1.6.50-1-any.pkg.tar.zst
//echo https://mirror.msys2.org/mingw/mingw64/mingw-w64-x86_64-zlib-1.3.1-1-any.pkg.tar.zst
//echo https://github.com/libjpeg-turbo/libjpeg-turbo/releases/tag/3.1.2

//g++ "Rewertyn Collage MakerPL.cpp" -o "Rewertyn Collage MakerPL.exe" -std=c++17 -D cimg_use_jpeg -D cimg_use_png -I C:/mingw64/include -L C:/mingw64/lib -mwindows -pthread -static -static-libgcc -static-libstdc++ -luser32 -lgdi32 -lcomctl32 -lshlwapi -lole32 -ljpeg -lpng -lz

//MIT License
//Copyright (c) 2025 Marcin Matysek (RewertynPL)

#include <windows.h>
#include <commctrl.h>
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <thread>
#include <stdexcept>
#include <shellapi.h>
#include <tchar.h>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <io.h>
#include <fstream>
#include <mutex>
#include <shlobj.h> // Potrzebne do SHCreateDirectoryExW

#define cimg_display 0 
#include "CImg.h"

using namespace cimg_library;
namespace fs = std::filesystem;

// --- ID KONTROLEK ---
#define IDC_BTN_SELECT_DIR 101
#define IDC_EDT_INPUT_DIR 102
#define IDC_EDT_NUM_COLLAGES 103
#define IDC_EDT_COLLAGE_SIZE 104
#define IDC_EDT_SPACING 105
#define IDC_BTN_START 106
#define IDC_LST_LOG 107
#define IDC_CHK_PNG_OUTPUT 108
#define IDC_BTN_SELECT_COLOR 109
#define IDC_EDT_COLOR_PREVIEW 110
#define IDC_EDT_BATCH_REDUCTION 111
#define IDC_LBL_STATUS 112
#define IDC_LBL_COUNTER 113
#define IDC_RB_MODE_UNIFORM 114
#define IDC_RB_MODE_SMART 115
#define IDC_RB_MODE_PACKED 116
#define IDC_RB_MODE_MIXED 117 

// Definicja wlasnych komunikatow
#define WM_LOG_MESSAGE (WM_USER + 1)
#define WM_UPDATE_STATUS (WM_USER + 2)
#define WM_UPDATE_COUNTER (WM_USER + 3)
#define WM_PROCESSING_FINISHED (WM_USER + 4)

const char g_szClassName[] = "CollageMakerClass";

HWND g_hInputEdit, g_hNumCollagesEdit, g_hCollageSizeEdit, g_hSpacingEdit, g_hLogList, g_hPngCheckbox, g_hColorPreview, g_hBatchReductionEdit, g_hStatusLabel, g_hCounterLabel;
HWND g_hRbUniform, g_hRbSmart, g_hRbPacked, g_hRbMixed; 
HWND g_hMainWindow;

std::wstring g_inputDirPathW = L"";
std::thread g_workerThread;

// LOGOWANIE
std::ofstream g_logFileStream;
std::mutex g_logMutex;

// ZMIENNE KONTROLNE
std::atomic<bool> g_isProcessing(false);
std::atomic<bool> g_stopRequested(false); 
std::atomic<int> g_totalCollagesMade(0); 

std::string OUTPUT_DIR_NAME = "output_collage";
std::string TEMP_DIR_NAME = "temp_image_processing_cimg";
int NUM_SQUARE_COLLAGES_PER_DIR = 8;
int SQUARE_COLLAGE_SIDE_LENGTH = 4000;
int TILE_SPACING = 30;
unsigned char BACKGROUND_COLOR[] = {255, 255, 255};
const int JPG_QUALITY = 92;
const std::vector<std::string> IMAGE_EXTENSIONS = {
    ".jpg", ".jpeg", ".png", ".bmp", ".tiff", ".tif", ".gif", ".webp"
};
std::atomic<int> g_tempFileCounter(0);
bool g_usePngOutput = false; 

// Tryby pracy
enum GenMode { MODE_UNIFORM, MODE_SMART, MODE_PACKED, MODE_MIXED };
GenMode g_currentMode = MODE_UNIFORM;

// --- HELPERY UNICODE ---
std::string WStringToString(const std::wstring& s) {
    if (s.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &s[0], (int)s.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &s[0], (int)s.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

void LogToFileAndConsole(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::cout << "[LOG] " << msg << std::endl;
    if (g_logFileStream.is_open()) {
        g_logFileStream << "[LOG] " << msg << std::endl;
        g_logFileStream.flush();
    }
}

void LogMessage(const std::string& msg) {
    LogToFileAndConsole(msg);
    SendMessageA(g_hLogList, LB_ADDSTRING, 0, (LPARAM)msg.c_str());
    SendMessageA(g_hLogList, LB_SETTOPINDEX, SendMessage(g_hLogList, LB_GETCOUNT, 0, 0) - 1, 0);
}

std::string GetSafePathForCImg(const std::wstring& wpath) {
    long length = GetShortPathNameW(wpath.c_str(), NULL, 0);
    if (length == 0) return WStringToString(wpath); 
    std::vector<wchar_t> buffer(length);
    GetShortPathNameW(wpath.c_str(), &buffer[0], length);
    std::wstring shortW(buffer.begin(), buffer.end() - 1);
    std::string result(shortW.begin(), shortW.end());
    return result;
}

std::vector<std::wstring> GetFilesInDirWinAPI(const std::wstring& dirPath) {
    std::vector<std::wstring> files;
    std::wstring searchPath = dirPath + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                files.push_back(dirPath + L"\\" + fd.cFileName);
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
    return files;
}

void GetSubDirsRecursive(const std::wstring& dirPath, std::vector<std::wstring>& outDirs, const std::wstring& ignore1, const std::wstring& ignore2) {
    outDirs.push_back(dirPath); 
    std::wstring searchPath = dirPath + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0) {
                    std::wstring fullPath = dirPath + L"\\" + fd.cFileName;
                    if (fullPath.find(ignore1) == std::wstring::npos && fullPath.find(ignore2) == std::wstring::npos) {
                         GetSubDirsRecursive(fullPath, outDirs, ignore1, ignore2);
                    }
                }
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
}

std::wstring BrowseForFolderW(HWND hwnd) {
    BROWSEINFOW bi = {0};
    bi.hwndOwner = hwnd;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    std::wstring path = L"";
    if (pidl != 0) {
        wchar_t buffer[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, buffer)) {
            path = buffer;
        }
        CoTaskMemFree(pidl);
    }
    return path;
}

void ChooseBackgroundColor(HWND hwnd) {
    CHOOSECOLOR cc;
    COLORREF acrCustColors[16];
    ZeroMemory(&cc, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hwnd;
    cc.lpCustColors = acrCustColors;
    cc.rgbResult = RGB(BACKGROUND_COLOR[0], BACKGROUND_COLOR[1], BACKGROUND_COLOR[2]);
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColor(&cc)) {
        BACKGROUND_COLOR[0] = GetRValue(cc.rgbResult);
        BACKGROUND_COLOR[1] = GetGValue(cc.rgbResult);
        BACKGROUND_COLOR[2] = GetBValue(cc.rgbResult);
        InvalidateRect(g_hColorPreview, NULL, TRUE);
    }
}

void DrawColorPreview(HDC hdc) {
    RECT rect;
    GetClientRect(g_hColorPreview, &rect);
    HBRUSH hBrush = CreateSolidBrush(RGB(BACKGROUND_COLOR[0], BACKGROUND_COLOR[1], BACKGROUND_COLOR[2]));
    FillRect(hdc, &rect, hBrush);
    DeleteObject(hBrush);
    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
    HGDIOBJ hOldPen = SelectObject(hdc, hPen);
    MoveToEx(hdc, rect.left, rect.top, NULL);
    LineTo(hdc, rect.right - 1, rect.top);
    LineTo(hdc, rect.right - 1, rect.bottom - 1);
    LineTo(hdc, rect.left, rect.bottom - 1);
    LineTo(hdc, rect.left, rect.top);
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
}

// --- PROCESSING ---
std::wstring processImageForCollage(
    const std::wstring& inputPathW,
    const fs::path& globalTempDir,
    int targetCellSize,
    std::vector<std::wstring>& tempFilesCreated,
    bool usePng,
    const unsigned char* bgColor
) {
    std::string safeInputPath = "";
    try {
        safeInputPath = GetSafePathForCImg(inputPathW);
        if (safeInputPath.empty()) safeInputPath = WStringToString(inputPathW); 
        
        CImg<unsigned char> src(safeInputPath.c_str());

        if (usePng) {
            if (src.spectrum() == 1) { 
                 src.resize(-100, -100, 1, 3); 
                 src.resize(-100, -100, 1, 4); 
                 src.get_shared_channel(3).fill(255);
            } else if (src.spectrum() == 3) { 
                src.resize(-100, -100, 1, 4); 
                src.get_shared_channel(3).fill(255); 
            }
        } else {
            if (src.spectrum() == 4) {
                CImg<unsigned char> rgb(src.width(), src.height(), 1, 3, bgColor[0]); 
                rgb.draw_rectangle(0, 0, src.width() - 1, src.height() - 1, bgColor); 
                cimg_forXY(src, x, y) {
                    unsigned char r = src(x, y, 0, 0);
                    unsigned char g = src(x, y, 0, 1);
                    unsigned char b = src(x, y, 0, 2);
                    unsigned char a = src(x, y, 0, 3);
                    if (a > 0) { 
                        float alpha = a / 255.0f;
                        unsigned char bg_r = rgb(x, y, 0, 0);
                        unsigned char bg_g = rgb(x, y, 0, 1);
                        unsigned char bg_b = rgb(x, y, 0, 2);
                        rgb(x, y, 0, 0) = static_cast<unsigned char>(bg_r * (1 - alpha) + r * alpha);
                        rgb(x, y, 0, 1) = static_cast<unsigned char>(bg_g * (1 - alpha) + g * alpha);
                        rgb(x, y, 0, 2) = static_cast<unsigned char>(bg_b * (1 - alpha) + b * alpha);
                    }
                }
                src = rgb;
            }
        }
        
        int w = src.width();
        int h = src.height();
        int max_dim = std::max(w, h);
        int spectrum = usePng ? 4 : 3;

        CImg<unsigned char> square_canvas;
        square_canvas.assign(max_dim, max_dim, 1, spectrum, 0); 
        
        if (!usePng) square_canvas.draw_rectangle(0, 0, max_dim - 1, max_dim - 1, bgColor);
        
        int x_offset = (max_dim - w) / 2;
        int y_offset = (max_dim - h) / 2;
        square_canvas.draw_image(x_offset, y_offset, src);
        square_canvas.resize(targetCellSize, targetCellSize);

        std::string ext = usePng ? ".png" : ".jpg";
        std::string tempFileName = "tile_" + std::to_string(g_tempFileCounter++) + ext;
        fs::path tempOutputPath = globalTempDir / tempFileName;
        
        std::string safeTempPath = GetSafePathForCImg(tempOutputPath.wstring());
        if (safeTempPath.empty()) safeTempPath = tempOutputPath.string(); 

        if (usePng) square_canvas.save_png(safeTempPath.c_str());
        else square_canvas.save_jpeg(safeTempPath.c_str(), JPG_QUALITY);

        std::wstring wTempPath = tempOutputPath.wstring();
        tempFilesCreated.push_back(wTempPath);
        return wTempPath;

    } catch (const CImgException& e) {
        std::string errMsg = "[CIMG LOAD ERR] " + safeInputPath + " : " + std::string(e.what());
        LogToFileAndConsole(errMsg);
        return L"";
    } catch (...) {
        LogToFileAndConsole("[UNKNOWN FATAL] In processImageForCollage");
        return L"";
    }
}

void createFinalCollage(const std::vector<std::wstring>& tilePaths, const fs::path& finalPath, int gridDim, int cellSize) {
    if (tilePaths.empty()) return;

    std::string statusMsg = "Saving collage (" + std::to_string(gridDim) + "x" + std::to_string(gridDim) + ")...";
    PostMessage(g_hMainWindow, WM_UPDATE_STATUS, 0, (LPARAM)new std::string(statusMsg));

    try {
        int spectrum = g_usePngOutput ? 4 : 3;
        CImg<unsigned char> collage(SQUARE_COLLAGE_SIDE_LENGTH, SQUARE_COLLAGE_SIDE_LENGTH, 1, spectrum, g_usePngOutput ? 0 : 255);
        if (!g_usePngOutput) collage.draw_rectangle(0, 0, SQUARE_COLLAGE_SIDE_LENGTH - 1, SQUARE_COLLAGE_SIDE_LENGTH - 1, BACKGROUND_COLOR);

        int totalTiles = tilePaths.size();
        int actualRows = (int)std::ceil( (double)totalTiles / gridDim );
        
        int collageContentWidth = gridDim * cellSize + (gridDim > 0 ? (gridDim - 1) * TILE_SPACING : 0);
        int collageContentHeight = actualRows * cellSize + (actualRows > 0 ? (actualRows - 1) * TILE_SPACING : 0);
        
        int center_x_offset = (SQUARE_COLLAGE_SIDE_LENGTH - collageContentWidth) / 2;
        int center_y_offset = (SQUARE_COLLAGE_SIDE_LENGTH - collageContentHeight) / 2;
        
        if (center_x_offset < 0) center_x_offset = 0;
        if (center_y_offset < 0) center_y_offset = 0;

        for (size_t i = 0; i < tilePaths.size(); ++i) {
            if (g_stopRequested) return;
            int row = i / gridDim;
            int col = i % gridDim;
            int x_offset = center_x_offset + col * (cellSize + TILE_SPACING);
            int y_offset = center_y_offset + row * (cellSize + TILE_SPACING);

            std::string safeTilePath = GetSafePathForCImg(tilePaths[i]);
            if (safeTilePath.empty()) safeTilePath = WStringToString(tilePaths[i]);

            CImg<unsigned char> tile(safeTilePath.c_str());
            collage.draw_image(x_offset, y_offset, tile);
        }
        
        if (g_stopRequested) return;

        std::wstring wFinalPath = finalPath.wstring();
        HANDLE hFile = CreateFileW(wFinalPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        std::string safeFinalPath;
        if (hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(hFile); 
            safeFinalPath = GetSafePathForCImg(wFinalPath); 
        } else {
             safeFinalPath = finalPath.string(); 
        }

        if (g_usePngOutput) collage.save_png(safeFinalPath.c_str());
        else collage.save_jpeg(safeFinalPath.c_str(), JPG_QUALITY);
        
        g_totalCollagesMade++;
        PostMessage(g_hMainWindow, WM_UPDATE_COUNTER, 0, 0);
        std::string successMsg = "SUCCESS: " + finalPath.filename().string();
        PostMessage(g_hMainWindow, WM_LOG_MESSAGE, 0, (LPARAM)new std::string(successMsg));

    } catch (const CImgException& e) {
        std::string errMsg = "[CIMG SAVE ERROR] " + std::string(e.what());
        LogToFileAndConsole(errMsg);
        PostMessage(g_hMainWindow, WM_LOG_MESSAGE, 0, (LPARAM)new std::string(errMsg));
    } catch (...) {
        LogToFileAndConsole("[UNKNOWN SAVE ERROR]");
    }
}

void processDirectory(const std::wstring& dirPathW, const fs::path& outputBaseDir, const fs::path& globalTempDir) {
    if (g_stopRequested) return;

    // --- REKONSTRUKCJA ŚCIEŻKI RELATYWNEJ DLA ZACHOWANIA DRZEWA FOLDERÓW ---
    // 1. Sprawdzamy czy ścieżka zaczyna się od wybranego przez użytkownika folderu
    std::wstring relPath = L"";
    if (dirPathW.length() >= g_inputDirPathW.length()) {
        if (dirPathW.substr(0, g_inputDirPathW.length()) == g_inputDirPathW) {
            relPath = dirPathW.substr(g_inputDirPathW.length());
            // Usuń ewentualny slash na początku
            if (!relPath.empty() && (relPath[0] == L'\\' || relPath[0] == L'/')) {
                relPath = relPath.substr(1);
            }
        }
    }
    
    // Jeśli z jakiegoś powodu relPath jest pusta (np. skanujemy sam root), to nazwa folderu to "" (wsadź prosto do output)
    // lub nazwa samego folderu. W tym przypadku chcemy zachować strukturę WEWNĄTRZ outputu.
    
    fs::path targetPath;
    if (relPath.empty()) {
        targetPath = outputBaseDir; // Root
    } else {
        targetPath = outputBaseDir / relPath;
    }

    // Tworzenie pełnego drzewa katalogów (mkdir -p) przy użyciu WinAPI (Unicode Safe)
    int res = SHCreateDirectoryExW(NULL, targetPath.wstring().c_str(), NULL);
    // Ignorujemy błędy, bo folder może już istnieć (ERROR_ALREADY_EXISTS)

    // Logowanie dla usera (żeby widział co się dzieje)
    size_t lastSlash = dirPathW.find_last_of(L"\\/");
    std::wstring dirNameW = (lastSlash == std::wstring::npos) ? dirPathW : dirPathW.substr(lastSlash + 1);
    std::string startMsg = "\n--- Dir: " + WStringToString(dirNameW) + " ---";
    PostMessage(g_hMainWindow, WM_LOG_MESSAGE, 0, (LPARAM)new std::string(startMsg));
    
    std::vector<std::wstring> allImagePaths = GetFilesInDirWinAPI(dirPathW);
    
    std::vector<std::wstring> filteredImages;
    for(const auto& pathW : allImagePaths) {
        size_t dot = pathW.find_last_of(L".");
        if (dot != std::wstring::npos) {
            std::string ext = WStringToString(pathW.substr(dot));
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
            for (const auto& allowed : IMAGE_EXTENSIONS) {
                if (ext == allowed) {
                    filteredImages.push_back(pathW);
                    break;
                }
            }
        }
    }

    if (filteredImages.empty()) {
        LogToFileAndConsole("No images in folder.");
        return;
    }
    
    std::sort(filteredImages.begin(), filteredImages.end());

    size_t totalImages = filteredImages.size();
    size_t totalProcessed = 0;
    
    // Używamy nazwy folderu (tylko liścia) jako prefiksu pliku, żeby było ładnie
    // Ale zapisujemy w odtworzonej strukturze (targetPath)
    std::string filePrefix = WStringToString(dirNameW);
    if (filePrefix.empty()) filePrefix = "collage";

    // --- LOGIKA TRYBÓW ---

    if (g_currentMode == MODE_MIXED) {
        LogToFileAndConsole("Mode: Mixed Grids (Exact Count)");
        
        int K = NUM_SQUARE_COLLAGES_PER_DIR;
        if (K > (int)totalImages) K = (int)totalImages;
        if (K < 1) K = 1;

        int avg = totalImages / K;
        int s = (int)floor(std::sqrt((double)avg));
        if (s < 1) s = 1;

        int smallGridDim = s;
        int bigGridDim = s + 1;
        int smallCap = smallGridDim * smallGridDim;
        int bigCap = bigGridDim * bigGridDim;
        int usedIfAllSmall = K * smallCap;
        int imagesLeft = (int)totalImages - usedIfAllSmall;
        int diff = bigCap - smallCap;

        int numBig = 0;
        if (diff > 0 && imagesLeft > 0) numBig = imagesLeft / diff;
        if (numBig > K) numBig = K;
        int numSmall = K - numBig;

        LogToFileAndConsole("Plan: " + std::to_string(numBig) + " large, " + std::to_string(numSmall) + " small.");

        for (int i = 0; i < numBig; ++i) {
            if (g_stopRequested) return;
            if (totalProcessed >= totalImages) break;
            int batchSize = bigCap;
            if (totalProcessed + batchSize > totalImages) batchSize = totalImages - totalProcessed;
            std::vector<std::wstring> batchPaths(filteredImages.begin() + totalProcessed, filteredImages.begin() + totalProcessed + batchSize);
            int gridDim = bigGridDim;
            int cellSize = (SQUARE_COLLAGE_SIDE_LENGTH - (gridDim - 1) * TILE_SPACING) / gridDim;
            if (cellSize <= 0) cellSize = SQUARE_COLLAGE_SIDE_LENGTH;
            std::vector<std::wstring> tilePaths;
            std::vector<std::wstring> tempFilesForThisCollage;
            for (const auto& p : batchPaths) {
                if (g_stopRequested) return;
                std::wstring tilePath = processImageForCollage(p, globalTempDir, cellSize, tempFilesForThisCollage, g_usePngOutput, BACKGROUND_COLOR);
                if (!tilePath.empty()) tilePaths.push_back(tilePath);
            }
            if (!tilePaths.empty()) {
                std::string ext = g_usePngOutput ? ".png" : ".jpg";
                std::string collageFileName = filePrefix + "_mixed_L_" + std::to_string(i + 1) + "_grid" + std::to_string(gridDim) + "x" + std::to_string(gridDim) + ext;
                fs::path outputCollageFile = targetPath / collageFileName;
                createFinalCollage(tilePaths, outputCollageFile, gridDim, cellSize);
            }
            for (const auto& tempFile : tempFilesForThisCollage) DeleteFileW(tempFile.c_str());
            totalProcessed += batchSize;
        }

        for (int i = 0; i < numSmall; ++i) {
            if (g_stopRequested) return;
            if (totalProcessed >= totalImages) break;
            int batchSize = smallCap;
            if (totalProcessed + batchSize > totalImages) batchSize = totalImages - totalProcessed;
            std::vector<std::wstring> batchPaths(filteredImages.begin() + totalProcessed, filteredImages.begin() + totalProcessed + batchSize);
            int gridDim = smallGridDim;
            int cellSize = (SQUARE_COLLAGE_SIDE_LENGTH - (gridDim - 1) * TILE_SPACING) / gridDim;
            if (cellSize <= 0) cellSize = SQUARE_COLLAGE_SIDE_LENGTH;
            std::vector<std::wstring> tilePaths;
            std::vector<std::wstring> tempFilesForThisCollage;
            for (const auto& p : batchPaths) {
                if (g_stopRequested) return;
                std::wstring tilePath = processImageForCollage(p, globalTempDir, cellSize, tempFilesForThisCollage, g_usePngOutput, BACKGROUND_COLOR);
                if (!tilePath.empty()) tilePaths.push_back(tilePath);
            }
            if (!tilePaths.empty()) {
                std::string ext = g_usePngOutput ? ".png" : ".jpg";
                std::string collageFileName = filePrefix + "_mixed_S_" + std::to_string(i + 1) + "_grid" + std::to_string(gridDim) + "x" + std::to_string(gridDim) + ext;
                fs::path outputCollageFile = targetPath / collageFileName;
                createFinalCollage(tilePaths, outputCollageFile, gridDim, cellSize);
            }
            for (const auto& tempFile : tempFilesForThisCollage) DeleteFileW(tempFile.c_str());
            totalProcessed += batchSize;
        }

    } else if (g_currentMode == MODE_PACKED) {
        LogToFileAndConsole("Mode: Packed Grid");
        int batchCounter = 1;
        while (totalProcessed < totalImages) {
            if (g_stopRequested) return;
            size_t remaining = totalImages - totalProcessed;
            int batchSize = 1;
            int gridDim = 1;
            if (remaining >= 4) { batchSize = 4; gridDim = 2; } 
            else { batchSize = 1; gridDim = 1; }

            std::vector<std::wstring> batchPaths(filteredImages.begin() + totalProcessed, filteredImages.begin() + totalProcessed + batchSize);
            int cellSize = (SQUARE_COLLAGE_SIDE_LENGTH - (gridDim - 1) * TILE_SPACING) / gridDim;
            std::vector<std::wstring> tilePaths;
            std::vector<std::wstring> tempFilesForThisCollage;
            
            for (const auto& p : batchPaths) {
                if (g_stopRequested) return;
                std::wstring tilePath = processImageForCollage(p, globalTempDir, cellSize, tempFilesForThisCollage, g_usePngOutput, BACKGROUND_COLOR);
                if (!tilePath.empty()) tilePaths.push_back(tilePath);
            }
            if (!tilePaths.empty()) {
                std::string ext = g_usePngOutput ? ".png" : ".jpg";
                std::string collageFileName = filePrefix + "_packed_" + std::to_string(batchCounter++) + "_grid" + std::to_string(gridDim) + "x" + std::to_string(gridDim) + ext;
                fs::path outputCollageFile = targetPath / collageFileName;
                createFinalCollage(tilePaths, outputCollageFile, gridDim, cellSize);
            }
            for (const auto& tempFile : tempFilesForThisCollage) DeleteFileW(tempFile.c_str());
            totalProcessed += batchSize;
        }

    } else if (g_currentMode == MODE_SMART) {
        int targetCollages = NUM_SQUARE_COLLAGES_PER_DIR;
        if ((size_t)targetCollages > totalImages) targetCollages = (int)totalImages;
        if (targetCollages < 1) targetCollages = 1;
        for (int i = 0; i < targetCollages; ++i) {
            if (g_stopRequested) return;
            size_t remainingCollages = targetCollages - i;
            size_t remainingImages = totalImages - totalProcessed;
            size_t numForThisCollage = remainingImages / remainingCollages;
            if (numForThisCollage == 0 && remainingImages > 0) numForThisCollage = 1;
            std::vector<std::wstring> batchPaths(filteredImages.begin() + totalProcessed, filteredImages.begin() + totalProcessed + numForThisCollage);
            int gridDim = (int)std::ceil(std::sqrt((double)numForThisCollage));
            if (gridDim == 0) gridDim = 1;
            int cellSize = (SQUARE_COLLAGE_SIDE_LENGTH - (gridDim - 1) * TILE_SPACING) / gridDim;
            std::vector<std::wstring> tilePaths;
            std::vector<std::wstring> tempFilesForThisCollage;
            for (const auto& p : batchPaths) {
                if (g_stopRequested) return;
                std::wstring tilePath = processImageForCollage(p, globalTempDir, cellSize, tempFilesForThisCollage, g_usePngOutput, BACKGROUND_COLOR);
                if (!tilePath.empty()) tilePaths.push_back(tilePath);
            }
            if (!tilePaths.empty()) {
                std::string ext = g_usePngOutput ? ".png" : ".jpg";
                std::string collageFileName = filePrefix + "_var_" + std::to_string(i + 1) + "_grid" + std::to_string(gridDim) + "x" + std::to_string(gridDim) + ext;
                fs::path outputCollageFile = targetPath / collageFileName;
                createFinalCollage(tilePaths, outputCollageFile, gridDim, cellSize);
            }
            for (const auto& tempFile : tempFilesForThisCollage) DeleteFileW(tempFile.c_str());
            totalProcessed += numForThisCollage;
        }

    } else {
        size_t approxTilesPerCollage = static_cast<size_t>(std::ceil(static_cast<double>(totalImages) / NUM_SQUARE_COLLAGES_PER_DIR));
        int optimalGridDim = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(approxTilesPerCollage))));
        if (optimalGridDim == 0) optimalGridDim = 1;
        size_t optimalTilesPerCollage = optimalGridDim * optimalGridDim;
        int actualNumCollages = static_cast<int>(std::ceil(static_cast<double>(totalImages) / optimalTilesPerCollage));
        if (actualNumCollages == 0) actualNumCollages = 1;
        for (int i = 0; i < actualNumCollages; ++i) {
            if (g_stopRequested) return; 
            size_t remainingImages = totalImages - totalProcessed;
            if (remainingImages == 0) break;
            size_t numForThisCollage = std::min(remainingImages, optimalTilesPerCollage);
            std::vector<std::wstring> batchPaths(filteredImages.begin() + totalProcessed, filteredImages.begin() + totalProcessed + numForThisCollage);
            int gridDim = optimalGridDim; 
            int cellSize = (SQUARE_COLLAGE_SIDE_LENGTH - (gridDim - 1) * TILE_SPACING) / gridDim;
            std::vector<std::wstring> tilePaths;
            std::vector<std::wstring> tempFilesForThisCollage;
            for (const auto& p : batchPaths) {
                if (g_stopRequested) return;
                std::wstring tilePath = processImageForCollage(p, globalTempDir, cellSize, tempFilesForThisCollage, g_usePngOutput, BACKGROUND_COLOR);
                if (!tilePath.empty()) tilePaths.push_back(tilePath);
            }
            if (!tilePaths.empty()) {
                std::string ext = g_usePngOutput ? ".png" : ".jpg";
                std::string collageFileName = filePrefix + "_sq_grid_" + std::to_string(i + 1) + ext;
                fs::path outputCollageFile = targetPath / collageFileName;
                createFinalCollage(tilePaths, outputCollageFile, gridDim, cellSize);
            }
            for (const auto& tempFile : tempFilesForThisCollage) DeleteFileW(tempFile.c_str());
            totalProcessed += numForThisCollage;
        }
    }
}

void WorkerThreadFunction() {
    g_isProcessing = true;
    g_stopRequested = false; 
    g_totalCollagesMade = 0; 
    PostMessage(g_hMainWindow, WM_UPDATE_COUNTER, 0, 0); 
    LogToFileAndConsole("Worker Thread Started");
    EnableWindow(GetDlgItem(g_hMainWindow, IDC_BTN_START), FALSE);
    
    std::wstring startPathW = g_inputDirPathW;
    fs::path current_dir = fs::current_path();
    fs::path outputDir = fs::absolute(current_dir / OUTPUT_DIR_NAME).lexically_normal(); 
    fs::path globalTempDir = fs::absolute(current_dir / TEMP_DIR_NAME).lexically_normal();
    
    std::wstring outputDirW = outputDir.wstring();
    std::wstring tempDirW = globalTempDir.wstring();

    PostMessage(g_hMainWindow, WM_LOG_MESSAGE, 0, (LPARAM)new std::string("Scanning directories..."));
    
    try {
        fs::create_directories(outputDir);
        if (fs::exists(globalTempDir)) fs::remove_all(globalTempDir);
        fs::create_directories(globalTempDir);
    } catch (...) {
        LogToFileAndConsole("Error creating setup dirs");
    }
    
    std::vector<std::wstring> DirsToProcess;
    GetSubDirsRecursive(startPathW, DirsToProcess, outputDirW, tempDirW);

    LogToFileAndConsole("Found " + std::to_string(DirsToProcess.size()) + " directories.");

    for(const auto& dirW : DirsToProcess) {
        if (g_stopRequested) break;
        if (dirW.find(outputDirW) != std::wstring::npos) continue;
        if (dirW.find(tempDirW) != std::wstring::npos) continue;
        processDirectory(dirW, outputDir, globalTempDir);
    }

    if (g_stopRequested) {
        PostMessage(g_hMainWindow, WM_LOG_MESSAGE, 0, (LPARAM)new std::string("\n!!! STOPPED BY USER !!!"));
        PostMessage(g_hMainWindow, WM_UPDATE_STATUS, 0, (LPARAM)new std::string("Stopped."));
    } else {
        PostMessage(g_hMainWindow, WM_LOG_MESSAGE, 0, (LPARAM)new std::string("\nDone."));
        PostMessage(g_hMainWindow, WM_UPDATE_STATUS, 0, (LPARAM)new std::string("All tasks finished."));
    }
    
    try {
        if (fs::exists(globalTempDir)) fs::remove_all(globalTempDir);
    } catch (...) {}
    
    g_isProcessing = false;
    LogToFileAndConsole("Worker Thread Finished");
    PostMessage(g_hMainWindow, WM_PROCESSING_FINISHED, 0, 0); 
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_CREATE: {
            AllocConsole();
            FILE* fp; freopen_s(&fp, "CONOUT$", "w", stdout);
            FILE* fp_err; freopen_s(&fp_err, "CONOUT$", "w", stderr);
            g_logFileStream.open("debug_log.txt", std::ios::out | std::ios::trunc);
            LogToFileAndConsole("--- APP START: TREE STRUCTURE SUPPORT ---");

            INITCOMMONCONTROLSEX icex;
            icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
            icex.dwICC = ICC_STANDARD_CLASSES;
            InitCommonControlsEx(&icex);
            g_hMainWindow = hwnd;

            CreateWindowA("STATIC", "1. Input Directory:", WS_VISIBLE | WS_CHILD, 10, 10, 150, 20, hwnd, NULL, NULL, NULL);
            g_hInputEdit = CreateWindowA("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 10, 30, 300, 25, hwnd, (HMENU)IDC_EDT_INPUT_DIR, NULL, NULL);
            CreateWindowA("BUTTON", "Select Folder", WS_VISIBLE | WS_CHILD, 320, 30, 120, 25, hwnd, (HMENU)IDC_BTN_SELECT_DIR, NULL, NULL);

            CreateWindowA("STATIC", "2. Output Settings:", WS_VISIBLE | WS_CHILD, 10, 70, 150, 20, hwnd, NULL, NULL, NULL);
            
            // --- MODES ---
            CreateWindowA("BUTTON", "Generation Mode:", WS_VISIBLE | WS_CHILD | BS_GROUPBOX, 10, 95, 430, 95, hwnd, NULL, NULL, NULL);
            
            g_hRbUniform = CreateWindowA("BUTTON", "Uniform Grid (Standard)", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP, 20, 115, 180, 20, hwnd, (HMENU)IDC_RB_MODE_UNIFORM, NULL, NULL);
            g_hRbSmart = CreateWindowA("BUTTON", "Smart Fit (Force exact # - old)", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON, 210, 115, 220, 20, hwnd, (HMENU)IDC_RB_MODE_SMART, NULL, NULL);
            g_hRbPacked = CreateWindowA("BUTTON", "Packed Grid (No empty spaces)", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON, 20, 140, 200, 20, hwnd, (HMENU)IDC_RB_MODE_PACKED, NULL, NULL);
            g_hRbMixed = CreateWindowA("BUTTON", "Mixed Grids (Exact Count - BEST)", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON, 210, 140, 220, 20, hwnd, (HMENU)IDC_RB_MODE_MIXED, NULL, NULL);

            SendMessage(g_hRbUniform, BM_SETCHECK, BST_CHECKED, 0);

            int yOffset = 105; // Przesunięcie UI w dół
            CreateWindowA("STATIC", "Collages/Dir (8):", WS_VISIBLE | WS_CHILD, 10, 95 + yOffset, 120, 20, hwnd, NULL, NULL, NULL);
            g_hNumCollagesEdit = CreateWindowA("EDIT", "8", WS_VISIBLE | WS_CHILD | WS_BORDER, 130, 95 + yOffset, 60, 20, hwnd, (HMENU)IDC_EDT_NUM_COLLAGES, NULL, NULL);
            CreateWindowA("STATIC", "Collage Size (4000):", WS_VISIBLE | WS_CHILD, 200, 95 + yOffset, 120, 20, hwnd, NULL, NULL, NULL);
            g_hCollageSizeEdit = CreateWindowA("EDIT", "4000", WS_VISIBLE | WS_CHILD | WS_BORDER, 320, 95 + yOffset, 60, 20, hwnd, (HMENU)IDC_EDT_COLLAGE_SIZE, NULL, NULL);
            CreateWindowA("STATIC", "Spacing (30):", WS_VISIBLE | WS_CHILD, 10, 125 + yOffset, 120, 20, hwnd, NULL, NULL, NULL);
            g_hSpacingEdit = CreateWindowA("EDIT", "30", WS_VISIBLE | WS_CHILD | WS_BORDER, 130, 125 + yOffset, 60, 20, hwnd, (HMENU)IDC_EDT_SPACING, NULL, NULL);
            CreateWindowA("STATIC", "Batch Reduction:", WS_VISIBLE | WS_CHILD, 200, 125 + yOffset, 120, 20, hwnd, NULL, NULL, NULL);
            g_hBatchReductionEdit = CreateWindowA("EDIT", "Auto", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_READONLY, 320, 125 + yOffset, 120, 20, hwnd, (HMENU)IDC_EDT_BATCH_REDUCTION, NULL, NULL);
            g_hPngCheckbox = CreateWindowA("BUTTON", "Output PNG (Transparent)", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 10, 155 + yOffset, 200, 20, hwnd, (HMENU)IDC_CHK_PNG_OUTPUT, NULL, NULL);
            CreateWindowA("STATIC", "Background Color (JPG only):", WS_VISIBLE | WS_CHILD, 220, 155 + yOffset, 180, 20, hwnd, NULL, NULL, NULL);
            CreateWindowA("BUTTON", "Select Color", WS_VISIBLE | WS_CHILD, 220, 175 + yOffset, 100, 25, hwnd, (HMENU)IDC_BTN_SELECT_COLOR, NULL, NULL);
            g_hColorPreview = CreateWindowA("STATIC", "", WS_VISIBLE | WS_CHILD | SS_OWNERDRAW | WS_BORDER, 330, 175 + yOffset, 40, 25, hwnd, (HMENU)IDC_EDT_COLOR_PREVIEW, NULL, NULL);
            CreateWindowA("STATIC", "3. Log:", WS_VISIBLE | WS_CHILD, 10, 210 + yOffset, 150, 20, hwnd, NULL, NULL, NULL);
            g_hLogList = CreateWindowA("LISTBOX", "", WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, 10, 230 + yOffset, 430, 140, hwnd, (HMENU)IDC_LST_LOG, NULL, NULL);
            g_hStatusLabel = CreateWindowA("STATIC", "Ready.", WS_VISIBLE | WS_CHILD, 10, 375 + yOffset, 300, 20, hwnd, (HMENU)IDC_LBL_STATUS, NULL, NULL);
            g_hCounterLabel = CreateWindowA("STATIC", "Collages: 0", WS_VISIBLE | WS_CHILD | SS_RIGHT, 320, 375 + yOffset, 120, 20, hwnd, (HMENU)IDC_LBL_COUNTER, NULL, NULL);
            CreateWindowA("BUTTON", "START PROCESSING", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 10, 400 + yOffset, 430, 35, hwnd, (HMENU)IDC_BTN_START, NULL, NULL);
            break;
        }
        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT lpDis = (LPDRAWITEMSTRUCT)lParam;
            if (lpDis->CtlID == IDC_EDT_COLOR_PREVIEW) {
                DrawColorPreview(lpDis->hDC); 
                return TRUE;
            }
            break;
        }
        case WM_COMMAND: {
            switch(LOWORD(wParam)) {
                case IDC_BTN_SELECT_DIR: {
                    if (g_isProcessing) break;
                    g_inputDirPathW = BrowseForFolderW(hwnd);
                    if (!g_inputDirPathW.empty()) {
                        SetWindowTextW(g_hInputEdit, g_inputDirPathW.c_str()); 
                    }
                    break;
                }
                case IDC_BTN_START: {
                    if (g_isProcessing) {
                        g_isProcessing = false;
                        EnableWindow(GetDlgItem(g_hMainWindow, IDC_BTN_START), TRUE);
                        SetWindowTextA(g_hStatusLabel, "Ready.");
                        break;
                    }
                    wchar_t bufferW[MAX_PATH];
                    GetWindowTextW(g_hInputEdit, bufferW, MAX_PATH);
                    g_inputDirPathW = bufferW;
                    if (g_inputDirPathW.empty()) {
                        MessageBoxA(hwnd, "Please select input directory.", "Error", MB_ICONERROR);
                        break;
                    }
                    try {
                        char buffer[MAX_PATH];
                        GetWindowTextA(g_hNumCollagesEdit, buffer, MAX_PATH);
                        NUM_SQUARE_COLLAGES_PER_DIR = std::stoi(buffer);
                        GetWindowTextA(g_hCollageSizeEdit, buffer, MAX_PATH);
                        SQUARE_COLLAGE_SIDE_LENGTH = std::stoi(buffer);
                        if (SQUARE_COLLAGE_SIDE_LENGTH < 2000 || SQUARE_COLLAGE_SIDE_LENGTH > 4000) SQUARE_COLLAGE_SIDE_LENGTH = 4000;
                        GetWindowTextA(g_hSpacingEdit, buffer, MAX_PATH);
                        TILE_SPACING = std::stoi(buffer);
                        if (TILE_SPACING < 0) TILE_SPACING = 0;
                        g_usePngOutput = (SendMessage(g_hPngCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED);
                        
                        if (SendMessage(g_hRbSmart, BM_GETCHECK, 0, 0) == BST_CHECKED) g_currentMode = MODE_SMART;
                        else if (SendMessage(g_hRbPacked, BM_GETCHECK, 0, 0) == BST_CHECKED) g_currentMode = MODE_PACKED;
                        else if (SendMessage(g_hRbMixed, BM_GETCHECK, 0, 0) == BST_CHECKED) g_currentMode = MODE_MIXED;
                        else g_currentMode = MODE_UNIFORM;

                    } catch (...) {
                        MessageBoxA(hwnd, "Invalid parameter values.", "Error", MB_ICONERROR);
                        break;
                    }
                    if (g_workerThread.joinable()) g_workerThread.join();
                    g_workerThread = std::thread(WorkerThreadFunction);
                    break;
                }
                case IDC_BTN_SELECT_COLOR: {
                    if (g_isProcessing) break;
                    ChooseBackgroundColor(hwnd);
                    break;
                }
            }
            break;
        }
        case WM_LOG_MESSAGE: {
            std::string* msg = (std::string*)lParam;
            LogMessage(*msg);
            delete msg;
            break;
        }
        case WM_UPDATE_STATUS: {
            std::string* msg = (std::string*)lParam;
            SetWindowTextA(g_hStatusLabel, msg->c_str());
            delete msg;
            break;
        }
        case WM_UPDATE_COUNTER: {
            std::string s = "Collages: " + std::to_string(g_totalCollagesMade);
            SetWindowTextA(g_hCounterLabel, s.c_str());
            break;
        }
        case WM_PROCESSING_FINISHED: {
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_START), TRUE);
            g_isProcessing = false;
            break;
        }
        case WM_CLOSE: {
            if (g_isProcessing) g_stopRequested = true;
            if (g_logFileStream.is_open()) g_logFileStream.close();
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY: {
            g_stopRequested = true;
            if (g_workerThread.joinable()) g_workerThread.join();
            if (g_logFileStream.is_open()) g_logFileStream.close();
            PostQuitMessage(0);
            break;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    #ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    #endif

    WNDCLASSA wc = {};
    MSG Msg;

    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = g_szClassName;

    if(!RegisterClassA(&wc)) return 0;

    HWND hwnd = CreateWindowA(
        g_szClassName,
        "C++ Collage Maker (Dir Structure)",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 470, 630, 
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if(hwnd == NULL) return 0;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    while(GetMessage(&Msg, NULL, 0, 0) > 0) {
        TranslateMessage(&Msg);
        DispatchMessage(&Msg);
    }
    return (int)Msg.wParam;
}