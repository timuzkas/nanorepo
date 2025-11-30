#include "httplib.h"
#include "template.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <random>
#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream> // For logging
#include <iomanip>  // For put_time
#include <ctime>    // For localtime, time_t

using namespace std;
using namespace std::chrono;
namespace fs = std::filesystem;

// Helper function for logging with timestamp
void log_message(const string& msg) {
    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    time_t t = system_clock::to_time_t(now);
    cout << "[" << put_time(localtime(&t), "%H:%M:%S") << "." << setfill('0') << setw(3) << ms.count() << "] " << msg << endl;
}

string PIN;
const string PIN_FILE = "uploads/.pin";

// Utilities
string getMimeType(const string& f) {
    const string ext = fs::path(f).extension().string();
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png") return "image/png";
    if (ext == ".gif") return "image/gif";
    if (ext == ".webp") return "image/webp";
    if (ext == ".mp4") return "video/mp4";
    if (ext == ".webm") return "video/webm";
    if (ext == ".mov") return "video/mov";
    if (ext == ".mpv") return "video/mpv";
    if (ext == ".ogg") return "video/ogg";
    return "application/octet-stream";
}

string sanitize(const string& s) {
    string r;
    for (char c : s) {
        unsigned char uc = static_cast<unsigned char>(c);

        if (uc > 127) {
            r += c;
            continue;
        }

        if (isalnum(uc)) {
            r += c;
            continue;
        }
        
        if (c == ' ' || c == '-' || c == '_' || c == '.') {
            r += c;
        }
    }

    if (r.empty() || r == "." || r == "..") return "unnamed";
    
    return r;
}

string genFilename(const string& orig) {
    random_device rd; mt19937 gen(rd());
    auto t = chrono::duration_cast<chrono::seconds>(
        chrono::system_clock::now().time_since_epoch()).count();
    return to_string(t) + "_" + to_string(uniform_int_distribution<>(1000, 9999)(gen)) + 
           fs::path(orig).extension().string();
}

bool auth(const httplib::Request& req) {
    return !PIN.empty() && req.has_header("X-PIN") && req.get_header_value("X-PIN") == PIN;
}

void loadPin() {
    if (fs::exists(PIN_FILE)) {
        ifstream f(PIN_FILE);
        if (f) getline(f, PIN);
    } else if (getenv("PIN")) {
        PIN = getenv("PIN");
        ofstream(PIN_FILE) << PIN;
    }
}

vector<string> listAlbums() {
    vector<string> a;
    if (!fs::exists("uploads")) return a;
    try {
        for (auto& e : fs::directory_iterator("uploads")) {
            if (e.is_directory() && e.path().filename().string()[0] != '.') {
                a.push_back(e.path().filename().string());
            }
        }
        sort(a.begin(), a.end());
    } catch (...) {}
    return a;
}

vector<string> listMedia(const string& album) {
    vector<string> p;
    const string path = "uploads/" + sanitize(album);
    if (!fs::exists(path)) return p;
    try {
        for (auto& e : fs::directory_iterator(path)) {
            if (e.is_regular_file() && e.path().extension() != ".pin") {
                p.push_back(e.path().filename().string());
            }
        }
        sort(p.rbegin(), p.rend());
    } catch (...) {}
    return p;
}

// API
void pinStatus(const httplib::Request&, httplib::Response& res) {
    res.set_content(PIN.empty() ? "false" : "true", "text/plain");
}

void setPin(const httplib::Request& req, httplib::Response& res) {
    if (!PIN.empty() && !auth(req)) {
        res.status = 401;
        res.set_content("Unauthorized", "text/plain");
        return;
    }
    if (!req.has_param("pin")) {
        res.status = 400;
        res.set_content("Missing PIN", "text/plain");
        return;
    }
    PIN = req.get_param_value("pin");
    ofstream(PIN_FILE) << PIN;
    res.set_content("PIN set", "text/plain");
}

void getAlbums(const httplib::Request& req, httplib::Response& res) {
    try {
        if (!auth(req)) {
            res.status = 401;
            res.set_content("[]", "application/json");
            return;
        }
        auto albums = listAlbums();
        stringstream j; j << "[";
        for (size_t i = 0; i < albums.size(); ++i) {
            j << "\"" << albums[i] << "\"";
            if (i < albums.size() - 1) j << ",";
        }
        j << "]";
        res.set_content(j.str(), "application/json");
    } catch (...) {
        res.status = 500;
        res.set_content("[]", "application/json");
    }
}

void createAlbum(const httplib::Request& req, httplib::Response& res) {
    try {
        if (!auth(req)) {
            res.status = 401;
            res.set_content("Unauthorized", "text/plain");
            return;
        }
        if (!req.has_param("name")) {
            res.status = 400;
            res.set_content("Missing name", "text/plain");
            return;
        }
        const string name = sanitize(req.get_param_value("name"));
        if (fs::create_directories("uploads/" + name)) {
            res.set_content("Album created", "text/plain");
        } else {
            res.status = 500;
            res.set_content("Failed", "text/plain");
        }
    } catch (...) {
        res.status = 500;
        res.set_content("Error", "text/plain");
    }
}

void deleteAlbum(const httplib::Request& req, httplib::Response& res) {
    try {
        if (!auth(req)) {
            res.status = 401;
            res.set_content("Unauthorized", "text/plain");
            return;
        }
        const string name = sanitize(req.path_params.at("name"));
        fs::remove_all("uploads/" + name);
        res.set_content("Deleted", "text/plain");
    } catch (...) {
        res.status = 500;
        res.set_content("Failed", "text/plain");
    }
}

void getMedia(const httplib::Request& req, httplib::Response& res) {
    try {
        const string album = sanitize(req.path_params.at("album"));
        auto photos = listMedia(album);
        stringstream j; j << "[";
        for (size_t i = 0; i < photos.size(); ++i) {
            j << "\"" << photos[i] << "\"";
            if (i < photos.size() - 1) j << ",";
        }
        j << "]";
        res.set_content(j.str(), "application/json");
    } catch (...) {
        res.status = 500;
        res.set_content("[]", "application/json");
    }
}

void uploadMedia(const httplib::Request& req, httplib::Response& res) {
    log_message("Handling /upload request for album: " + (req.has_param("album") ? req.get_param_value("album") : "N/A"));
    try {
        if (!auth(req)) {
            res.status = 401;
            res.set_content("Unauthorized", "text/plain");
            return;
        }
        
        // Check for album in form fields OR query parameters
        string album;
        if (req.has_param("album")) {
            album = req.get_param_value("album");
        } else {
            // Try to get album from path if it exists
            auto album_it = req.path_params.find("album");
            if (album_it != req.path_params.end()) {
                album = album_it->second;
            } else {
                log_message("Upload failed: Missing album parameter.");
                res.status = 400;
                res.set_content("Missing album parameter", "text/plain");
                return;
            }
        }
        
        album = sanitize(album);
        const string path = "uploads/" + album;
        
        if (!fs::exists(path)) {
            log_message("Upload failed: Album not found: " + album);
            res.status = 404;
            res.set_content("Album not found", "text/plain");
            return;
        }
        
        int uploaded = 0;
        for (auto& [field, file] : req.form.files) {
            if (field != "media") continue;
            const string out = path + "/" + genFilename(file.filename);
            ofstream(out, ios::binary).write(file.content.data(), file.content.size());
            uploaded++;
        }
        
        log_message("Uploaded " + to_string(uploaded) + " media items to album: " + album);
        res.set_content(uploaded ? ("Uploaded " + to_string(uploaded) + " media items") : "No files uploaded", "text/plain");
    } catch (const exception& e) {
        log_message("Upload failed: " + string(e.what()));
        res.status = 500;
        res.set_content("Upload failed", "text/plain");
    } catch (...) {
        log_message("Upload failed: Unknown error.");
        res.status = 500;
        res.set_content("Upload failed", "text/plain");
    }
}

void deleteMedia(const httplib::Request& req, httplib::Response& res) {
    try {
        if (!auth(req)) {
            res.status = 401;
            res.set_content("Unauthorized", "text/plain");
            return;
        }
        const string album = sanitize(req.path_params.at("album"));
        const string file = req.path_params.at("filename");
        fs::remove("uploads/" + album + "/" + file);
        res.set_content("Deleted", "text/plain");
    } catch (...) {
        res.status = 500;
        res.set_content("Failed", "text/plain");
    }
}

string url_decode(const string& str) {
    string result;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            int hex1 = (str[i+1] >= '0' && str[i+1] <= '9') ? str[i+1] - '0' : 
                      (str[i+1] >= 'A' && str[i+1] <= 'F') ? str[i+1] - 'A' + 10 :
                      (str[i+1] >= 'a' && str[i+1] <= 'f') ? str[i+1] - 'a' + 10 : 0;
            int hex2 = (str[i+2] >= '0' && str[i+2] <= '9') ? str[i+2] - '0' : 
                      (str[i+2] >= 'A' && str[i+2] <= 'F') ? str[i+2] - 'A' + 10 :
                      (str[i+2] >= 'a' && str[i+2] <= 'f') ? str[i+2] - 'a' + 10 : 0;
            result += static_cast<char>(hex1 * 16 + hex2);
            i += 2;
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

string url_encode(const string &value) {
    ostringstream escaped;
    escaped.fill('0');
    escaped << hex;

    for (string::const_iterator i = value.begin(), n = value.end(); i != n; ++i) {
        string::value_type c = (*i);
        // Keep alphanumeric and other safe characters
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
            continue;
        }
        // Any other character is percent-encoded
        escaped << uppercase;
        escaped << '%' << setw(2) << int((unsigned char) c);
        escaped << nouppercase;
    }
    return escaped.str();
}

// Pages
void serveGallery(const httplib::Request& req, httplib::Response& res) {
    log_message("Serving gallery for event: " + (req.path_params.count("event") ? url_decode(req.path_params.at("event")) : "N/A"));
    Template tmpl = Template::fromFile("gallery.html");
    tmpl.clear();

	tmpl.addFilter("is_video", [](const vector<string>& args) {
	    if (args.empty()) return "false";
	    
	    static const vector<string> video_exts = {".mp4", ".webm", ".mov", ".mpg", ".ogg", ".mkv"};
	    string filename = args[0];
	    
	    for (const auto& ext : video_exts) {
	        if (filename.length() >= ext.length() && 
	            filename.substr(filename.length() - ext.length()) == ext) {
	            return "true";
	        }
	    }
	    return "false";
	});
    
    string event_from_url;
    if (!req.path_params.empty() && req.path_params.count("event")) {
        event_from_url = url_decode(req.path_params.at("event"));
    }
    
    auto albums = listAlbums();
    
    string current_album = "";
    for (const auto& album : albums) {
        if (album == event_from_url) {
            current_album = album;
            break;
        }
    }
    
    tmpl.set("site_title", "Photo Gallery")
        .set("current_album", current_album)
        .setList("albums", albums);
    
    if (!current_album.empty()) {
        tmpl.setList("photos", listMedia(current_album));
    }
    
    res.set_content(tmpl.render(), "text/html");
}

void serveAdmin(const httplib::Request& req, httplib::Response& res) {
    log_message("Serving admin page.");
    Template tmpl = Template::fromFile("admin.html");
    tmpl.clear();
    
    tmpl.set("pin_set", PIN.empty() ? "false" : "true");
    res.set_content(tmpl.render(), "text/html");
}

void serveFile(const httplib::Request& req, httplib::Response& res) {
    log_message("Serving file: " + req.path_params.at("filename") + " from event: " + req.path_params.at("event"));
    try {
        const string path = "uploads/" + sanitize(req.path_params.at("event")) + "/" + req.path_params.at("filename");
        if (!fs::exists(path)) {
            res.status = 404;
            res.set_content("Not found", "text/plain");
            return;
        }
        ifstream f(path, ios::binary);
        res.set_content(string((istreambuf_iterator<char>(f)), {}), getMimeType(path));
    } catch (...) {
        res.status = 500;
        res.set_content("Error", "text/plain");
    }
}

string base64_decode(const string& input) {
    static const string base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    
    string result;
    int val = 0, valb = -8;
    
    for (unsigned char c : input) {
        if (c == '=') break;
        if (base64_chars.find(c) == string::npos) continue;
        
        val = (val << 6) + base64_chars.find(c);
        valb += 6;
        
        if (valb >= 0) {
            result.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return result;
}


void serveFavicon(const httplib::Request&, httplib::Response& res) {
    string b64_favicon = "PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAxMDAgMTAwIj48cmVjdCB4PSIyMCIgeT0iMzAiIHdpZHRoPSI2MCIgaGVpZ2h0PSI0NSIgcng9IjUiIGZpbGw9IiM2MzY2ZjEiIHN0cm9rZT0iIzRmNDZlNSIgc3Ryb2tlLXdpZHRoPSIyIi8+PHJlY3QgeD0iMjUiIHk9IjM1IiB3aWR0aD0iNTAiIGhlaWdodD0iMzUiIHJ4PSIzIiBmaWxsPSIjZmZmZmZmIi8+PGNpcmNsZSBjeD0iNTAiIGN5PSI1MiIgcj0iMTIiIGZpbGw9IiM2MzY2ZjEiLz48Y2lyY2xlIGN4PSI1MCIgY3k9IjUyIiByPSI4IiBmaWxsPSIjZmZmZmZmIi8+PHJlY3QgeD0iMTUiIHk9IjI1IiB3aWR0aD0iMTIiIGhlaWdodD0iOCIgcng9IjIiIGZpbGw9IiM2MzY2ZjEiLz48cmVjdCB4PSI3MyIgeT0iMjUiIHdpZHRoPSIxMiIgaGVpZ2h0PSI4IiByeD0iMiIgZmlsbD0iIzYzNjZmMSIvPjxjaXJjbGUgY3g9IjM1IiBjeT0iNDIiIHI9IjMiIGZpbGw9IiM2MzY2ZjEiLz48L3N2Zz4=";
    
    string favicon_svg = base64_decode(b64_favicon);
    res.set_content(favicon_svg, "image/svg+xml");
}

int main() {
    auto startup_start_time = chrono::high_resolution_clock::now();
    
    loadPin();
    int port = 8080;
    if (const char* env_p = getenv("PORT")) port = stoi(env_p);
    
    fs::create_directories("uploads");
    
    httplib::Server svr;

    // --- CRITICAL CONFIGURATION FOR BIG UPLOADS ---
    // 1. Allow up to 1GB payload
    svr.set_payload_max_length(1024 * 1024 * 1024); 
    // 2. 10 Minute timeout for slow connections
    svr.set_read_timeout(600, 0); 
    svr.set_write_timeout(600, 0);
    // ----------------------------------------------

    // Request logging middleware
    svr.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        log_message("REQ: " + req.method + " " + req.path + " (Status: " + to_string(res.status) + ")");
    });

    svr.Get("/favicon.ico", serveFavicon);
    svr.Get("/admin", serveAdmin);

    // Standard Routes
    svr.Get("/", [](const httplib::Request& req, httplib::Response& res) {
        auto albums = listAlbums();
        if (albums.empty()) res.set_content("No albums.", "text/plain");
        else res.set_redirect(("/" + url_encode(albums[0])).c_str());
    });    
    svr.Get("/:event", serveGallery);
    svr.Get("/uploads/:event/:filename", serveFile);
    
    // API Routes
    svr.Get("/api/pin/status", pinStatus);
    svr.Post("/api/pin", setPin);
    svr.Get("/api/albums", getAlbums);
    svr.Post("/api/albums", createAlbum);
    svr.Delete("/api/albums/:name", deleteAlbum);
    svr.Get("/api/albums/:album/media", getMedia);
    svr.Delete("/api/albums/:album/media/:filename", deleteMedia);

    // --- NEW STREAMING UPLOAD HANDLER ---
    // This accepts raw binary data instead of multipart/form-data
    svr.Post("/api/stream_upload", [&](const httplib::Request &req, httplib::Response &res, const httplib::ContentReader &content_reader) {
            log_message("Handling /api/stream_upload request for album: " + url_decode(req.get_header_value("X-Album")));
            if (!auth(req)) {
                res.status = 401;
                res.set_content("Unauthorized", "text/plain");
                return;
            }
    
            if (!req.has_header("X-Album") || !req.has_header("X-Filename")) {
                res.status = 400;
                res.set_content("Missing Headers", "text/plain");
                return;
            }
    
            string album = sanitize(url_decode(req.get_header_value("X-Album")));
            string filename = url_decode(req.get_header_value("X-Filename"));
            
            // Check for Offset (Default to 0 if missing)
            size_t offset = 0;
            if (req.has_header("X-Offset")) {
                try {
                    offset = stoull(req.get_header_value("X-Offset"));
                } catch (...) { offset = 0; }
            }
    
            string dir_path = "uploads/" + album;
            if (!fs::exists(dir_path)) {
                res.status = 404;
                res.set_content("Album not found", "text/plain");
                return;
            }
    
            string final_path = dir_path + "/" + genFilename(filename); 
            
            // IMPORTANT: If offset > 0, we APPEND. If offset == 0, we OVERWRITE.
            // Note: genFilename includes a timestamp, so for chunking to work, 
            // the JS must send the exact SAME filename header for all chunks.
            // However, your genFilename randomizes the name. 
            // WE MUST DISABLE RANDOMIZATION FOR CHUNKS to work, or rely on client sending the target name.
            // For simplicity, we will trust the client's filename here to allow appending.
            
            final_path = dir_path + "/" + filename; // TRUST CLIENT FILENAME FOR CHUNKING
    
            ios_base::openmode mode = ios::binary;
            if (offset > 0) {
                mode |= ios::app; // Append mode
            } else {
                mode |= ios::trunc; // Overwrite mode
            }
    
            ofstream ofs(final_path, mode);
            if (!ofs) {
                res.status = 500;
                res.set_content("Cannot open file", "text/plain");
                return;
            }
    
            content_reader([&](const char *data, size_t data_length) {
                ofs.write(data, data_length);
                return true;
            });
    
            res.set_content("Chunk Received", "text/plain");
        });
    // ------------------------------------

    auto startup_end_time = chrono::high_resolution_clock::now();
    auto startup_duration_ms = duration_cast<milliseconds>(startup_end_time - startup_start_time).count();
    log_message("Server startup completed in " + to_string(startup_duration_ms) + "ms. Listening on port " + to_string(port));
    svr.listen("0.0.0.0", port);
    return 0;
}


