// MIT License
// 
// Copyright (c) 2026 White Field Studios
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// -----------------------------------------------------------------------------
//
// wipefile.cpp
//
//   Command line utility that overwrites a file's contents with zero bytes,
//   renames it to a random string to obscure metadata, and deletes it.
//
//   WARNING / DISCLAIMER:
//     Use this tool at your own risk. This program permanently destroys data. 
//     The author (White Field Studios) is not responsible or liable for any 
//     accidental data loss, file corruption, or system damage resulting from 
//     the use or misuse of this software.
//
//   Requires C++17 or newer for std::filesystem.
//
//   Usage:
//     wipefile <filename1> [filename2 ...]
//     wipefile -k <filename1> [filename2 ...]
//     wipefile -w <options> <filename1> [filename2 ...]
//     wipefile --help | -h | /?
//
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <filesystem>
#include <random>

using namespace std;
namespace fs = std::filesystem;

// Structure to hold multiple middle wipe commands
struct MidWipe {
    long long startPos;
    int len;
};

// Print full usage information to stdout.
static void showHelp() {
    cout <<
        "wipefile -- securely zero-fill, scramble the filename, and delete files\n"
        "\n"
        "Usage:\n"
        "  wipefile <filename1> [filename2 ...]\n"
        "  wipefile -k <filename1> [filename2 ...]\n"
        "  wipefile -w <options> <filename1> [filename2 ...]\n"
        "  wipefile --help | -h | /?\n"
        "\n"
        "Without flags, the entire file is zeroed, renamed to a random string, and deleted.\n"
        "\n"
        "Global Flags:\n"
        "  -k   Keep the file. Overrides all other deletion logic; the file is neither\n"
        "       renamed nor deleted after the wipe completes.\n"
        "\n"
        "Standard Options List <beg,end,del> (no spaces allowed, comma separated):\n"
        "  beg  Number of zero bytes to write at the beginning. Use -1 for entire file.\n"
        "  end  Number of zero bytes to write at the end. Ignored if beg is -1.\n"
        "  del  1 = delete the file after wiping, 0 = keep the file.\n"
        "\n"
        "Alternative Letter Syntax (case-insensitive, separated by commas):\n"
        "  B#     Zero '#' bytes at the beginning. Defaults to 1024 if omitted.\n"
        "  E#     Zero '#' bytes at the end. Defaults to 1024 if omitted.\n"
        "  M#,#   Zero in the middle. First '#' is the 1-based start position,\n"
        "         second '#' is the number of zero bytes to write. Second defaults to 1024.\n"
        "         If M is provided with no numbers at all, it centers a 1024 byte wipe in the file.\n"
        "         You can provide multiple M parameters (e.g. M10,20,M100,50).\n"
        "  K      Keep the file. If K is omitted, the file is deleted.\n"
        "\n"
        "Examples:\n"
        "  wipefile data.bin\n"
        "      Zero the entire file, randomize its name, and delete it.\n"
        "  wipefile -k data.bin\n"
        "      Zero the entire file but keep it intact.\n"
        "  wipefile -w B,E file1.txt file2.txt\n"
        "      Zero first 1024 and last 1024 bytes of both files, then delete them.\n"
        "  wipefile -w M40,20 -k text.txt\n"
        "      Zero 20 bytes starting at pos 40, and strictly keep the file.\n"
        "\n"
        "Exit code is 0 on success, 1 on any error.\n";
}

// Split a string on a delimiter, discarding empty tokens.
static vector<string> splitString(const string& str, char delimiter) {
    vector<string> tokens;
    istringstream iss(str);
    string token;

    while (getline(iss, token, delimiter)) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

// Helper to extract an integer while parsing letter syntax, with a fallback default.
static int readIntOrDefault(const string& s, size_t& idx, int defaultVal) {
    int val = 0;
    bool found = false;
    while (idx < s.length() && isdigit(s[idx])) {
        val = val * 10 + (s[idx] - '0');
        idx++;
        found = true;
    }
    return found ? val : defaultVal;
}

// Generate a random alphanumeric string for file renaming
static string generateRandomFilename(size_t length) {
    const char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(0, sizeof(chars) - 2);
    
    string result;
    for (size_t i = 0; i < length; ++i) {
        result += chars[dis(gen)];
    }
    return result;
}

// Overwrite a file with zero bytes, optionally rename it, and optionally delete it.
static int wipeFilename(const string& filename, int begByteLen, int endByteLen, const vector<MidWipe>& midWipes, bool removeFile) {
    // Open for in-place binary read/write (does not truncate the file)
    fstream file(filename, ios::in | ios::out | ios::binary);
    if (!file) {
        cerr << "Error: Unable to open " << filename << endl;
        return 1;
    }

    // Determine total file size
    file.seekg(0, ios::end);
    streamsize fileSize = file.tellg();
    file.seekp(0, ios::beg);

    // Cap the requested lengths for beginning and end to the actual file size
    if (endByteLen > fileSize) {
        endByteLen = static_cast<int>(fileSize);
    }
    if (begByteLen > fileSize) {
        begByteLen = static_cast<int>(fileSize);
    }

    if (begByteLen != -1) {
        // Fast mode: zero only specific regions
        
        // Zero beginning
        if (begByteLen > 0) {
            const size_t bufferSize = begByteLen;
            vector<char> zeros(bufferSize, 0);
            
            streamsize wipeSize = std::min(static_cast<streamsize>(bufferSize), fileSize);
            cout << "Zeroing the beginning " << wipeSize << " bytes..." << endl;
            file.write(zeros.data(), wipeSize);
        }
        
        // Zero multiple middle sections
        for (const auto& mw : midWipes) {
            long long currentMidPos = mw.startPos;
            int currentMidLen = mw.len;

            // If offset is -2, auto-center this specific wipe block
            if (currentMidPos == -2) {
                currentMidPos = (fileSize / 2) - (currentMidLen / 2);
                if (currentMidPos < 0) {
                    currentMidPos = 0;
                }
            }

            if (currentMidLen > fileSize) {
                currentMidLen = static_cast<int>(fileSize);
            }

            if (currentMidLen > 0 && currentMidPos >= 0) {
                streamsize maxMidWrite = std::min(static_cast<streamsize>(currentMidLen), fileSize - static_cast<streamsize>(currentMidPos));
                if (maxMidWrite > 0) {
                    cout << "Zeroing " << maxMidWrite << " bytes starting at position " << (currentMidPos + 1) << "..." << endl;
                    file.seekp(currentMidPos, ios::beg);
                    vector<char> zerosMid(maxMidWrite, 0);
                    file.write(zerosMid.data(), maxMidWrite);
                }
            }
        }

        // Zero ending
        if (fileSize > begByteLen && endByteLen > 0) {
            cout << "Zeroing the ending " << endByteLen << " bytes..." << endl;
            const size_t bufferSizeEnd = endByteLen;
            vector<char> zerosEnd(bufferSizeEnd, 0);
            
            streamsize endPos = (fileSize > static_cast<streamsize>(bufferSizeEnd)) ? (fileSize - static_cast<streamsize>(bufferSizeEnd)) : 0;
            file.seekp(endPos, ios::beg);
            
            streamsize maxEndWrite = std::min(static_cast<streamsize>(bufferSizeEnd), fileSize - endPos);
            file.write(zerosEnd.data(), maxEndWrite);
        }
    }
    else {
        // Full mode: overwrite the entire file in fixed-size chunks
        cout << "Zeroing the entire file..." << endl;
        const size_t bufferSize = 8192; // 8 KB buffer
        vector<char> buffer(bufferSize, 0);

        streamsize remaining = fileSize;
        while (remaining > 0) {
            streamsize chunkSize = std::min(remaining, static_cast<streamsize>(bufferSize));
            file.write(buffer.data(), chunkSize);
            remaining -= chunkSize;
        }
    }

    // Flush and close so the zeroes reach the disk before any rename/delete operations
    file.close();
    cout << "File filled with zeroes successfully." << endl;

    if (removeFile) {
        try {
            fs::path p(filename);
            // Generate a 16-character random filename (e.g. K8xT9pL2vM5aQ1rW.tmp)
            string randomName = generateRandomFilename(16) + ".tmp";
            fs::path newPath = p.parent_path() / randomName;
            
            cout << "Scrambling metadata, renaming to: " << randomName << "..." << endl;
            fs::rename(p, newPath);
            
            cout << "Deleting file..." << endl;
            if (fs::remove(newPath)) {
                cout << "File successfully wiped and removed." << endl;
            } else {
                cerr << "Error: Could not remove renamed file." << endl;
                return 1;
            }
        } catch (const fs::filesystem_error& e) {
            cerr << "Filesystem Error during rename/remove: " << e.what() << endl;
            return 1;
        }
    }
    else {
        cout << "File successfully wiped (not removed)." << endl;
    }

    return 0;
}

int main(int argc, char* argv[]) {
    string optPart = "";
    vector<string> filenames;
    
    // Default behaviors
    bool removeFile = true;
    bool forceKeep = false; // Overrides removeFile if -k is passed
    int begByteLen = -1; // -1 means full file wipe
    int endByteLen = 0;
    vector<MidWipe> midWipes;

    if (argc < 2) {
        showHelp();
        return 1;
    }

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        
        if (arg == "--help" || arg == "-h" || arg == "/?") {
            showHelp();
            return 0;
        } 
        else if (arg == "-k") {
            forceKeep = true;
        }
        else if (arg == "-w") {
            if (i + 1 < argc) {
                optPart = argv[++i]; // Consume the next argument as the options string
            } else {
                cerr << "Error: -w flag requires an options string (e.g., -w B1024,K)" << endl;
                return 1;
            }
        } 
        else {
            filenames.push_back(arg); // Anything else is treated as a filename
        }
    }

    if (filenames.empty()) {
        cerr << "Error: No files specified for wiping." << endl;
        showHelp();
        return 1;
    }

    // Process options if the -w flag was used
    if (!optPart.empty()) {
        removeFile = false; // Reset delete flag, letter syntax or standard syntax will re-evaluate

        // Strip surrounding quotes if the shell passed them literally (e.g. PowerShell)
        if (optPart.length() >= 2) {
            if ((optPart.front() == '"' && optPart.back() == '"') || 
                (optPart.front() == '\'' && optPart.back() == '\'')) {
                optPart = optPart.substr(1, optPart.length() - 2);
            }
        }

        string innerStr = optPart;
        
        // Determine if we are using standard mode or letter syntax
        bool useLetterSyntax = false;
        for (char c : innerStr) {
            if (isalpha(c)) {
                useLetterSyntax = true;
                break;
            }
        }

        if (useLetterSyntax) {
            begByteLen = 0; // Turn off full wipe default
            removeFile = true; // Default to deleting in letter syntax unless 'K' is found
            size_t i = 0;
            try {
                while (i < innerStr.length()) {
                    char c = toupper(innerStr[i]);
                    if (c == 'K') {
                        removeFile = false;
                        i++;
                    }
                    else if (c == 'B') {
                        i++;
                        begByteLen = readIntOrDefault(innerStr, i, 1024);
                    }
                    else if (c == 'E') {
                        i++;
                        endByteLen = readIntOrDefault(innerStr, i, 1024);
                    }
                    else if (c == 'M') {
                        i++;
                        MidWipe mw;
                        if (i < innerStr.length() && isdigit(innerStr[i])) {
                            mw.startPos = readIntOrDefault(innerStr, i, 1024) - 1; // convert 1-based start to 0-based offset
                            if (i < innerStr.length() && innerStr[i] == ',') {
                                i++; // skip separator
                            }
                            mw.len = readIntOrDefault(innerStr, i, 1024);
                        } else {
                            // M supplied with no numbers
                            mw.startPos = -2; // special flag for auto-centering
                            mw.len = 1024;
                        }
                        midWipes.push_back(mw);
                    }
                    else if (c == ',') {
                        i++; // Ignore stray commas
                    }
                    else {
                        throw runtime_error("Invalid character in options");
                    }
                }
            }
            catch (const exception& e) {
                cerr << "Error parsing letter options: " << e.what() << endl;
                return 1;
            }
        }
        else {
            // Original Standard Syntax (e.g. -w 1024,512,1)
            vector<string> result = splitString(innerStr, ',');
            if (result.size() != 3) {
                cerr << "Error: standard options list must contain exactly 3 values, e.g. -w 1024,512,1" << endl;
                return 1;
            }
            try {
                begByteLen = stoi(result[0]);
                endByteLen = stoi(result[1]);
                if (stoi(result[2]) == 1) {
                    removeFile = true;
                }
            }
            catch (...) {
                cerr << "Error: standard options list values must be integers" << endl;
                return 1;
            }
            if (endByteLen < 0) {
                cerr << "Error: end byte count must not be negative" << endl;
                return 1;
            }
        }
    }

    // Apply the global -k flag if provided, overriding any internal syntax deletion logic
    if (forceKeep) {
        removeFile = false;
    }

    // Loop through all collected filenames and wipe them
    int overallExitCode = 0;
    for (size_t i = 0; i < filenames.size(); ++i) {
        if (filenames.size() > 1) {
            cout << "\nProcessing: " << filenames[i] << endl;
        }
        int status = wipeFilename(filenames[i], begByteLen, endByteLen, midWipes, removeFile);
        if (status != 0) {
            overallExitCode = status; // Record failure, but continue wiping
        }
    }

    return overallExitCode;
}
