#include "sources/link_resolver.h"

#include <QString>
#include <QUrl>

#include <cctype>
#include <cstring>
#include <regex>

namespace vfx {

namespace {

const char* kAudioExt = R"((?:mp3|ogg|oga|opus|wav|flac|m4a))";

std::string absolute(const std::string& base, const std::string& rel) {
    QUrl b(QString::fromStdString(base));
    QUrl r(QString::fromStdString(rel));
    return b.resolved(r).toString(QUrl::FullyEncoded).toStdString();
}

std::string decodeEntities(std::string s) {
    static const std::pair<const char*, const char*> ents[] = {
        {"&amp;", "&"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&#x27;", "'"},
        {"&apos;", "'"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&nbsp;", " "}};
    for (auto& [e, r] : ents) {
        size_t p;
        while ((p = s.find(e)) != std::string::npos) s.replace(p, strlen(e), r);
    }
    return s;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string{} : s.substr(a, b - a + 1);
}

// <meta property="og:xyz" content="..."> in either attribute order.
std::string metaContent(const std::string& html, const std::string& prop) {
    std::regex a("<meta[^>]+(?:property|name)\\s*=\\s*[\"']" + prop +
                     "[\"'][^>]*content\\s*=\\s*[\"']([^\"']+)[\"']",
                 std::regex::icase);
    std::regex b("<meta[^>]+content\\s*=\\s*[\"']([^\"']+)[\"'][^>]*(?:property|name)\\s*=\\s*[\"']" +
                     prop + "[\"']",
                 std::regex::icase);
    std::smatch m;
    if (std::regex_search(html, m, a) || std::regex_search(html, m, b)) return decodeEntities(m[1]);
    return {};
}

std::string cleanTitle(std::string t) {
    t = trim(decodeEntities(t));
    // "Bruh Sound Effect - Instant Sound Button | Myinstants" -> "Bruh Sound Effect"
    for (const char* sep : {" | ", " - Instant Sound", " - Voicemod", " – "}) {
        auto p = t.find(sep);
        if (p != std::string::npos && p > 0) t = t.substr(0, p);
    }
    return trim(t);
}

} // namespace

Site siteOf(const std::string& url) {
    QString host = QUrl(QString::fromStdString(url)).host().toLower();
    if (host.endsWith("myinstants.com")) return Site::MyInstants;
    if (host.endsWith("voicemod.net") || host.endsWith("voicemod.com")) return Site::Voicemod;
    return Site::Other;
}

const char* siteName(Site s) {
    switch (s) {
        case Site::MyInstants: return "myinstants";
        case Site::Voicemod: return "voicemod";
        default: return "web";
    }
}

bool looksLikeAudioUrl(const std::string& url) {
    std::regex re(std::string(R"(\.)") + kAudioExt + R"((?:[?#].*)?$)", std::regex::icase);
    QString path = QUrl(QString::fromStdString(url)).path();
    return std::regex_search(path.toStdString(), re);
}

std::optional<ResolvedLink> resolveAudioFromHtml(const std::string& html, const std::string& pageUrl) {
    ResolvedLink out;
    std::smatch m;

    // Title: og:title, then <title>.
    out.title = metaContent(html, "og:title");
    if (out.title.empty()) {
        std::regex t("<title[^>]*>([^<]*)</title>", std::regex::icase);
        if (std::regex_search(html, m, t)) out.title = m[1];
    }
    out.title = cleanTitle(out.title);

    std::string found;

    // 1. OpenGraph / Twitter audio tags.
    for (const char* p : {"og:audio:secure_url", "og:audio:url", "og:audio", "twitter:player:stream"}) {
        found = metaContent(html, p);
        if (!found.empty()) break;
    }

    // 2. <audio src> / <source src>.
    if (found.empty()) {
        std::regex tag(R"(<(?:audio|source)[^>]+src\s*=\s*["']([^"']+)["'])", std::regex::icase);
        if (std::regex_search(html, m, tag)) found = decodeEntities(m[1]);
    }

    // 3. JS handlers like onclick="play('/media/sounds/x.mp3', ...)" or
    //    data-url="..." attributes: any quoted string ending in an audio extension.
    //    Prefer one inside the main play button if a site marks it.
    if (found.empty()) {
        std::regex quoted(std::string(R"(["']((?:https?:)?[^"'\s<>]*\.)") + kAudioExt +
                              R"((?:\?[^"'\s<>]*)?)["'])",
                          std::regex::icase);
        if (std::regex_search(html, m, quoted)) found = decodeEntities(m[1]);
    }

    if (found.empty()) return std::nullopt;
    out.audioUrl = absolute(pageUrl, found);
    if (out.title.empty()) {
        QString file = QUrl(QString::fromStdString(out.audioUrl)).fileName();
        out.title = file.section('.', 0, 0).replace('_', ' ').toStdString();
    }
    return out;
}

std::string sanitizeFileName(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') o += char(c);
        else if (c == ' ') o += '_';
    }
    if (o.empty() || o[0] == '.') o = "sound" + o;
    if (o.size() > 80) o.resize(80);
    return o;
}

} // namespace vfx
