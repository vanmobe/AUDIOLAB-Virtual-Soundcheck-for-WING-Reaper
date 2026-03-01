/*
 * Configuration Management
 * Load/save extension configuration
 */

#include "wing_config.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <filesystem>
#include <exception>

// Simple JSON parser (minimal for our needs)
// For production, consider using a library like nlohmann/json or rapidjson

namespace WingConnector {

std::string WingConfig::GetConfigPath() {
    // Get user config directory
    const char* home = getenv("HOME");
    if (!home) home = getenv("USERPROFILE"); // Windows
    
    if (home) {
        return std::string(home) + "/.wingconnector/config.json";
    }
    
    return "config.json"; // Fallback to current directory
}

bool WingConfig::LoadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }
    
    // Simple JSON parsing (very basic)
    // TODO: Replace with proper JSON library
    std::string line;
    while (std::getline(file, line)) {
        // Remove whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        
        if (line.find("\"wing_ip\"") != std::string::npos) {
            size_t start = line.find(":") + 1;
            size_t quote1 = line.find("\"", start);
            size_t quote2 = line.find("\"", quote1 + 1);
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                wing_ip = line.substr(quote1 + 1, quote2 - quote1 - 1);
            }
        }
        else if (line.find("\"wing_port\"") != std::string::npos) {
            size_t start = line.find(":") + 1;
            std::string num_str = line.substr(start);
            wing_port = (uint16_t)std::stoi(num_str);
        }
        else if (line.find("\"listen_port\"") != std::string::npos) {
            size_t start = line.find(":") + 1;
            std::string num_str = line.substr(start);
            listen_port = (uint16_t)std::stoi(num_str);
        }
        else if (line.find("\"channel_count\"") != std::string::npos) {
            size_t start = line.find(":") + 1;
            std::string num_str = line.substr(start);
            channel_count = std::stoi(num_str);
        }
        else if (line.find("\"timeout\"") != std::string::npos) {
            size_t start = line.find(":") + 1;
            std::string num_str = line.substr(start);
            timeout_ms = std::stoi(num_str) * 1000; // Convert to ms
        }
        else if (line.find("\"create_stereo_pairs\"") != std::string::npos) {
            create_stereo_pairs = (line.find("true") != std::string::npos);
        }
        else if (line.find("\"color_tracks\"") != std::string::npos) {
            color_tracks = (line.find("true") != std::string::npos);
        }
        else if (line.find("\"track_prefix\"") != std::string::npos) {
            size_t start = line.find(":") + 1;
            size_t quote1 = line.find("\"", start);
            size_t quote2 = line.find("\"", quote1 + 1);
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                track_prefix = line.substr(quote1 + 1, quote2 - quote1 - 1);
            }
        }
        else if (line.find("\"include_channels\"") != std::string::npos) {
            size_t start = line.find(":") + 1;
            size_t quote1 = line.find("\"", start);
            size_t quote2 = line.find("\"", quote1 + 1);
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                include_channels = line.substr(quote1 + 1, quote2 - quote1 - 1);
            }
        }
        else if (line.find("\"exclude_channels\"") != std::string::npos) {
            size_t start = line.find(":") + 1;
            size_t quote1 = line.find("\"", start);
            size_t quote2 = line.find("\"", quote1 + 1);
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                exclude_channels = line.substr(quote1 + 1, quote2 - quote1 - 1);
            }
        }
        else if (line.find("\"configure_midi_actions\"") != std::string::npos) {
            configure_midi_actions = (line.find("true") != std::string::npos);
        }
    }
    
    file.close();
    return true;
}

bool WingConfig::SaveToFile(const std::string& filepath) {
    namespace fs = std::filesystem;
    fs::path config_path(filepath);

    try {
        fs::path parent = config_path.parent_path();
        if (!parent.empty() && !fs::exists(parent)) {
            fs::create_directories(parent);
        }
    } catch (const std::exception&) {
        return false;
    }

    std::ofstream file(config_path);
    if (!file.is_open()) {
        return false;
    }
    
    file << "{\n";
    file << "  \"wing_ip\": \"" << wing_ip << "\",\n";
    file << "  \"wing_port\": " << wing_port << ",\n";
    file << "  \"listen_port\": " << listen_port << ",\n";
    file << "  \"channel_count\": " << channel_count << ",\n";
    file << "  \"timeout\": " << (timeout_ms / 1000) << ",\n";
    file << "  \"track_prefix\": \"" << track_prefix << "\",\n";
    file << "  \"color_tracks\": " << (color_tracks ? "true" : "false") << ",\n";
    file << "  \"create_stereo_pairs\": " << (create_stereo_pairs ? "true" : "false") << ",\n";
    file << "  \"include_channels\": \"" << include_channels << "\",\n";
    file << "  \"exclude_channels\": \"" << exclude_channels << "\",\n";
    file << "  \"configure_midi_actions\": " << (configure_midi_actions ? "true" : "false") << ",\n";
    file << "  \"default_track_color\": {\n";
    file << "    \"r\": " << (int)default_color.r << ",\n";
    file << "    \"g\": " << (int)default_color.g << ",\n";
    file << "    \"b\": " << (int)default_color.b << "\n";
    file << "  }\n";
    file << "}\n";
    
    file.close();
    return true;
}

} // namespace WingConnector
