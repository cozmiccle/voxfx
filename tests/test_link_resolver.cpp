#include "sources/link_resolver.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>

using namespace vfx;

namespace {
std::string fixture(const char* name) {
    std::ifstream f(std::string(VOXFX_FIXTURES) + "/" + name);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
} // namespace

TEST_CASE("OpenGraph audio tag wins, title is cleaned") {
    auto r = resolveAudioFromHtml(fixture("og_audio.html"), "https://example.com/s/1");
    REQUIRE(r);
    REQUIRE(r->audioUrl == "https://cdn.example.com/sounds/air-horn.mp3");
    REQUIRE(r->title == "Air Horn & Friends");
}

TEST_CASE("play('...mp3') buttons resolve relative to the page") {
    auto r = resolveAudioFromHtml(fixture("button_onclick.html"),
                                  "https://www.myinstants.com/en/instant/sad-trombone/");
    REQUIRE(r);
    REQUIRE(r->audioUrl == "https://www.myinstants.com/media/sounds/sad-trombone.mp3");
    REQUIRE(r->title == "Sad Trombone");
}

TEST_CASE("<source> tags with protocol-relative URLs") {
    auto r = resolveAudioFromHtml(fixture("audio_tag.html"), "https://tuna.voicemod.net/sound/abc");
    REQUIRE(r);
    REQUIRE(r->audioUrl == "https://media.example.net/clips/robot_beep.ogg?v=2");
    REQUIRE(r->title == "Robot Beep");
}

TEST_CASE("pages without audio give nothing") {
    REQUIRE_FALSE(resolveAudioFromHtml(fixture("no_audio.html"), "https://example.com/"));
}

TEST_CASE("site and URL helpers") {
    REQUIRE(siteOf("https://www.myinstants.com/en/instant/x/") == Site::MyInstants);
    REQUIRE(siteOf("https://tuna.voicemod.net/sound/1") == Site::Voicemod);
    REQUIRE(siteOf("https://example.com") == Site::Other);
    REQUIRE(looksLikeAudioUrl("https://a.b/c/d.MP3"));
    REQUIRE(looksLikeAudioUrl("https://a.b/c/d.ogg?x=1"));
    REQUIRE_FALSE(looksLikeAudioUrl("https://a.b/mp3/page"));
    auto evil = sanitizeFileName("../../etc/passwd");
    REQUIRE(evil.find('/') == std::string::npos);
    REQUIRE(evil[0] != '.');
    REQUIRE(sanitizeFileName("my sound!.mp3") == "my_sound.mp3");
    REQUIRE(sanitizeFileName(".hidden")[0] != '.');
}
