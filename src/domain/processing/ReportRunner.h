#pragma once

#include <string>

// Headless driver that produces a single PDF report of every .g file found
// under a library directory: index -> process -> (optional) AI tag -> render
// per-model gist pages -> assemble PDF.  Reuses ProcessGFiles and GistHandler
// so there is a single implementation of the processing and PDF-assembly logic.
class ReportRunner {
public:
    struct Options {
        std::string libraryPath;   // directory to scan for .g files
        std::string outputPdf;     // output .pdf path (empty -> library default)
        int         depth = 4;     // filesystem scan depth (auto-deepens if none found)
        bool        tags  = true;  // attempt AI tagging if a model is available
        bool        render = true;  // render missing gist pages (false -> use only cached)
        std::string title = "3D Model Inventory Report";
        std::string label = "";    // optional banner/classification label
        std::string user  = "";    // optional preparer/owner name
    };

    // returns 0 on success, non-zero on failure
    static int run(const Options& opt);
};
