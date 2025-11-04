#include "logging_animation.h"
#include "animation_event_utils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <iomanip>
#include <sstream>

namespace why {
namespace animations {

namespace {
std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string trim(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}

std::vector<std::string> extract_tags(const std::string& line) {
    std::vector<std::string> tags;
    std::size_t pos = 0;
    while (pos < line.size() && line[pos] == '[') {
        const std::size_t end = line.find(']', pos + 1);
        if (end == std::string::npos) {
            break;
        }
        tags.emplace_back(line.substr(pos + 1, end - pos - 1));
        pos = end + 1;
        while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
            ++pos;
        }
    }
    return tags;
}

bool parse_float(const std::string& text, float& out_value) {
    const std::string trimmed = trim(text);
    if (trimmed.empty()) {
        return false;
    }
    char* end_ptr = nullptr;
    const float value = std::strtof(trimmed.c_str(), &end_ptr);
    if (end_ptr == trimmed.c_str()) {
        return false;
    }
    out_value = value;
    return true;
}

std::string format_float(float value, int precision = 2) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

} // namespace

std::optional<LoggingAnimation::Condition> LoggingAnimation::parse_condition_tag(const std::string& tag) {
    const std::string trimmed = trim(tag);
    const std::string lower = to_lower(trimmed);

    LoggingAnimation::Condition condition{};

    const auto parse_threshold = [&](const std::string& prefix,
                                     LoggingAnimation::Condition::Type type) -> std::optional<LoggingAnimation::Condition> {
        if (lower.rfind(prefix, 0) != 0) {
            return std::nullopt;
        }
        const std::string value_str = lower.substr(prefix.size());
        float threshold = 0.0f;
        if (!parse_float(value_str, threshold)) {
            return std::nullopt;
        }
        LoggingAnimation::Condition parsed{};
        parsed.type = type;
        parsed.threshold = threshold;
        return parsed;
    };

    if (auto parsed = parse_threshold("beat>", LoggingAnimation::Condition::Type::BeatAbove)) {
        return parsed;
    }
    if (auto parsed = parse_threshold("beat<", LoggingAnimation::Condition::Type::BeatBelow)) {
        return parsed;
    }
    if (auto parsed = parse_threshold("rms>", LoggingAnimation::Condition::Type::RmsAbove)) {
        return parsed;
    }
    if (auto parsed = parse_threshold("rms<", LoggingAnimation::Condition::Type::RmsBelow)) {
        return parsed;
    }
    if (auto parsed = parse_threshold("peak>", LoggingAnimation::Condition::Type::PeakAbove)) {
        return parsed;
    }
    if (auto parsed = parse_threshold("peak<", LoggingAnimation::Condition::Type::PeakBelow)) {
        return parsed;
    }
    if (lower == "dropped") {
        condition.type = LoggingAnimation::Condition::Type::DroppedAbove;
        condition.threshold = 0.0f;
        return condition;
    }
    if (auto parsed = parse_threshold("dropped>", LoggingAnimation::Condition::Type::DroppedAbove)) {
        return parsed;
    }
    if (lower == "audio_active") {
        condition.type = LoggingAnimation::Condition::Type::AudioActive;
        condition.threshold = 0.0f;
        return condition;
    }
    if (lower == "audio_inactive") {
        condition.type = LoggingAnimation::Condition::Type::AudioInactive;
        condition.threshold = 0.0f;
        return condition;
    }

    return std::nullopt;
}

namespace {

int clamp_dimension(int value, int min_value, int max_value) {
    return std::max(min_value, std::min(value, max_value));
}

int safe_origin(int requested, int plane_extent, int parent_extent) {
    if (parent_extent <= 0) {
        return 0;
    }
    if (plane_extent >= parent_extent) {
        return 0;
    }
    requested = std::max(0, requested);
    if (requested + plane_extent > parent_extent) {
        requested = parent_extent - plane_extent;
    }
    return requested;
}
} // namespace

LoggingAnimation::LoggingAnimation() = default;

LoggingAnimation::~LoggingAnimation() {
    if (plane_) {
        ncplane_destroy(plane_);
        plane_ = nullptr;
    }
}

void LoggingAnimation::init(notcurses* nc, const AppConfig& config) {
    if (!nc) {
        return;
    }

    const AnimationConfig* matched_config = nullptr;
    for (const auto& anim_config : config.animations) {
        if (anim_config.type == "Logging") {
            matched_config = &anim_config;
            break;
        }
    }

    if (matched_config) {
        z_index_ = matched_config->z_index;
        is_active_ = matched_config->initially_active;
        show_border_ = matched_config->log_show_border;
        loop_messages_ = matched_config->log_loop_messages;
        padding_y_ = std::max(0, matched_config->log_padding_y);
        padding_x_ = std::max(0, matched_config->log_padding_x);
        line_interval_s_ = std::max(0.0f, matched_config->log_line_interval_s);
        title_ = matched_config->log_title;
        if (!matched_config->text_file_path.empty()) {
            messages_file_path_ = matched_config->text_file_path;
        }
        if (matched_config->plane_rows) {
            plane_rows_ = std::max(3, *matched_config->plane_rows);
        }
        if (matched_config->plane_cols) {
            plane_cols_ = std::max(6, *matched_config->plane_cols);
        }
        if (matched_config->plane_y) {
            plane_origin_y_ = *matched_config->plane_y;
        }
        if (matched_config->plane_x) {
            plane_origin_x_ = *matched_config->plane_x;
        }
    }

    if (messages_file_path_.empty()) {
        messages_file_path_ = "assets/logging_animation.txt";
    }

    time_since_last_beat_log_event_ = beat_log_cooldown_s_;
    has_latest_metrics_ = false;
    audio_state_initialized_ = false;
    last_audio_active_state_ = false;
    next_peak_report_threshold_ = 0.2f;
    highest_peak_observed_ = 0.0f;
    latest_metrics_ = {};

    ensure_plane(nc);
    load_messages();
    recalculate_content_geometry();
    time_since_last_line_ = 0.0f;
    if (is_active_ && content_rows_ > 0) {
        append_next_line();
        time_since_last_line_ = 0.0f;
    }
    needs_redraw_ = true;
}

void LoggingAnimation::ensure_plane(notcurses* nc) {
    if (!nc) {
        return;
    }

    if (plane_) {
        ncplane_destroy(plane_);
        plane_ = nullptr;
    }

    ncplane* stdplane = notcurses_stdplane(nc);
    if (!stdplane) {
        return;
    }

    unsigned int parent_rows = 0;
    unsigned int parent_cols = 0;
    ncplane_dim_yx(stdplane, &parent_rows, &parent_cols);

    plane_rows_ = clamp_dimension(plane_rows_, 3, static_cast<int>(parent_rows));
    plane_cols_ = clamp_dimension(plane_cols_, 6, static_cast<int>(parent_cols));

    plane_origin_y_ = safe_origin(plane_origin_y_, plane_rows_, static_cast<int>(parent_rows));
    plane_origin_x_ = safe_origin(plane_origin_x_, plane_cols_, static_cast<int>(parent_cols));

    ncplane_options opts{};
    opts.y = plane_origin_y_;
    opts.x = plane_origin_x_;
    opts.rows = static_cast<unsigned>(plane_rows_);
    opts.cols = static_cast<unsigned>(plane_cols_);
    opts.userptr = nullptr;
    opts.name = "logging_animation";
    opts.resizecb = nullptr;
    opts.flags = 0u;

    plane_ = ncplane_create(stdplane, &opts);
    if (!plane_) {
        return;
    }

    ncplane_set_fg_rgb8(plane_, 120, 255, 120);
    ncplane_set_bg_rgb8(plane_, 0, 0, 0);
}

void LoggingAnimation::load_messages() {
    messages_.clear();
    sequential_indices_.clear();

    std::ifstream file(messages_file_path_);
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                continue;
            }

            MessageEntry entry;
            entry.text = line;
            entry.tags = extract_tags(line);

            for (const std::string& raw_tag : entry.tags) {
                const std::string trimmed_tag = trim(raw_tag);
                const std::string lowered_tag = to_lower(trimmed_tag);
                if (lowered_tag == "once") {
                    entry.once = true;
                    continue;
                }
                if (auto condition = LoggingAnimation::parse_condition_tag(trimmed_tag)) {
                    entry.conditions.push_back(*condition);
                }
            }

            messages_.push_back(std::move(entry));
        }
    }

    if (messages_.empty()) {
        MessageEntry entry;
        entry.text = "[log] No log messages configured.";
        messages_.push_back(entry);
    }

    for (std::size_t i = 0; i < messages_.size(); ++i) {
        MessageEntry& entry = messages_[i];
        entry.last_condition_state = false;
        entry.triggered_once = false;
        if (!entry.is_conditional()) {
            sequential_indices_.push_back(i);
        }
    }

    if (sequential_indices_.empty()) {
        MessageEntry entry;
        entry.text = "[log] No default log messages configured.";
        messages_.push_back(entry);
        sequential_indices_.push_back(messages_.size() - 1);
    }

    next_message_index_ = 0;
    visible_entries_.clear();
}

void LoggingAnimation::recalculate_content_geometry() {
    const int border = show_border_ ? 1 : 0;
    content_origin_y_ = border + padding_y_;
    content_origin_x_ = border + padding_x_;
    content_rows_ = plane_rows_ - 2 * border - 2 * padding_y_;
    content_cols_ = plane_cols_ - 2 * border - 2 * padding_x_;
    if (content_rows_ < 0) {
        content_rows_ = 0;
    }
    if (content_cols_ < 0) {
        content_cols_ = 0;
    }

    trim_history();
}

void LoggingAnimation::append_next_line() {
    if (content_rows_ <= 0 || sequential_indices_.empty()) {
        return;
    }

    if (next_message_index_ >= sequential_indices_.size()) {
        if (loop_messages_) {
            next_message_index_ = 0;
        } else {
            return;
        }
    }

    const std::size_t entry_index = sequential_indices_[next_message_index_];
    if (entry_index < messages_.size()) {
        append_log_entry(messages_[entry_index].text);
    }

    ++next_message_index_;
    if (loop_messages_ && next_message_index_ >= sequential_indices_.size()) {
        next_message_index_ = 0;
    }
}

void LoggingAnimation::append_log_entry(const std::string& entry) {
    visible_entries_.push_back(entry);
    trim_history();
    needs_redraw_ = true;
}

void LoggingAnimation::trim_history() {
    if (visible_entries_.empty()) {
        return;
    }

    const int effective_width = std::max(content_cols_, 1);
    const int base_rows = std::max(content_rows_, 1);
    const int max_history_lines = std::max(base_rows * 4, base_rows);

    int total_lines = 0;
    for (const auto& entry : visible_entries_) {
        total_lines += estimate_line_usage(entry, effective_width);
    }

    while (total_lines > max_history_lines && !visible_entries_.empty()) {
        total_lines -= estimate_line_usage(visible_entries_.front(), effective_width);
        visible_entries_.pop_front();
    }
}

void LoggingAnimation::process_conditional_messages(const AudioMetrics& metrics, float beat_strength) {
    for (auto& entry : messages_) {
        if (!entry.is_conditional()) {
            continue;
        }

        if (entry.once && entry.triggered_once) {
            continue;
        }

        const bool current_state = evaluate_conditions(entry, metrics, beat_strength);
        const bool should_emit = current_state && !entry.last_condition_state;

        if (should_emit) {
            append_log_entry(entry.text);
            if (entry.once) {
                entry.triggered_once = true;
            }
        }

        entry.last_condition_state = current_state;
    }
}

bool LoggingAnimation::evaluate_conditions(const MessageEntry& entry,
                                           const AudioMetrics& metrics,
                                           float beat_strength) {
    if (entry.conditions.empty()) {
        return false;
    }

    for (const Condition& condition : entry.conditions) {
        switch (condition.type) {
        case Condition::Type::BeatAbove:
            if (!(beat_strength > condition.threshold)) {
                return false;
            }
            break;
        case Condition::Type::BeatBelow:
            if (!(beat_strength < condition.threshold)) {
                return false;
            }
            break;
        case Condition::Type::RmsAbove:
            if (!(metrics.rms > condition.threshold)) {
                return false;
            }
            break;
        case Condition::Type::RmsBelow:
            if (!(metrics.rms < condition.threshold)) {
                return false;
            }
            break;
        case Condition::Type::PeakAbove:
            if (!(metrics.peak > condition.threshold)) {
                return false;
            }
            break;
        case Condition::Type::PeakBelow:
            if (!(metrics.peak < condition.threshold)) {
                return false;
            }
            break;
        case Condition::Type::DroppedAbove:
            if (!(static_cast<float>(metrics.dropped) > condition.threshold)) {
                return false;
            }
            break;
        case Condition::Type::AudioActive:
            if (!metrics.active) {
                return false;
            }
            break;
        case Condition::Type::AudioInactive:
            if (metrics.active) {
                return false;
            }
            break;
        }
    }

    return true;
}

void LoggingAnimation::handle_beat_event(float strength) {
    if (!is_active_ || !has_latest_metrics_) {
        return;
    }

    if (time_since_last_beat_log_event_ < beat_log_cooldown_s_) {
        return;
    }

    time_since_last_beat_log_event_ = 0.0f;

    std::ostringstream stream;
    stream << "[beat] pulse registered strength=" << format_float(strength, 2)
           << " :: rms=" << format_float(latest_metrics_.rms, 3)
           << " peak=" << format_float(latest_metrics_.peak, 3);
    append_log_entry(stream.str());
}

void LoggingAnimation::handle_audio_activity_change(const AudioMetrics& metrics) {
    if (!audio_state_initialized_ || metrics.active != last_audio_active_state_) {
        audio_state_initialized_ = true;
        last_audio_active_state_ = metrics.active;

        std::ostringstream stream;
        if (metrics.active) {
            stream << "[status] capture conduit awakened :: rms="
                   << format_float(metrics.rms, 3) << " peak="
                   << format_float(metrics.peak, 3);
            next_peak_report_threshold_ = 0.2f;
            highest_peak_observed_ = metrics.peak;
            time_since_last_beat_log_event_ = beat_log_cooldown_s_;
        } else {
            stream << "[status] silence veils the input :: dropped=" << metrics.dropped;
            next_peak_report_threshold_ = 0.2f;
            highest_peak_observed_ = 0.0f;
            has_latest_metrics_ = false;
        }

        append_log_entry(stream.str());
    }
}

void LoggingAnimation::check_peak_progression(const AudioMetrics& metrics) {
    if (!metrics.active) {
        return;
    }

    highest_peak_observed_ = std::max(highest_peak_observed_, metrics.peak);

    if (metrics.peak < next_peak_report_threshold_) {
        return;
    }

    std::ostringstream stream;
    stream << "[signal] crest breached " << format_float(metrics.peak, 2)
           << " :: rms=" << format_float(metrics.rms, 3);
    append_log_entry(stream.str());

    const float step = 0.1f;
    float candidate = next_peak_report_threshold_ + step;
    while (candidate <= metrics.peak) {
        candidate += step;
    }
    next_peak_report_threshold_ = std::min(candidate, 0.95f);
}

std::vector<std::string> LoggingAnimation::wrap_text(const std::string& text, int width) const {
    std::vector<std::string> lines;
    if (width <= 0) {
        lines.push_back(text);
        return lines;
    }

    const std::size_t chunk = static_cast<std::size_t>(width);
    if (text.empty()) {
        lines.emplace_back();
        return lines;
    }

    for (std::size_t start = 0; start < text.size(); start += chunk) {
        lines.emplace_back(text.substr(start, std::min(chunk, text.size() - start)));
    }

    return lines;
}

int LoggingAnimation::estimate_line_usage(const std::string& text, int width) const {
    const int effective_width = std::max(width, 1);
    if (text.empty()) {
        return 1;
    }
    const std::size_t length = text.size();
    return static_cast<int>((length + static_cast<std::size_t>(effective_width) - 1) /
                            static_cast<std::size_t>(effective_width));
}

void LoggingAnimation::update(float delta_time,
                              const AudioMetrics& metrics,
                              const std::vector<float>& bands,
                              float beat_strength) {
    (void)bands;

    if (!plane_ || !is_active_) {
        return;
    }

    latest_metrics_ = metrics;
    has_latest_metrics_ = true;
    time_since_last_beat_log_event_ += delta_time;

    handle_audio_activity_change(metrics);
    check_peak_progression(metrics);

    process_conditional_messages(metrics, beat_strength);

    if (line_interval_s_ <= 0.0f) {
        if (sequential_indices_.empty() || content_rows_ <= 0) {
            return;
        }
        const std::size_t sequence_size = sequential_indices_.size();
        std::size_t iterations = loop_messages_ ? sequence_size : (sequence_size - next_message_index_);
        for (std::size_t i = 0; i < iterations; ++i) {
            append_next_line();
            if (!loop_messages_ && next_message_index_ >= sequence_size) {
                break;
            }
        }
        time_since_last_line_ = 0.0f;
        return;
    }

    time_since_last_line_ += delta_time;
    while (time_since_last_line_ >= line_interval_s_) {
        append_next_line();
        time_since_last_line_ -= line_interval_s_;
        if (!loop_messages_ && next_message_index_ >= sequential_indices_.size()) {
            time_since_last_line_ = 0.0f;
            break;
        }
    }
}

void LoggingAnimation::draw_border() {
    if (!show_border_ || !plane_ || plane_rows_ < 2 || plane_cols_ < 2) {
        return;
    }

    const int last_row = plane_rows_ - 1;
    const int last_col = plane_cols_ - 1;

    ncplane_putchar_yx(plane_, 0, 0, '+');
    ncplane_putchar_yx(plane_, 0, last_col, '+');
    ncplane_putchar_yx(plane_, last_row, 0, '+');
    ncplane_putchar_yx(plane_, last_row, last_col, '+');

    for (int x = 1; x < last_col; ++x) {
        ncplane_putchar_yx(plane_, 0, x, '-');
        ncplane_putchar_yx(plane_, last_row, x, '-');
    }

    for (int y = 1; y < last_row; ++y) {
        ncplane_putchar_yx(plane_, y, 0, '|');
        ncplane_putchar_yx(plane_, y, last_col, '|');
    }

    if (!title_.empty() && plane_cols_ > 4) {
        int max_title = plane_cols_ - 4;
        std::string clipped = title_.substr(0, static_cast<std::size_t>(max_title));
        int start_x = 2;
        for (std::size_t i = 0; i < clipped.size() && (start_x + static_cast<int>(i)) < last_col; ++i) {
            ncplane_putchar_yx(plane_, 0, start_x + static_cast<int>(i), clipped[i]);
        }
    }
}

void LoggingAnimation::draw_logs() {
    if (!plane_ || content_rows_ <= 0 || content_cols_ <= 0) {
        return;
    }

    std::vector<std::string> wrapped_lines;
    wrapped_lines.reserve(visible_entries_.size());
    for (const auto& entry : visible_entries_) {
        std::vector<std::string> segments = wrap_text(entry, content_cols_);
        wrapped_lines.insert(wrapped_lines.end(), segments.begin(), segments.end());
    }

    const int total_lines = static_cast<int>(wrapped_lines.size());
    const int lines_to_render = std::min(content_rows_, total_lines);
    const int start_index = std::max(0, total_lines - lines_to_render);

    const int max_rows = content_origin_y_ + content_rows_;
    std::string blank(static_cast<std::size_t>(content_cols_), ' ');

    int y = content_origin_y_;
    for (int i = start_index; i < total_lines && y < max_rows; ++i) {
        std::string line = wrapped_lines[static_cast<std::size_t>(i)];
        if (static_cast<int>(line.size()) < content_cols_) {
            line.append(static_cast<std::size_t>(content_cols_ - static_cast<int>(line.size())), ' ');
        } else if (static_cast<int>(line.size()) > content_cols_) {
            line.resize(static_cast<std::size_t>(content_cols_));
        }
        ncplane_putstr_yx(plane_, y, content_origin_x_, line.c_str());
        ++y;
    }

    while (y < max_rows) {
        ncplane_putstr_yx(plane_, y, content_origin_x_, blank.c_str());
        ++y;
    }
}

void LoggingAnimation::redraw() {
    if (!plane_) {
        return;
    }

    ncplane_erase(plane_);
    draw_border();
    draw_logs();
    needs_redraw_ = false;
}

void LoggingAnimation::render(notcurses* nc) {
    (void)nc;
    if (!plane_ || !is_active_) {
        return;
    }

    if (needs_redraw_) {
        redraw();
    }
}

void LoggingAnimation::activate() {
    if (is_active_) {
        return;
    }

    is_active_ = true;
    visible_entries_.clear();
    next_message_index_ = 0;
    for (auto& entry : messages_) {
        entry.last_condition_state = false;
    }
    if (content_rows_ > 0) {
        append_next_line();
    }
    time_since_last_line_ = 0.0f;
    time_since_last_beat_log_event_ = beat_log_cooldown_s_;
    has_latest_metrics_ = false;
    audio_state_initialized_ = false;
    latest_metrics_ = {};
    next_peak_report_threshold_ = 0.2f;
    highest_peak_observed_ = 0.0f;
    needs_redraw_ = true;
}

void LoggingAnimation::deactivate() {
    if (!is_active_) {
        return;
    }

    is_active_ = false;
    visible_entries_.clear();
    next_message_index_ = 0;
    if (plane_) {
        ncplane_erase(plane_);
    }
    has_latest_metrics_ = false;
    audio_state_initialized_ = false;
    needs_redraw_ = false;
}

void LoggingAnimation::bind_events(const AnimationConfig& config, events::EventBus& bus) {
    bind_standard_frame_updates(this, config, bus);
    auto handle = bus.subscribe<events::BeatDetectedEvent>(
        [this](const events::BeatDetectedEvent& event) { handle_beat_event(event.strength); });
    track_subscription(std::move(handle));
}

} // namespace animations
} // namespace why
