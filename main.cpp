#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <set>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <csignal>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace fs = std::filesystem;

std::string g_json_data;
std::mutex g_json_mutex;
std::atomic<bool> g_running{true};
int g_server_socket = -1;
std::thread g_http_thread;

const std::set<std::string> image_ext = {".jpg", ".jpeg", ".png", ".gif", ".bmp", ".svg", ".webp"};
const std::set<std::string> audio_ext = {".mp3", ".wav", ".ogg", ".flac", ".aac", ".m4a", ".wma"};
const std::set<std::string> video_ext = {".mp4", ".mpg", ".mpeg", ".avi", ".mkv", ".mov", ".wmv", ".flv", ".webm"};

std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;
        }
    }
    return out;
}

std::string scan_directory(const std::string& root) {
    std::set<std::string> images, audios, videos;
    if (!fs::exists(root) || !fs::is_directory(root))
        return "{}";

    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::string fname = entry.path().filename().string();

        for (auto& c : ext) c = std::tolower(c);

        if (image_ext.count(ext))
            images.insert(fname);
        else if (audio_ext.count(ext))
            audios.insert(fname);
        else if (video_ext.count(ext))
            videos.insert(fname);
    }

    std::string json = "{\n";

    json += "  \"audio\": [";
    bool first = true;
    for (const auto& f : audios) {
        if (!first) json += ", ";
        json += "\"" + json_escape(f) + "\"";
        first = false;
    }
    json += "],\n";

    json += "  \"video\": [";
    first = true;
    for (const auto& f : videos) {
        if (!first) json += ", ";
        json += "\"" + json_escape(f) + "\"";
        first = false;
    }
    json += "],\n";

    json += "  \"images\": [";
    first = true;
    for (const auto& f : images) {
        if (!first) json += ", ";
        json += "\"" + json_escape(f) + "\"";
        first = false;
    }
    json += "]\n";

    json += "}";
    return json;
}

void handle_http_client(int client_fd) {
    char buf[4096];
    ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    buf[n] = '\0';

    std::string request(buf, n);
    size_t line_end = request.find("\r\n");
    if (line_end == std::string::npos) line_end = request.find("\n");
    std::string first_line = request.substr(0, line_end);

    bool is_get_media = (first_line.find("GET /media_files") == 0);

    std::string response;
    if (is_get_media) {
        std::lock_guard<std::mutex> lock(g_json_mutex);
        std::string body = g_json_data;
        response = "HTTP/1.0 200 OK\r\n"
                   "Content-Type: application/json\r\n"
                   "Content-Length: " + std::to_string(body.size()) + "\r\n"
                   "Connection: close\r\n"
                   "\r\n" + body;
    } else {
        std::string body = "{\"error\": \"not found\"}";
        response = "HTTP/1.0 404 Not Found\r\n"
                   "Content-Type: application/json\r\n"
                   "Content-Length: " + std::to_string(body.size()) + "\r\n"
                   "Connection: close\r\n"
                   "\r\n" + body;
    }

    send(client_fd, response.c_str(), response.size(), 0);
    close(client_fd);
}

void http_server_thread() {
    g_server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_socket < 0) {
        std::cerr << "Failed to create socket\n";
        return;
    }

    int opt = 1;
    setsockopt(g_server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(1234);

    if (bind(g_server_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind port 1234\n";
        close(g_server_socket);
        return;
    }

    if (listen(g_server_socket, 5) < 0) {
        std::cerr << "Failed to listen\n";
        close(g_server_socket);
        return;
    }

    std::cout << "HTTP server listening on http://localhost:1234\n";

    while (g_running) {
        int client_fd = accept(g_server_socket, nullptr, nullptr);
        if (client_fd < 0) {
            if (g_running) perror("accept");
            break;
        }
        handle_http_client(client_fd);
    }
    close(g_server_socket);
}

void signal_handler(int) {
    g_running = false;
    if (g_server_socket >= 0) {
        shutdown(g_server_socket, SHUT_RDWR);
        close(g_server_socket);
    }
}

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  -i <seconds>   Scan interval (default: 60)\n"
              << "  -d <path>      Directory to scan (default: $HOME)\n"
              << "  --http         Serve JSON via HTTP on port 1234 instead of writing file\n"
              << "  -h, --help     Show this help\n";
}

int main(int argc, char* argv[]) {
    int interval_sec = 60;
    std::string scan_dir = getenv("HOME") ? getenv("HOME") : "/tmp";
    bool use_http = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-i" && i + 1 < argc) {
            interval_sec = std::stoi(argv[++i]);
            if (interval_sec <= 0) interval_sec = 60;
        } else if (arg == "-d" && i + 1 < argc) {
            scan_dir = argv[++i];
        } else if (arg == "--http") {
            use_http = true;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (use_http) {
        g_http_thread = std::thread(http_server_thread);
    }

    std::string output_path;
    if (!use_http) {
        output_path = std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.media_files";
    }

    while (g_running) {
        std::string json = scan_directory(scan_dir);

        if (use_http) {
            std::lock_guard<std::mutex> lock(g_json_mutex);
            g_json_data = json;
            std::cout << "JSON updated (" << json.size() << " bytes)\n";
        } else {
            std::ofstream ofs(output_path);
            if (ofs) {
                ofs << json;
                std::cout << "Written to " << output_path << "\n";
            } else {
                std::cerr << "Failed to write " << output_path << "\n";
            }
        }

        for (int waited = 0; waited < interval_sec * 10 && g_running; ++waited) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    if (use_http && g_http_thread.joinable()) {
        g_running = false;
        if (g_server_socket >= 0) {
            shutdown(g_server_socket, SHUT_RDWR);
            close(g_server_socket);
        }
        g_http_thread.join();
    }

    std::cout << "Media scanner stopped.\n";
    return 0;
}
