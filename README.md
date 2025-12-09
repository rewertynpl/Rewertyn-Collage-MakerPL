# Rewertyn-Collage-MakerPL

Copyright (c) 2025 Marcin Matysek (RewertynPL) under MIT License

I. Project Design Assumptions (Business & Functional Level)
1. Project Name and Purpose
Project Name: Rewertyn Collage MakerPL / Rewertyn Collage Automation Tool

Main Goal/Mission: To automate the generation of high-resolution image collages from large, nested directory structures. The system addresses the need for bulk image processing—specifically creating "contact sheets" or artistic grids from thousands of images—while preserving the original folder hierarchy. It eliminates the manual effort required to resize and stitch images for printing, dataset visualization, or archival purposes.

2. Description of Functionality
Key Modules and Functions:

GUI & Configuration Module: Provides a native Windows interface for users to select input directories and configure parameters (collage size, tile spacing, background color, output format).

File System Crawler: A recursive scanning engine that navigates through deep directory trees, filtering for supported image formats (.jpg, .png, .bmp, etc.) and replicating the structure in the output folder.

Collage Logic Engine: The core brain containing algorithms to arrange images. It includes four distinct modes:

Uniform Grid: Standard grid layout.

Smart Fit: Attempts to force a specific number of images.

Packed Grid: Removes empty spaces.

Mixed Grids: An advanced algorithm that dynamically calculates a mix of large and small grids to fit an exact number of images per folder.

Image Processing Core: Handles image loading, resizing, transparency management (Alpha channel), background rendering, and stitching using the CImg library.

Logging & Status Module: Provides real-time feedback to the user via a scrolling log window, status bar updates, and a persistent text log file (debug_log.txt).

Usage Scenarios (Happy Paths):

Bulk Archiving: A user selects a root folder containing 50 subfolders of family photos. They select "Uniform Grid", set the output to JPG, and click Start. The system recreates the 50 subfolders in the output directory, each containing neat collage grids of the photos found within.

Asset Preparation (Game Dev/Print): A designer needs transparent sprite sheets. They select a folder of assets, check "Output PNG (Transparent)", select "Packed Grid" mode, and click Start. The system generates transparent PNG collages ready for texture mapping.

3. Non-Functional Assumptions (Quality Requirements)
Performance: The application uses multithreading. The UI runs on the main thread, while heavy image processing occurs in a dedicated background worker thread (std::thread), ensuring the interface remains responsive (does not freeze) during heavy workloads.

Resource Management: The code implements a temporary file strategy. Instead of holding gigabytes of raw pixel data in RAM for large collages, processed tiles are saved to a temporary directory and reloaded only when stitching the final image. This prevents "Out of Memory" crashes on standard consumer hardware.

Scalability: The architecture supports recursive processing of an unlimited number of files and directories, limited only by disk space.

Reliability: Robust error handling is implemented using try-catch blocks around image operations to prevent a single corrupt file from crashing the entire batch process.

II. Technical Specification (Architectural Level)
1. Technology Stack
Programming Language: C++17

Main Frameworks & Libraries:

Windows API (Win32): For the Graphical User Interface (GUI), window management, and file system dialogs (shlobj.h, commctrl.h).

CImg Library: A lightweight, header-only library for image processing logic.

LibJPEG & LibPNG: External dependencies linked for high-quality image encoding/decoding.

MinGW-w64: The target build environment (GCC compiler for Windows).

Database: None. The system relies on the File System as the data store.

2. System Architecture (High Level)
Architecture Schema: Event-Driven Desktop Monolith.

The application follows a standard Windows Message Loop architecture.

It implements a simplified Worker Pattern: The Main Thread handles UI events (clicks, painting), while a separate Worker Thread executes the business logic.

Data Flow:

Input: User selects a directory via the UI.

Trigger: IDC_BTN_START initiates the Worker Thread.

Discovery: The scanner traverses the directory tree (GetSubDirsRecursive) and identifies image files.

Transformation: Images are loaded, normalized (resized/cropped), and saved as temporary tiles.

Assembly: Tiles are loaded back and drawn onto a large canvas (the Collage).

Output: The canvas is saved to the disk (Output Directory).

Feedback: The Worker Thread sends PostMessage events (e.g., WM_UPDATE_COUNTER, WM_LOG_MESSAGE) back to the Main UI Thread to update the display safely.

3. Code Structure and Conventions
Key Files/Components:

WinMain & WndProc: Entry point and message handler. manages the lifecycle of the application window and controls.

WorkerThreadFunction: The entry point for the background processing thread.

processDirectory: Contains the high-level logic for handling a specific folder (creating output paths, sorting files, selecting algorithms).

processImageForCollage: The atomic unit of work—processes a single image into a collage tile.

createFinalCollage: The assembly function that stitches tiles together.

Design Patterns:

Observer/Event Listener: The WndProc function listens for Windows messages (WM_COMMAND, WM_USER) to react to user actions and thread updates.

Producer-Consumer (Simplified): The scanning logic produces file paths, and the processing logic consumes them to create images.

Guard Pattern: std::lock_guard is used with std::mutex (g_logMutex) to ensure thread-safe logging.

4. Data Model (Entities)
Since there is no traditional database, the "entities" are represented by data structures in memory and files on disk.

Main Entities:

Directory: The unit of organization. The system iterates over a list of DirsToProcess.

Image File: The raw input. Represented as std::wstring paths.

Collage Configuration: Global variables acting as a configuration object (e.g., NUM_SQUARE_COLLAGES_PER_DIR, SQUARE_COLLAGE_SIDE_LENGTH, GenMode).

Relations:

Directory -> Images (One-to-Many): A directory contains multiple images.

Directory -> Collages (One-to-Many): One input directory can result in multiple output collage files depending on the batch reduction settings.

5. Implementation Details
State Management:

The application relies on Global State variables (prefixed with g_ like g_isProcessing, g_stopRequested) to share data between the UI and the Worker Thread. This is typical for simple WinAPI applications but requires careful atomic management (std::atomic).

Unicode/Path Handling:

The code explicitly handles Windows Unicode (std::wstring, wchar_t) to support special characters in file paths.

It uses a helper GetSafePathForCImg to convert paths to "Short Path" (8.3 format) if necessary, ensuring compatibility with the CImg library which primarily uses char*.

Error Handling:

CImg Exceptions: Caught locally to prevent image corruption from crashing the app.

File System Errors: Directory creation failures are logged but generally non-blocking.

User Interruption: An atomic flag g_stopRequested is checked periodically in loops to allow the user to safely "Stop" the process immediately.
