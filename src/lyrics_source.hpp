#pragma once

#include <cstdint>
#include <functional>
#include <gio/gio.h>
#include <string>
#include <string_view>
#include <vector>

namespace cadence {

struct LyricLine {
    std::int64_t milliseconds;
    std::string text;
};

struct LyricsQuery {
    std::string title;
    std::string artist;
    std::string album;
    int duration_seconds = 0;

    bool operator==(const LyricsQuery&) const = default;
};

struct LyricsResult {
    std::vector<LyricLine> lines;
    std::string status;
    std::int64_t retry_after_seconds = 0;
    bool cacheable = false;
};

std::vector<LyricLine> parse_lrc(std::string_view text);
std::string_view lyric_at(const std::vector<LyricLine>& lines, std::int64_t milliseconds);

// Looks up an exact match on LRCLIB. This makes a network request; callers
// must check opt-in first. Completion runs on the caller's GLib context.
using LyricsCallback = std::function<void(LyricsResult)>;
void lookup_synced_lyrics(LyricsQuery query, GCancellable* cancellable, LyricsCallback done);

} // namespace cadence
