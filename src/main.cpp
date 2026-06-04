#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace {

struct IniSection {
    std::string                        name;
    std::map<std::string, std::string> values;
};

struct Config {
    std::string name = "Traffic Signal Light Simulator";
    std::string             layout = "one-direction";
    std::vector<int>         phase_durations_ms;
    std::string signal_label = "Main Lane";
    std::vector<std::string> signal_states;
};

struct PhaseView {
    std::size_t index = 0;
    int elapsed_ms = 0;
    int remaining_ms = 0;
    int duration_ms = 0;
    long long period_ms = 0;
};

enum class Key {
    Left,
    Right,
    Activate,
    Quit,
    Other
};

constexpr int kMaxPhaseDurationMs        = 24 * 60 * 60 * 1000;
constexpr int kMaxAdvanceSeconds         = 24 * 60 * 60;
constexpr const char* kDefaultConfigPath = "configs/one_way.ini";

std::string trim(const std::string& value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();

    if (first >= last) {
        return "";
    }
    return std::string(first, last);
}

std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

std::vector<std::string> split_csv(const std::string& value, const std::string& key_name) {
    std::vector<std::string> result;
    std::size_t start = 0;

    while (start <= value.size()) {
        const auto comma = value.find(',', start);
        const auto end = (comma == std::string::npos) ? value.size() : comma;
        auto token = trim(value.substr(start, end - start));
        if (token.empty()) {
            throw std::runtime_error("Empty value in " + key_name);
        }
        result.push_back(token);

        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }

    return result;
}

int parse_int_exact(const std::string& token, const std::string& context) {
    try {
        std::size_t parsed_chars = 0;
        const int parsed = std::stoi(token, &parsed_chars);
        if (parsed_chars != token.size()) {
            throw std::runtime_error("trailing characters");
        }
        return parsed;
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid integer for " + context + ": '" + token + "'");
    }
}

std::vector<int> parse_int_csv(const std::string& value, const std::string& key_name) {
    std::vector<int> result;
    for (const auto& token : split_csv(value, key_name)) {
        const int parsed = parse_int_exact(token, key_name);
        if (parsed <= 0) {
            throw std::runtime_error(key_name + " must contain positive durations");
        }
        if (parsed > kMaxPhaseDurationMs) {
            throw std::runtime_error(key_name + " values cannot exceed 24 hours");
        }
        result.push_back(parsed);
    }
    return result;
}

std::vector<IniSection> load_ini(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Unable to open config file: " + path);
    }

    std::vector<IniSection> sections;
    int current_section_index = -1;
    std::string line;
    int line_number = 0;

    while (std::getline(file, line)) {
        ++line_number;
        const auto comment_start = line.find_first_of("#;");
        if (comment_start != std::string::npos) {
            line = line.substr(0, comment_start);
        }

        line = trim(line);
        if (line.empty()) {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            const auto section_name = trim(line.substr(1, line.size() - 2));
            if (section_name.empty()) {
                throw std::runtime_error("Empty INI section name at line " + std::to_string(line_number));
            }

            sections.push_back({section_name, {}});
            current_section_index = static_cast<int>(sections.size() - 1);
            continue;
        }

        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            throw std::runtime_error("Expected key=value at line " + std::to_string(line_number));
        }
        if (current_section_index < 0) {
            throw std::runtime_error("Expected an INI section before line " + std::to_string(line_number));
        }

        const auto key = trim(line.substr(0, equals));
        const auto value = trim(line.substr(equals + 1));
        if (key.empty()) {
            throw std::runtime_error("Empty key at line " + std::to_string(line_number));
        }

        sections[static_cast<std::size_t>(current_section_index)].values[key] = value;
    }

    return sections;
}

const IniSection& require_section(const std::vector<IniSection>& sections, const std::string& name) {
    const auto found = std::find_if(sections.begin(), sections.end(), [&](const IniSection& section) {
        return section.name == name;
    });
    if (found == sections.end()) {
        throw std::runtime_error("Missing required [" + name + "] section");
    }
    return *found;
}

std::string value_or(const std::map<std::string, std::string>& values,
                     const std::string& key,
                     const std::string& fallback) {
    const auto found = values.find(key);
    if (found == values.end()) {
        return fallback;
    }
    return found->second;
}

Config parse_config(const std::string& path) {
    const auto sections = load_ini(path);
    const auto& controller = require_section(sections, "controller").values;
    const auto& signal = require_section(sections, "signal").values;

    Config config;
    config.name = value_or(controller, "name", config.name);
    config.layout = value_or(controller, "layout", config.layout);

    const auto durations_it = controller.find("phase_durations_ms");
    if (durations_it == controller.end()) {
        throw std::runtime_error("controller.phase_durations_ms must be provided");
    }
    config.phase_durations_ms = parse_int_csv(durations_it->second, "phase_durations_ms");

    config.signal_label = value_or(signal, "label", config.signal_label);
    const auto states_it = signal.find("states");
    if (states_it == signal.end()) {
        throw std::runtime_error("signal.states must be provided");
    }
    config.signal_states = split_csv(states_it->second, "states");
    for (auto& state : config.signal_states) {
        state = upper(state);
    }

    if (config.phase_durations_ms.size() != config.signal_states.size()) {
        throw std::runtime_error("phase_durations_ms and signal.states must have the same number of entries");
    }

    return config;
}

long long cycle_period_ms(const Config& config) {
    long long total = 0;
    for (const auto duration : config.phase_durations_ms) {
        if (total > std::numeric_limits<long long>::max() - duration) {
            throw std::runtime_error("Configured cycle period overflow");
        }
        total += duration;
    }
    return total;
}

PhaseView phase_at_time(const Config& config, long long simulated_ms) {
    const long long period = cycle_period_ms(config);
    const long long phase_time = simulated_ms % period;
    long long phase_start = 0;

    for (std::size_t index = 0; index < config.phase_durations_ms.size(); ++index) {
        const int duration = config.phase_durations_ms[index];
        const long long phase_end = phase_start + duration;
        if (phase_time < phase_end) {
            const int elapsed = static_cast<int>(phase_time - phase_start);
            return {index, elapsed, duration - elapsed, duration, period};
        }
        phase_start = phase_end;
    }

    throw std::runtime_error("Unable to resolve phase for simulated time");
}

long long time_until_state(const Config& config,
                           const PhaseView& phase,
                           const std::string& target_state) {
    if (config.signal_states[phase.index] == target_state) {
        return 0;
    }

    long long wait_ms = phase.remaining_ms;
    std::size_t index = (phase.index + 1) % config.signal_states.size();

    for (std::size_t checked = 0; checked < config.signal_states.size(); ++checked) {
        if (config.signal_states[index] == target_state) {
            return wait_ms;
        }
        wait_ms += config.phase_durations_ms[index];
        index = (index + 1) % config.signal_states.size();
    }

    return -1;
}

std::string format_seconds(long long milliseconds) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3)
        << (static_cast<double>(milliseconds) / 1000.0) << "s";
    return out.str();
}

std::string ansi_wrap(const std::string& text, const std::string& code) {
    return "\033[" + code + "m" + text + "\033[0m";
}

std::string colored_state(const std::string& state) {
    if (state == "RED") {
        return ansi_wrap(state, "31;1");
    }
    if (state == "YELLOW") {
        return ansi_wrap(state, "33;1");
    }
    if (state == "GREEN") {
        return ansi_wrap(state, "32;1");
    }
    return ansi_wrap(state, "36;1");
}

std::string traffic_bulb(const std::string& active_state, const std::string& bulb_state) {
    if (active_state != bulb_state) {
        return "[" + ansi_wrap("   ", "100") + "]";
    }
    if (bulb_state == "RED") {
        return "[" + ansi_wrap("   ", "41;1") + "]";
    }
    if (bulb_state == "YELLOW") {
        return "[" + ansi_wrap("   ", "43;1") + "]";
    }
    if (bulb_state == "GREEN") {
        return "[" + ansi_wrap("   ", "42;1") + "]";
    }
    return "[" + ansi_wrap("   ", "46;1") + "]";
}

std::string signal_strip(const std::string& state) {
    return traffic_bulb(state, "RED") + " " +
           traffic_bulb(state, "YELLOW") + " " +
           traffic_bulb(state, "GREEN");
}

std::string vehicle_status(const std::string& state) {
    if (state == "GREEN") {
        return "moving through the intersection";
    }
    if (state == "YELLOW") {
        return "waiting, ready for green";
    }
    if (state == "RED") {
        return "stopped at the red light";
    }
    return "holding for controller state " + state;
}

int scaled_position(int start, int end, int elapsed_ms, int duration_ms) {
    if (duration_ms <= 0) {
        return end;
    }
    const int clamped_elapsed = std::clamp(elapsed_ms, 0, duration_ms);
    const double ratio = static_cast<double>(clamped_elapsed) / static_cast<double>(duration_ms);
    return start + static_cast<int>((end - start) * ratio);
}

int vehicle_position_for_state(const std::string& state,
                               int stop_line,
                               int road_width,
                               int vehicle_width,
                               int phase_elapsed_ms,
                               int phase_duration_ms) {
    int position = stop_line - vehicle_width - 2;

    if (state == "GREEN") {
        position = scaled_position(stop_line - vehicle_width - 1,
                                   road_width - vehicle_width - 2,
                                   phase_elapsed_ms,
                                   phase_duration_ms);
    }

    return std::clamp(position, 0, road_width - vehicle_width);
}

void put_token(std::string& line, std::size_t column, const std::string& token) {
    if (column >= line.size()) {
        return;
    }

    for (std::size_t index = 0; index < token.size() && column + index < line.size(); ++index) {
        line[column + index] = token[index];
    }
}

void print_scene_token(int column, const std::string& token) {
    std::cout << "  " << std::string(static_cast<std::size_t>(std::max(column, 0)), ' ')
              << token << "\n";
}

void render_road_scene(const Config& config,
                       const std::string& state,
                       int phase_elapsed_ms,
                       int phase_duration_ms) {
    constexpr int kRoadWidth = 78;
    constexpr int kStopLine = 44;
    constexpr int kIntersectionEnd = 58;

    const std::vector<std::string> vehicle_sprite = {
        "    ____     ",
        " __/|__|\\__ ", // 
        "O--O----O--O"
    };
    const int vehicle_width = static_cast<int>(vehicle_sprite.front().size());
    const int light_column = kStopLine + 3;
    const int vehicle_position = vehicle_position_for_state(state,
                                                            kStopLine,
                                                            kRoadWidth,
                                                            vehicle_width,
                                                            phase_elapsed_ms,
                                                            phase_duration_ms);

    std::string border = "+" + std::string(kRoadWidth, '-') + "+";
    std::string zone_labels = "|" + std::string(kRoadWidth, ' ') + "|";
    std::string stop_marker = "|" + std::string(kRoadWidth, ' ') + "|";
    std::string vehicle_label = "|" + std::string(kRoadWidth, ' ') + "|";
    std::string vehicle_roof = "|" + std::string(kRoadWidth, ' ') + "|";
    std::string vehicle_body = "|" + std::string(kRoadWidth, ' ') + "|";
    std::string vehicle_wheels = "|" + std::string(kRoadWidth, ' ') + "|";
    std::string lane_markers = "|" + std::string(kRoadWidth, '-') + "|";

    put_token(zone_labels, 3, "APPROACH");
    put_token(zone_labels, static_cast<std::size_t>(kStopLine + 2), "INTERSECTION");
    put_token(zone_labels, static_cast<std::size_t>(kIntersectionEnd + 5), "EXIT");

    put_token(stop_marker, static_cast<std::size_t>(kStopLine - 12), "STOP LINE");
    put_token(stop_marker, static_cast<std::size_t>(kStopLine + 1), "||");
    put_token(stop_marker, static_cast<std::size_t>(kIntersectionEnd + 1), "||");

    put_token(vehicle_roof, static_cast<std::size_t>(1 + kStopLine), "|");
    put_token(vehicle_body, static_cast<std::size_t>(1 + kStopLine), "|");
    put_token(vehicle_wheels, static_cast<std::size_t>(1 + kStopLine), "|");
    put_token(vehicle_roof, static_cast<std::size_t>(1 + kIntersectionEnd), "|");
    put_token(vehicle_body, static_cast<std::size_t>(1 + kIntersectionEnd), "|");
    put_token(vehicle_wheels, static_cast<std::size_t>(1 + kIntersectionEnd), "|");

    put_token(vehicle_label, static_cast<std::size_t>(1 + vehicle_position + 2), "Vehicle");
    put_token(vehicle_roof, static_cast<std::size_t>(1 + vehicle_position), vehicle_sprite[0]);
    put_token(vehicle_body, static_cast<std::size_t>(1 + vehicle_position), vehicle_sprite[1]);
    put_token(vehicle_wheels, static_cast<std::size_t>(1 + vehicle_position), vehicle_sprite[2]);
    put_token(lane_markers, static_cast<std::size_t>(1 + kStopLine), "||");
    put_token(lane_markers, static_cast<std::size_t>(1 + kIntersectionEnd), "||");

    std::cout << "  Road view (" << config.signal_label << ")\n";
    print_scene_token(light_column - 6, "Traffic Light");
    print_scene_token(light_column, traffic_bulb(state, "RED"));
    print_scene_token(light_column, traffic_bulb(state, "YELLOW"));
    print_scene_token(light_column, traffic_bulb(state, "GREEN"));
    print_scene_token(light_column + 1, "|");
    std::cout << "  " << border << "\n"
              << "  " << zone_labels << "\n"
              << "  " << stop_marker << "\n"
              << "  " << vehicle_label << "\n"
              << "  " << vehicle_roof << "\n"
              << "  " << vehicle_body << "\n"
              << "  " << vehicle_wheels << "\n"
              << "  " << lane_markers << "\n"
              << "  " << border << "\n"
              << "  Light: " << signal_strip(state) << " | State: "
              << colored_state(state) << " | Vehicle: " << vehicle_status(state) << "\n";
}

std::string progress_bar(int elapsed_ms, int duration_ms) {
    constexpr int kWidth = 30;
    const int filled = duration_ms <= 0 ? kWidth : std::clamp((elapsed_ms * kWidth) / duration_ms, 0, kWidth);
    std::string bar = "[";
    bar += std::string(static_cast<std::size_t>(filled), '#');
    bar += std::string(static_cast<std::size_t>(kWidth - filled), '.');
    bar += "]";
    return ansi_wrap(bar, "36;1");
}

std::string render_button(const std::string& label, bool selected) {
    const std::string body = selected ? "> " + label + " <" : "  " + label + "  ";
    if (selected) {
        return ansi_wrap(body, "7;1");
    }
    return body;
}

void clear_dashboard() {
    std::cout << "\033[2J\033[3J\033[H" << std::flush;
}

void render_dashboard(const Config& config,
                      long long simulated_ms,
                      int selected_button,
                      int custom_advance_seconds,
                      const std::string& prompt_message = "") {
    const PhaseView phase = phase_at_time(config, simulated_ms);
    const auto& state = config.signal_states[phase.index];
    const int advance_options[] = {1, 10, 100};
    const long long wait_until_green = time_until_state(config, phase, "GREEN");

    clear_dashboard();
    std::cout << "=== Traffic Signal Light Simulator ===\n"
              << "Name: " << config.name << " | Layout: " << config.layout << "\n"
              << "Simulated time: " << format_seconds(simulated_ms)
              << " | Cycle period: " << format_seconds(phase.period_ms) << "\n"
              << "Phase: " << (phase.index + 1) << "/" << config.phase_durations_ms.size()
              << " | Active state: " << colored_state(state)
              << " | Remaining: " << format_seconds(phase.remaining_ms) << "\n"
              << "Next color change: " << format_seconds(phase.remaining_ms) << " | ";

    if (state == "GREEN") {
        std::cout << "Vehicle is moving now; green ends in " << format_seconds(phase.remaining_ms) << "\n";
    } else if (wait_until_green >= 0) {
        std::cout << "Vehicle waits " << format_seconds(wait_until_green) << " before GREEN\n";
    } else {
        std::cout << "No GREEN phase configured for this signal\n";
    }

    std::cout << "Progress: " << progress_bar(phase.elapsed_ms, phase.duration_ms) << "\n"
              << "  " << std::left << std::setw(18) << config.signal_label
              << " state=" << std::setw(10) << colored_state(state)
              << std::right << "\n\n";

    render_road_scene(config, state, phase.elapsed_ms, phase.duration_ms);

    std::cout << "\n  Advance simulation:\n  ";
    for (int index = 0; index < 3; ++index) {
        std::cout << render_button("+" + std::to_string(advance_options[index]) + "s", selected_button == index)
                  << "  ";
    }
    std::cout << render_button("+" + std::to_string(custom_advance_seconds) + "s", selected_button == 3);
    std::cout << "\n\n"
              << "  Left/Right arrows select a button. Enter/Space advances time. Select the last button to type a custom N seconds. q quits.\n";
    if (!prompt_message.empty()) {
        std::cout << "  " << prompt_message << "\n";
    }
}

void enable_ansi_output() {
#ifdef _WIN32
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD mode = 0;
    if (GetConsoleMode(output, &mode) == 0) {
        return;
    }
    SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

class TerminalGuard {
public:
    TerminalGuard() {
        enable_ansi_output();
        std::cout << "\033[?25l";
#ifndef _WIN32
        if (tcgetattr(STDIN_FILENO, &original_) == 0) {
            raw_ = original_;
            raw_.c_lflag &= static_cast<unsigned int>(~(ICANON | ECHO));
            raw_.c_cc[VMIN] = 1;
            raw_.c_cc[VTIME] = 0;
            raw_enabled_ = tcsetattr(STDIN_FILENO, TCSANOW, &raw_) == 0;
        }
#endif
    }

    ~TerminalGuard() {
#ifndef _WIN32
        if (raw_enabled_) {
            tcsetattr(STDIN_FILENO, TCSANOW, &original_);
        }
#endif
        std::cout << "\033[0m\033[?25h\n";
    }

private:
#ifndef _WIN32
    bool raw_enabled_ = false;
    termios original_{};
    termios raw_{};
#endif
};

int read_raw_char() {
#ifdef _WIN32
    return _getch();
#else
    unsigned char ch = '\0';
    if (read(STDIN_FILENO, &ch, 1) != 1) {
        return -1;
    }
    return ch;
#endif
}

Key read_key() {
    const int ch = read_raw_char();
    if (ch < 0) {
        return Key::Quit;
    }

#ifdef _WIN32
    if (ch == 0 || ch == 224) {
        const int special = read_raw_char();
        if (special == 75) {
            return Key::Left;
        }
        if (special == 77) {
            return Key::Right;
        }
        return Key::Other;
    }
    if (ch == 13 || ch == 32) {
        return Key::Activate;
    }
    if (ch == 'q' || ch == 'Q') {
        return Key::Quit;
    }
    return Key::Other;
#else
    if (ch == '\n' || ch == '\r' || ch == ' ') {
        return Key::Activate;
    }
    if (ch == 'q' || ch == 'Q') {
        return Key::Quit;
    }
    if (ch == '\033') {
        const int first = read_raw_char();
        const int second = read_raw_char();
        if (first < 0 || second < 0) {
            return Key::Quit;
        }
        if (first == '[' && second == 'D') {
            return Key::Left;
        }
        if (first == '[' && second == 'C') {
            return Key::Right;
        }
    }
    return Key::Other;
#endif
}

int read_custom_advance_seconds(const Config& config,
                                long long simulated_ms,
                                int selected_button,
                                int current_value) {
    std::string input;
    std::string message = "Custom seconds: _  (type 1-" + std::to_string(kMaxAdvanceSeconds) +
                          ", Enter applies, Esc/q cancels)";
    render_dashboard(config, simulated_ms, selected_button, current_value, message);

    while (true) {
        const int ch = read_raw_char();
        if (ch < 0 || ch == 27 || ch == 'q' || ch == 'Q') {
            render_dashboard(config, simulated_ms, selected_button, current_value, "Custom advance canceled.");
            return -1;
        }
        if (ch == '\n' || ch == '\r') {
            if (input.empty()) {
                return current_value;
            }
            const int parsed = parse_int_exact(input, "custom seconds");
            if (parsed <= 0 || parsed > kMaxAdvanceSeconds) {
                input.clear();
                message = "Custom seconds must be from 1 to " + std::to_string(kMaxAdvanceSeconds) + ". Try again: _";
                render_dashboard(config, simulated_ms, selected_button, current_value, message);
                continue;
            }
            return parsed;
        }
        if (ch == 8 || ch == 127) {
            if (!input.empty()) {
                input.pop_back();
            }
        } else if (std::isdigit(static_cast<unsigned char>(ch)) != 0 && input.size() < 5) {
            input.push_back(static_cast<char>(ch));
        }

        const std::string visible_input = input.empty() ? "_" : input + "_";
        message = "Custom seconds: " + visible_input + "  (Enter applies, Esc/q cancels)";
        render_dashboard(config, simulated_ms, selected_button, current_value, message);
    }
}

void advance_simulated_time(long long& simulated_ms, int seconds) {
    const long long increment = static_cast<long long>(seconds) * 1000LL;
    if (simulated_ms > std::numeric_limits<long long>::max() - increment) {
        throw std::runtime_error("Simulated time overflow");
    }
    simulated_ms += increment;
}

void run_dashboard(const Config& config) {
    TerminalGuard terminal;
    const int advance_options[] = {1, 10, 100};
    int selected_button = 0;
    int custom_advance_seconds = 30;
    long long simulated_ms = 0;

    render_dashboard(config, simulated_ms, selected_button, custom_advance_seconds);
    while (true) {
        const Key key = read_key();
        if (key == Key::Quit) {
            break;
        }
        if (key == Key::Left) {
            selected_button = (selected_button + 3) % 4;
        } else if (key == Key::Right) {
            selected_button = (selected_button + 1) % 4;
        } else if (key == Key::Activate) {
            int seconds = custom_advance_seconds;
            if (selected_button < 3) {
                seconds = advance_options[selected_button];
            } else {
                const int entered_seconds = read_custom_advance_seconds(config,
                                                                        simulated_ms,
                                                                        selected_button,
                                                                        custom_advance_seconds);
                if (entered_seconds < 0) {
                    render_dashboard(config, simulated_ms, selected_button, custom_advance_seconds);
                    continue;
                }
                custom_advance_seconds = entered_seconds;
                seconds = custom_advance_seconds;
            }
            advance_simulated_time(simulated_ms, seconds);
        }
        render_dashboard(config, simulated_ms, selected_button, custom_advance_seconds);
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc > 1) {
            std::cerr << "No command-line options are needed. Build with make, then run ./traffic_signal.\n";
            return 1;
        }

        (void)argv;
        const Config config = parse_config(kDefaultConfigPath);
        run_dashboard(config);
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }

    return 0;
}
