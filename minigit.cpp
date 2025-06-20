#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <ctime>
#include <vector>
#include <cstdio>
#include <sys/stat.h> // mkdir for Windows use <direct.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir(dir, mode) _mkdir(dir)
#endif
// Simple hash function (djb2) for demonstration (not cryptographically secure)
unsigned long simpleHash(const std::string& str) {
    unsigned long hash = 5381;
    for (size_t i = 0; i < str.size(); ++i) {
        hash = ((hash << 5) + hash) + static_cast<unsigned char>(str[i]);
    }
    return hash;
}

std::string hashToString(unsigned long h) {
    std::ostringstream oss;
    oss << std::hex << h;
    return oss.str();
}

// Create directory if not exists
void createDir(const std::string& dir) {
    struct stat info;
    if (stat(dir.c_str(), &info) != 0) {
        mkdir(dir.c_str(), 0755);
    }
}

// Read whole file content
std::string readFile(const std::string& filename) {
    std::ifstream ifs(filename.c_str(), std::ios::binary);
    if (!ifs) return "";
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

// Write content to file
void writeFile(const std::string& filename, const std::string& content) {
    std::ofstream ofs(filename.c_str(), std::ios::binary);
    ofs << content;
}

// MiniGit class
class MiniGit {
    std::string repoDir = ".minigit";
    std::string objectsDir = ".minigit/objects";
    std::string headFile = ".minigit/HEAD";
    std::string indexFile = ".minigit/index";
    std::string branchesFile = ".minigit/branches";

    std::unordered_set<std::string> stagingArea;
    std::unordered_map<std::string, std::string> branches;
    std::string head = "master";

public:
    void merge(const std::string& otherBranch);
    void diff(const std::string& commit1, const std::string& commit2);
    std::string findLCA(const std::string& a, const std::string& b);
    void init() {
        createDir(repoDir);
        createDir(objectsDir);
        if (readFile(headFile).empty()) {
            writeFile(headFile, head);
        }
        if (readFile(branchesFile).empty()) {
            branches[head] = "";
            saveBranches();
        } else {
            loadBranches();
        }
        std::cout << "Initialized empty MiniGit repository.\n";
    }

    void add(const std::string& filename) {
        std::string content = readFile(filename);
        if (content.empty()) {
            std::cout << "File not found or empty: " << filename << "\n";
            return;
        }
        stagingArea.insert(filename);
        saveIndex();
        std::cout << "Added " << filename << " to staging area.\n";
    }

    void commit(const std::string& message, const std::string& secondParent = "") {
        loadIndex();
        if (stagingArea.empty()) {
            std::cout << "No changes staged for commit.\n";
            return;
        }
    
        loadBranches();
        head = readFile(headFile);
        if (!head.empty()) head.erase(head.find_last_not_of(" \n\r\t")+1);

        std::string parent = "";
        std::unordered_map<std::string, std::string>::const_iterator itb = branches.find(head);
        if (itb != branches.end()) {
            parent = itb->second;
        }

        // Load parent commit files
        std::unordered_map<std::string, std::string> files;
        if (!parent.empty()) {
            files = loadCommitFiles(parent);
        }

        // Add staged files
        std::unordered_set<std::string>::const_iterator it;
        for (it = stagingArea.begin(); it != stagingArea.end(); ++it) {
            const std::string& f = *it;
            std::string content = readFile(f);
            std::string blobHash = hashToString(simpleHash(content));
            std::string blobPath = objectsDir + "/" + blobHash;
            if (readFile(blobPath).empty()) {
                writeFile(blobPath, content);
            }
            files[f] = blobHash;
        }

        // Create commit content
        std::ostringstream oss;
        oss << "parent " << parent << "\n";
        if (!secondParent.empty()) {
            oss << "parent2 " << secondParent << "\n";
        }
        oss << "date " << std::time(nullptr) << "\n";
        oss << "message " << message << "\n";

        std::unordered_map<std::string, std::string>::const_iterator itf;
        for (itf = files.begin(); itf != files.end(); ++itf) {
            oss << "file " << itf->first << " " << itf->second << "\n";
        }

        std::string commitContent = oss.str();
        std::string commitHash = hashToString(simpleHash(commitContent));
        std::string commitPath = objectsDir + "/" + commitHash;
        writeFile(commitPath, commitContent);

        // Update branch pointer
        branches[head] = commitHash;
        saveBranches();

        stagingArea.clear();
        saveIndex();

        std::cout << "Committed to " << head << ": " << commitHash << "\n";
    }

    void log() {
        loadBranches();
        head = readFile(headFile);
        if (!head.empty()) head.erase(head.find_last_not_of(" \n\r\t")+1);
        std::string current = "";
        std::unordered_map<std::string, std::string>::const_iterator itb = branches.find(head);
        if (itb != branches.end()) {
            current = itb->second;
        }

        while (!current.empty()) {
            std::string commitPath = objectsDir + "/" + current;
            std::string content = readFile(commitPath);
            if (content.empty()) break;

            std::istringstream iss(content);
            std::string line;
            std::string parent, parent2, message;
            std::time_t date = 0;
            while (std::getline(iss, line)) {
                if (line.find("parent ") == 0) parent = line.substr(7);
                else if (line.find("parent2 ") == 0) parent2 = line.substr(8); 
                else if (line.find("date ") == 0) date = std::stoll(line.substr(5));
                else if (line.find("message ") == 0) message = line.substr(8);
            }
            std::cout << "Commit: " << current << "\n";
            if (!parent2.empty()) {
                std::cout << "Merge: " << parent2.substr(0, 7) << "\n";
            }
            std::cout << "Date: " << std::ctime(&date);
            std::cout << "Message: " << message << "\n\n";
            current = parent;
        }
    }

    void branch(const std::string& name) {
        loadBranches();
        if (branches.count(name)) {
            std::cout << "Branch already exists: " << name << "\n";
            return;
        }
        loadBranches();
        head = readFile(headFile);
        if (!head.empty()) head.erase(head.find_last_not_of(" \n\r\t")+1);
        std::unordered_map<std::string, std::string>::const_iterator itb = branches.find(head);
        std::string currentCommit = "";
        if (itb != branches.end()) currentCommit = itb->second;
        branches[name] = currentCommit;
        saveBranches();
        std::cout << "Created branch " << name << "\n";
    }

    void checkout(const std::string& name) {
        loadBranches();
        if (branches.count(name)) {
            head = name;
            writeFile(headFile, head);
            restoreCommit(branches[head]);
            std::cout << "Switched to branch " << name << "\n";
        } else if (restoreCommit(name)) {
            std::cout << "Checked out commit " << name << "\n";
        } else {
            std::cout << "Branch or commit not found: " << name << "\n";
        }
    }

private:
    void saveIndex() {
        std::ofstream ofs(indexFile.c_str());
        std::unordered_set<std::string>::const_iterator it;
        for (it = stagingArea.begin(); it != stagingArea.end(); ++it) {
            ofs << *it << "\n";
        }
    }

    void loadIndex() {
        stagingArea.clear();
        std::ifstream ifs(indexFile.c_str());
        std::string line;
        while (std::getline(ifs, line)) {
            if (!line.empty()) stagingArea.insert(line);
        }
    }

    void saveBranches() {
        std::ofstream ofs(branchesFile.c_str());
        for (const auto& pair : branches) {
            ofs << pair.first << " " << pair.second << "\n";
        
        }
    }

    void loadBranches() {
        branches.clear();
        std::ifstream ifs(branchesFile.c_str());
        std::string line;
        while (std::getline(ifs, line)) {
            if (line.empty()) continue;
            size_t pos = line.find(' ');
            if (pos == std::string::npos) continue;
            std::string name = line.substr(0, pos);
            std::string hash = line.substr(pos + 1);
            branches[name] = hash;
        }
    }

    std::unordered_map<std::string, std::string>loadCommitFiles(const std::string& commitHash) {
        std::unordered_map<std::string, std::string> files;
        std::string commitPath = objectsDir + "/" + commitHash;
        std::string content = readFile(commitPath);
        std::string headFile;
        std::string objectsDir;
        std::unordered_map<std::string, std::string> branches;

        if (content.empty()) return files;
        std::istringstream iss(content);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.find("file ") == 0) {
                size_t pos1 = line.find(' ', 5);
                if (pos1 != std::string::npos) {
                    std::string fname = line.substr(5, pos1 - 5);
                    std::string blob = line.substr(pos1 + 1);
                    files[fname] = blob;
                }
            }
        }
        return files;
    }

    bool restoreCommit(const std::string& commitHash) {
        if (commitHash.empty()) return false;
        std::unordered_map<std::string, std::string> files = loadCommitFiles(commitHash);
        std::unordered_map<std::string, std::string>::const_iterator it;
        for (it = files.begin(); it != files.end(); ++it) {
            std::string blobPath = objectsDir + "/" + it->second;
            std::string content = readFile(blobPath);
            if (!content.empty()) {
                writeFile(it->first, content);
            }
        }
        return true;
    }
};

int main(int argc, char* argv[]) {
    MiniGit mg;

    if (argc < 2) {
        std::cout << "Usage: minigit <command> [args]\n";
        return 1;
    }

    std::string cmd = argv[1];

    if (cmd == "init") {
        mg.init();
    } else if (cmd == "add" && argc == 3) {
        mg.add(argv[2]);
    } else if (cmd == "commit" && argc == 4 && std::string(argv[2]) == "-m") {
        mg.commit(argv[3]);
    } else if (cmd == "log") {
        mg.log();
    } else if (cmd == "branch" && argc == 3) {
        mg.branch(argv[2]);
    } else if (cmd == "checkout" && argc == 3) {
        mg.checkout(argv[2]);
    } else if (cmd == "merge" && argc == 3) {
        mg.merge(argv[2]);
    }
    else if (cmd == "diff" && argc == 4) {
        mg.diff(argv[2], argv[3]);
    }
    else {
        std::cout << "Unknown or incomplete command.\n";
    }

    return 0;
}

void MiniGit::merge(const std::string& otherBranch) {
    loadBranches();
    if (!branches.count(otherBranch)) {
        std::cout << "Branch not found: " << otherBranch << "\n";
        return;
    }

    head = readFile(headFile);
    if (!head.empty()) head.erase(head.find_last_not_of("\n\r\t") + 1);

    std::string ourCommit = branches[head];
    std::string theirCommit = branches[otherBranch];
    std::string baseCommit = findLCA(ourCommit, theirCommit);

    auto base = loadCommitFiles(baseCommit);
    auto ours = loadCommitFiles(ourCommit);
    auto theirs = loadCommitFiles(theirCommit);

    std::unordered_set<std::string> allFiles;
    for (const auto& pair : base) allFiles.insert(pair.first);
    for (const auto& pair : ours) allFiles.insert(pair.first);
    for (const auto& pair : theirs) allFiles.insert(pair.first);

    bool hasConflicts = false;

    for (const auto& file : allFiles) {
        std::string baseContent = base.count(file) ? readFile(objectsDir + "/" + base[file]) : "";
        std::string ourContent = ours.count(file) ? readFile(objectsDir + "/" + ours[file]) : "";
        std::string theirContent = theirs.count(file) ? readFile(objectsDir + "/" + theirs[file]) : "";

        if (ourContent == theirContent) {
            if (!ourContent.empty()) writeFile(file, ourContent);
        } 
        else if (baseContent.empty()) {
            if (!ourContent.empty() && !theirContent.empty()) {
                hasConflicts = true;
                std::cout << "CONFLICT: both modified " << file << "\n";
                std::string merged = "<<<<<<< HEAD (" + head + ")\n" + ourContent 
                                   + "\n=======\n" + theirContent 
                                   + "\n>>>>>>> " + otherBranch + "\n";
                writeFile(file, merged);
            } 
            else if (!theirContent.empty()) {
                writeFile(file, theirContent);
            }
        } 
        else if (ourContent.empty() && baseContent == theirContent) {
            remove(file.c_str()); // deleted in ours, unchanged in theirs
        } 
        else if (theirContent.empty() && baseContent == ourContent) {
            continue; // deleted in theirs, unchanged in ours
        } 
        else if (baseContent == ourContent) {
            writeFile(file, theirContent);
        } 
        else if (baseContent == theirContent) {
            writeFile(file, ourContent);
        } 
        else {
            hasConflicts = true;
            std::cout << "CONFLICT: both modified " << file << "\n";
            std::string merged = "<<<<<<< HEAD (" + head + ")\n" + ourContent 
                               + "\n=======\n" + theirContent 
                               + "\n>>>>>>> " + otherBranch + "\n";
            writeFile(file, merged);
        }

        if (!ourContent.empty() || !theirContent.empty()) {
            add(file);
        }
    }

    if (hasConflicts) {
        std::cout << "Automatic merge failed; fix conflicts and commit the result.\n";
    } else {
        commit("Merge branch " + otherBranch, theirCommit);
    }
}


std::string MiniGit::findLCA(const std::string& a, const std::string& b) {
    std::unordered_set<std::string> ancestors;
    std::string current = a;
    while (!current.empty()) {
        ancestors.insert(current);
        std::string content = readFile(objectsDir + "/" + current);
        std::istringstream iss(content);
        std::string line;
        bool foundParent = false;
        while (std::getline(iss, line)) {
            if(line.find("parent") == 0) {
                current = line.substr(7);
                foundParent = true;
                break;
            }
        }
        if (!foundParent) break;
    }
    current = b;
    while (!current.empty()) {
        if(ancestors.count(current)) return current;
        std::string content = readFile(objectsDir + "/" + current);
        std::istringstream iss(content);
        std::string line;
        bool foundParent = false;
        while (std::getline(iss, line)) {
            if (line.find("parent") == 0) {
                current = line.substr(7);
                foundParent = true;
                break;
            }
        }
        if(!foundParent) break;
    }
    return "";
    
}

void MiniGit::diff(const std::string& commit1, const std::string& commit2) {
    auto files1 = loadCommitFiles(commit1);
    auto files2 = loadCommitFiles(commit2);
    
    std::unordered_set<std::string> allFiles;
    for (const auto& pair : files1) allFiles.insert(pair.first);
    for (const auto& pair : files2) allFiles.insert(pair.first);

    for (const auto& file : allFiles) {
        std::string content1 = files1.count(file) ? readFile(objectsDir + "/" + files1[file]) : "";
        std::string content2 = files2.count(file) ? readFile(objectsDir + "/" + files2[file]) : "";

        if (content1 != content2) {
            std::cout << "--- " << file << " (" << commit1.substr(0,7) << ")\n";
            std::cout << "+++ " << file << " (" << commit2.substr(0,7) << ")\n";
            
            std::istringstream iss1(content1);
            std::istringstream iss2(content2);
            std::string line1, line2;
            int lineNum = 1;
            
            while (std::getline(iss1, line1) || std::getline(iss2, line2)) {
                if (line1 != line2) {
                    std::cout << "@@ -" << lineNum << " +" << lineNum << " @@\n";
                    if (!line1.empty()) std::cout << "-" << line1 << "\n";
                    if (!line2.empty()) std::cout << "+" << line2 << "\n";
                }
                lineNum++;
            }
            std::cout << "\n";
        }
    }
}