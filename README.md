# CADventory


CADventory is a 3D CAD file, categorical tagging, and associated
metadata inventory management system.

CADventory is a Qt desktop application for indexing and organizing
large CAD model libraries and their associated files without requiring
a heavyweight PDM deployment.  It is aimed at workflows where
individuals and teams want to index existing 3D model collections, and
build useful metadata, reporting, and discovery layers on top of those
model libraries.  It's tested on Mac, Linux, and Windows, includes
code coverage testing, and code security testing.


## Installation

1) Install: CMake 3.25+, C++17 compiler, SQLite3, BRL-CAD, and Qt6
2) Clone/Install BRLCAD from https://github.com/BRL-CAD/brlcad.git
3) Clone/Download CADventory from source
4) **Install Ollama and LLaMA 3** (required for AI tagging):
   - Download Ollama from: https://ollama.com
   - Once installed, run the following command to install the model:
     ```
     ollama pull llama3
     ```
5) Compile (see .gitlab-ci.yml for variations):

  cmake -S . -B build-release \
    -DCMAKE_BUILD_TYPE=Release \
    -DQt6_DIR=/path/to/qt6 \
    -DBRLCAD_ROOT=/path/to/brlcad
  cmake --build build-release
  cmake --install build-release --prefix ./install

### Configuration Notes

- `CADVENTORY_WITH_GUI=ON` is the default.
- If you want a headless-only build, configure with `-DCADVENTORY_WITH_GUI=OFF`.

## Usage

Run (Mac and Linux):
  ./install/bin/cadventory
Run (Windows):
  .\Release\bin\cadventory.exe

Click + in GUI and navigate to a folder to index.

> ⚠️ AI-based tagging requires Ollama to be running and the `llama3` model to be available.  CADventory includes an experimental AI-assisted tagging path built around Ollama and a local model such as `llama3`.

To clear all settings, run with --no-gui command-line option.

The currently supported command-line options are:

- `--index /path/to/library`
  Run library indexing in CLI mode.
- `--worker /path/to/library`
  Run the background worker in CLI mode.
- `--reset`
  Clear CADventory settings and reset the model database state.
- `-j`, `--num-cpus`
  Set the worker thread count.
- `-t`, `--timeout`
  Set the worker timeout value in seconds.
- `-v`
  Increase logging verbosity. The logger also supports stacked flags such as `-vv`.

Example:

```bash
./install/bin/cadventory --index /path/to/library -v
```

## Testing 

Use a fresh build directory for test work:

```bash
cmake -S . -B build-debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DQt6_DIR=/path/to/qt6 \
  -DBRLCAD_ROOT=/path/to/brlcad

cmake --build build-debug
ctest --test-dir build-debug --output-on-failure
```

1. Create build directory from root of the project (mkdir build)
2. Navigate to build directory (cd build)
3. cmake -DCMAKE_BUILD_TYPE=Debug -DBRLCAD_DIR=/path/to/brlcad/build ..
4. make
5. ctest --output-on-failure
6. lcov --capture --directory . --output-file coverage.info
7. lcov --remove coverage.info '/usr/*' --output-file coverage_filtered.info
8. genhtml coverage_filtered.info --output-directory coverage_report
9. view coverage_report/index.html 

## Automated Test Suite via Python Script

1. Dependencies:

  Install CMake:
    On macOS: brew install cmake
    On Ubuntu: sudo apt install cmake
    On Windows: Install from the CMake website.
  Install LCOV and GenHTML:
    On macOS: brew install lcov
    On Ubuntu: sudo apt install lcov
    On Windows: Install LCOV and GenHTML through Cygwin or equivalent.
  Ensure make or mingw32-make is available.

2. Environment Variable for BRLCAD_ROOT:

  You can specify the BRLCAD_ROOT path interactively when the script runs.

3. Cross-Platform Commands:

  make: Uses mingw32-make on Windows.
  File Paths: Adapts to Linux/macOS vs. Windows paths (/usr/* vs. C:/Program Files/*).
  Opening HTML Reports: Uses start, open, or xdg-open based on the OS.

4. Testing Workflow:

  Compiles the application.
  Runs unit tests with CTest.
  Generates and filters a coverage report.
  Opens the report in the default browser.

5. Running the Script
  Ensure Python is installed (version 3.6+).
  Run it in your project directory (cadventory):
    python run_tests.py or python3 run_tests.py

## Design

Here's an architecture diagram:

```
┌──────────────────────────────────────────────────────────────────────┐
│                               CADventory                             │
│                                                                      │
│    ┌──────────────────────────────────────────────────────────────┐  │
│    │                      User Interface (Qt)                     │  │
│    └─────────────┬───────────────────────┬─────────────────┬──────┘  │
│                  │                       │                 │         │
│    ┌─────────────▼───────────┐  ┌────────▼─────────┐  ┌────▼──────┐  │
│    │  Filesystem Processor   │  │ SQLite or JSON   │◄─│ Report    │  │
│    └─────────────┬───────────┘  │ Storage Manager  │  │ Generator │  │
│                  │              └──────────────────┘  └────┬──────┘  │
│                  │                                         │         │
│    ┌─────────────▼───────────┐                             │         │
│    │ Geometry/Image/Document │                             │         │
│    │ Handler                 │                             │         │
│    └─────────────┬───────────┘                             │         │
│                  │                                         │         │
│    ┌─────────────▼────────┐                                │         │
│    │     CAD Libraries    │◄───────────────────────────────┘         │
│    └──────────────────────┘                                          │
└──────────────────────────────────────────────────────────────────────┘
```


### File Structure
```
.
├── src/                  Application source
│   ├── core/             App bootstrap and shared utilities
│   ├── domain/           Model, indexing, job, processing, and LLM logic
│   ├── ui/               Qt UI code
│   └── tests/            Automated tests
├── doc/                  Design docs, planning notes, and wireframes
├── scripts/              Small helper scripts
├── cmake/                CMake modules and dependency helpers
└── third_party/          Vendored third-party assets

```

### Core Components
#### ModelTagging.h/cpp
- Checks for Ollama + model
- Builds prompt and runs AI tagging
- Uses QProcess to write/read tag results
- Signals: `tagsGenerated`, `tagGenerationCanceled`

#### ModelParser.h/cpp
- Uses `mged` CLI to extract title and object paths
- Converts file paths to WSL-compatible if needed

#### executeCommand.h/cpp
- Windows-safe hidden execution of commands
- Also supports redirection for prompt generation

### AI Tagging
- Powered by LLaMA 3 (via Ollama)
- Parses .g BRL-CAD files for object paths and titles
- Sends structured prompt to local LLaMA model via command-line interface
- Returns exactly 10 one-word tags for classification/search

#### How to Use
- Ensure Ollama is installed and added to your `PATH`
- Call ModelTagging::generateTags(filepath) to begin tag generation
- Automatically emits a tagsGenerated(tags) signal when complete

### Roadmap

1) Implement automatic PDF report generation (1-page per library model).
2) Integrate metadata support into GUI for tagging primary models.
3) Graphically depict models and iconography during GUI browsing.
4) Implement support for creating & modifying collections manually.
5) Implement support for identifying between AI generated and manually added tags
6) Multi-threading support for tag generation
7) Refine accuracy of the AI Tagging

## Credits
- AI tagging logic: Written in-house using prompts run via Ollama
- BRL-CAD Integration: Leveraging mged CLI from BRL-CAD
- AI backend: Powered by LLaMA 3 via Ollama
- Qt GUI: Built using Qt6

## License

MIT

