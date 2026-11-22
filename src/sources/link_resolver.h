#pragma once
// Pure functions that find the audio file on a sound page (MyInstants, Voicemod's
// sound pages, or anything else that embeds an mp3/ogg/wav). Kept free of
// networking so they can be tested against saved HTML.
//
// Site layouts change. When a site breaks, add a site-specific rule at the top of
// resolveAudioFromHtml() and a fixture in tests/fixtures/.
#include <optional>
#include <string>

namespace vfx {

struct ResolvedLink {
    std::string audioUrl; // absolute
    std::string title;    // best guess, may be empty
};

enum class Site { MyInstants, Voicemod, Other };

Site siteOf(const std::string& url);
const char* siteName(Site s);
bool looksLikeAudioUrl(const std::string& url);
std::optional<ResolvedLink> resolveAudioFromHtml(const std::string& html, const std::string& pageUrl);
std::string sanitizeFileName(const std::string& s);

} // namespace vfx
