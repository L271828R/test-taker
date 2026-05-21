#include <SDL.h>
#include <SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "game_data.h"

// ── File logger (writes to /tmp/tt-game.log) ─────────────────────────────────
static FILE* gLog = nullptr;

static void logOpen() {
    gLog = fopen("/tmp/tt-game.log", "w");
}
static void logf(const char* fmt, ...) {
    if (!gLog) return;
    Uint32 t = SDL_GetTicks();
    fprintf(gLog, "[%6u] ", t);
    va_list ap; va_start(ap, fmt); vfprintf(gLog, fmt, ap); va_end(ap);
    fprintf(gLog, "\n");
    fflush(gLog);
}
static void logClose() { if (gLog) { fclose(gLog); gLog = nullptr; } }

// ── Chiptune audio (square-wave melody + synthesised drums) ──────────────────
static constexpr int AUDIO_FREQ = 44100;

// 5 themes × 16 notes (MIDI, 0 = rest). Quarter-note pace.
// ADHD-friendly: energetic BPMs, short catchy hooks, rhythmic rests.
static const int THEME_NOTES[5][16] = {
    {60, 0,64,67,  69,67,64, 0,  65, 0,67,72,  71,69,67,60},  // Groove: C major
    {67, 0,71, 0,  74, 0,72,71,  69,71, 0,74,  76,74,71, 0},  // Bounce: G major
    {64,67, 0,69,  71, 0,74,71,  69, 0,67,64,  67,69,71, 0},  // Drive:  E minor penta
    {65, 0,69, 0,  72,69,65, 0,  67, 0,70, 0,  72,74,72,69},  // Pulse:  F major
    {62, 0,66,69,  71,69,66, 0,  67,69, 0,74,  73,71,69,62},  // Spark:  D major
};
static const float THEME_BPM[5] = {140.f, 152.f, 168.f, 145.f, 158.f};

// Drum patterns: 5 themes × 16 steps (16th-note grid, 1 = hit)
static const int DRUM_KICK[5][16] = {
    {1,0,0,0, 0,0,0,0, 1,0,0,0, 0,0,0,0},  // Groove: beats 1 & 3
    {1,0,0,0, 0,0,1,0, 1,0,0,1, 0,0,0,0},  // Bounce: syncopated
    {1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,0},  // Drive:  four-on-the-floor
    {1,0,0,0, 0,0,1,0, 1,0,0,0, 0,1,0,0},  // Pulse:  groove kick
    {1,0,1,0, 0,0,1,0, 1,0,0,0, 1,0,1,0},  // Spark:  busy
};
static const int DRUM_SNARE[5][16] = {
    {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0},  // Groove: beats 2 & 4
    {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0},  // Bounce
    {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0},  // Drive
    {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,1},  // Pulse: + ghost on 4-and
    {0,0,0,0, 1,0,1,0, 0,0,0,0, 1,0,0,0},  // Spark: + ghost on 2-and
};
static const int DRUM_HAT[5][16] = {
    {1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0},  // Groove: 8th notes
    {1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1},  // Bounce: every 16th
    {1,0,1,1, 1,0,1,0, 1,0,1,1, 1,0,1,0},  // Drive:  offbeat push
    {1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0},  // Pulse:  8th notes
    {1,1,0,1, 1,1,0,1, 1,1,0,1, 1,1,0,1},  // Spark:  driving pattern
};

struct AudioCtx {
    SDL_AudioDeviceID devId         = 0;
    int   theme                     = 0;
    int   noteIdx                   = 0;
    int   sampLeft                  = 0;
    float phase                     = 0.f;
    // Drum sequencer
    int   drumStep                  = 0;
    int   drumSampLeft              = 0;
    // Kick: sine-wave pitch sweep (punchy thud)
    float kickAmp                   = 0.f;
    float kickFreq                  = 80.f;
    float kickPhase                 = 0.f;
    // Snare & hat: filtered noise bursts
    float snareAmp                  = 0.f;
    float hatAmp                    = 0.f;
    // Thread-local LCG for noise (no std::rand in audio thread)
    Uint32 rng                      = 0xBEEF1234u;
};
static AudioCtx gAudio;

// Generative pentatonic melody (active when streak >= 4)
static int   gGenNotes[64] = {};
static int   gGenLen       = 32;
static float gGenBpm       = 150.f;
static bool  gGenMode      = false;  // read/written only under audio lock

static void audioCallback(void*, Uint8* stream, int len) {
    Sint16* buf    = (Sint16*)stream;
    int     frames = len / 4;  // stereo 16-bit

    for (int i = 0; i < frames; ++i) {
        float bpm = gGenMode ? gGenBpm : THEME_BPM[gAudio.theme];

        // ── Melody note advance ───────────────────────────────────────────────
        if (gAudio.sampLeft <= 0) {
            int seqLen      = gGenMode ? gGenLen : 16;
            gAudio.noteIdx  = (gAudio.noteIdx + 1) % seqLen;
            gAudio.sampLeft = (int)(AUDIO_FREQ * 60.f / bpm);
            gAudio.phase    = 0.f;
        }

        // ── Drum step advance (16th notes = quarter-note / 4) ────────────────
        if (gAudio.drumSampLeft <= 0) {
            gAudio.drumSampLeft = (int)(AUDIO_FREQ * 60.f / (bpm * 4.f));
            gAudio.drumStep     = (gAudio.drumStep + 1) % 16;
            int t = gAudio.theme;
            if (DRUM_KICK [t][gAudio.drumStep]) {
                gAudio.kickAmp   = 1.0f;
                gAudio.kickFreq  = 150.f;
                gAudio.kickPhase = 0.f;
            }
            if (DRUM_SNARE[t][gAudio.drumStep]) gAudio.snareAmp = 1.0f;
            if (DRUM_HAT  [t][gAudio.drumStep]) gAudio.hatAmp   = 1.0f;
        }

        // ── Melody: square wave ───────────────────────────────────────────────
        int note = gGenMode ? gGenNotes[gAudio.noteIdx]
                            : THEME_NOTES[gAudio.theme][gAudio.noteIdx];
        float melodyF = 0.f;
        if (note > 0) {
            float freq = 440.f * std::pow(2.f, (note - 69) / 12.f);
            melodyF = (gAudio.phase < 0.5f) ? 1.f : -1.f;
            gAudio.phase += freq / AUDIO_FREQ;
            if (gAudio.phase >= 1.f) gAudio.phase -= 1.f;
        }

        // ── Kick: sine sweep, pitch falls from ~150 Hz to ~40 Hz ─────────────
        float kickF = 0.f;
        if (gAudio.kickAmp > 0.001f) {
            kickF = gAudio.kickAmp * std::sin(gAudio.kickPhase * 6.28318f);
            gAudio.kickPhase += gAudio.kickFreq / AUDIO_FREQ;
            if (gAudio.kickPhase >= 1.f) gAudio.kickPhase -= 1.f;
            gAudio.kickFreq  *= 0.9999f;   // pitch sweep down
            gAudio.kickAmp   *= 0.9993f;   // ~220 ms decay
        }

        // ── Snare: noise burst, ~100 ms decay ────────────────────────────────
        float snareF = 0.f;
        if (gAudio.snareAmp > 0.001f) {
            gAudio.rng  = gAudio.rng * 1664525u + 1013904223u;
            float noise = (float)(int)(gAudio.rng >> 1) / (float)0x40000000;
            snareF = gAudio.snareAmp * noise;
            gAudio.snareAmp *= 0.9985f;
        }

        // ── Hi-hat: very short noise burst, ~8 ms decay ──────────────────────
        float hatF = 0.f;
        if (gAudio.hatAmp > 0.001f) {
            gAudio.rng  = gAudio.rng * 1664525u + 1013904223u;
            float noise = (float)(int)(gAudio.rng >> 1) / (float)0x40000000;
            hatF = gAudio.hatAmp * noise;
            gAudio.hatAmp *= 0.984f;
        }

        // ── Mix (melody softer to give drums room) ────────────────────────────
        float mix = melodyF * 3800.f
                  + kickF   * 6500.f
                  + snareF  * 3000.f
                  + hatF    * 1100.f;
        Sint16 s = (Sint16)std::max(-30000.f, std::min(30000.f, mix));

        --gAudio.sampLeft;
        --gAudio.drumSampLeft;
        buf[i * 2]     = s;
        buf[i * 2 + 1] = s;
    }
}

static void initAudio() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        logf("AUDIO init failed: %s", SDL_GetError()); return;
    }
    SDL_AudioSpec want{}, got{};
    want.freq     = AUDIO_FREQ;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = 512;
    want.callback = audioCallback;
    gAudio.devId  = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
    if (gAudio.devId == 0) { logf("AUDIO open failed: %s", SDL_GetError()); return; }
    SDL_PauseAudioDevice(gAudio.devId, 0);
    logf("AUDIO started, freq=%d", got.freq);
}

static void closeAudio() {
    if (gAudio.devId) { SDL_CloseAudioDevice(gAudio.devId); gAudio.devId = 0; }
}

static void setAudioTheme(int t) {
    if (!gAudio.devId) return;
    SDL_LockAudioDevice(gAudio.devId);
    gAudio.theme        = t;
    gAudio.noteIdx      = 0;
    gAudio.sampLeft     = 0;
    gAudio.phase        = 0.f;
    gAudio.drumStep     = 0;
    gAudio.drumSampLeft = 0;
    gAudio.kickAmp      = 0.f;
    gAudio.snareAmp     = 0.f;
    gAudio.hatAmp       = 0.f;
    gGenMode            = false;   // return to theme music on any theme change / death
    SDL_UnlockAudioDevice(gAudio.devId);
}

// ── Layout ────────────────────────────────────────────────────────────────────
static constexpr int WIN_W    = 1100;
static constexpr int WIN_H    = 700;
static constexpr int QBAR_H   = 108;   // question bar height
static constexpr int GAME_TOP = QBAR_H;
static constexpr int GAME_H   = WIN_H - QBAR_H;
static constexpr int GATE_X   = WIN_W - 230; // decision zone left edge
static constexpr int GATE_MID = GAME_TOP + GAME_H / 2;

// ── Physics / pipe constants ──────────────────────────────────────────────────
static constexpr float GRAVITY   = 0.372f;   // 0.46 * 0.85 * 0.95
static constexpr float FLAP      = -7.59f;   // -9.4 * 0.85 * 0.95
static constexpr float SPEED     = 2.10f;    // 2.6  * 0.85 * 0.95
static constexpr float PIPE_W    = 74.f;
static constexpr float PIPE_GAP  = 172.f;
static constexpr float DPIPE_GAP = 160.f;    // each opening in the decision pipe
static constexpr float BIRD_R    = 16.f;
static constexpr float COLL_GRACE = 2.f;  // forgiveness margin so 0.1-px float overshoots don't kill

// Decision-pipe gap centres: midpoint of the A-zone and B-zone respectively.
static constexpr float DPIPE_GA = (GAME_TOP + GATE_MID) / 2.f;
static constexpr float DPIPE_GB = (GATE_MID + WIN_H)    / 2.f;

static constexpr int CAP_H   = 24;   // height of the wider pipe cap section
static constexpr int CAP_EXT = 8;    // cap extends this many px past each pipe-body side

// ── Colour palette ────────────────────────────────────────────────────────────
static const SDL_Color C_BG        = {13,  17,  23,  255};
static const SDL_Color C_QBAR      = {22,  27,  34,  255};
static const SDL_Color C_BORDER    = {48,  54,  61,  255};
static const SDL_Color C_TEXT      = {230, 237, 243, 255};
static const SDL_Color C_MUTED     = {125, 133, 144, 255};
// Pixel-art pipe palette (matches reference Flappy Bird pipe)
static const SDL_Color CP_DARK  = {44,  42,  14,  255};  // dark outline
static const SDL_Color CP_HI1   = {196, 228, 72,  255};  // bright lime highlight
static const SDL_Color CP_HI2   = {142, 190, 42,  255};  // mid highlight
static const SDL_Color CP_BODY  = {94,  152, 22,  255};  // main body
static const SDL_Color CP_SHAD  = {50,  80,  10,  255};  // right shadow
static const SDL_Color C_BIRD      = {247, 201, 72,  255};
static const SDL_Color C_BIRD_RING = {180, 130, 30,  255};
static const SDL_Color C_WHITE     = {255, 255, 255, 255};
static const SDL_Color C_OK        = {46,  160, 67,  255};
static const SDL_Color C_ERR       = {248, 81,  73,  255};
static const SDL_Color C_OVERLAY   = {0,   0,   0,   210};
static const SDL_Color C_A_BG      = {20,  70,  180, 200};
static const SDL_Color C_B_BG      = {75,  30,  160, 200};
static const SDL_Color C_A_TEXT    = {88,  166, 255, 255};
static const SDL_Color C_B_TEXT    = {163, 113, 247, 255};
static const SDL_Color C_A_LABEL   = {31,  111, 235, 255};
static const SDL_Color C_B_LABEL   = {110, 64,  201, 255};

// ── Visual themes (sky gradient, ground, parallax props) ──────────────────────
struct VisTheme {
    SDL_Color sky1, sky2;   // gradient: top → bottom of game area
    SDL_Color ground;
    SDL_Color prop;         // colour of clouds / stars / crystals
    int       kind;         // 0=clouds 1=stars 2=neon 3=cave
    bool      lightBg;      // true = bright sky needs dark answer-zone overlay
};
static const VisTheme VIS_THEMES[5] = {
    // Day
    {{100,180,255,255},{160,215,255,255},{76,158,36, 255},{255,255,255,220}, 0, true},
    // Sunset
    {{255, 96, 38,255},{255,190, 74,255},{160, 68,18,255},{255,220,120,220}, 0, true},
    // Night
    {{  8,  8, 46,255},{ 18, 14, 62,255},{ 18, 12,36,255},{255,255,190,230}, 1, false},
    // Neon
    {{  4,  4, 18,255},{  8, 12, 36,255},{  0, 38,56,255},{  0,255,196,200}, 2, false},
    // Cave
    {{ 22, 14,  7,255},{ 38, 24,10,255},{ 56, 38,18,255},{160, 76,235,200}, 3, false},
};
static int      gTheme       = 0;
static float    gBgScroll    = 0.f;
static SDL_Rect gSaveBtnRect = {0, 0, 0, 0};  // set each frame by drawResult

// ── Theme shuffle queue ───────────────────────────────────────────────────────
// Advance through all 5 themes in random order before repeating any.
static int gThemeQueue[5] = {0, 1, 2, 3, 4};
static int gThemePos      = 0;

static void shuffleThemeQueue(int avoidFirst) {
    // Fisher-Yates
    for (int i = 4; i > 0; --i) {
        int j = std::rand() % (i + 1);
        std::swap(gThemeQueue[i], gThemeQueue[j]);
    }
    // Avoid playing the same theme twice in a row across a reshuffle.
    if (avoidFirst >= 0 && gThemeQueue[0] == avoidFirst) {
        int j = 1 + std::rand() % 4;
        std::swap(gThemeQueue[0], gThemeQueue[j]);
    }
}

static int nextTheme() {
    ++gThemePos;
    if (gThemePos >= 5) {
        shuffleThemeQueue(gThemeQueue[4]);
        gThemePos = 0;
    }
    return gThemeQueue[gThemePos];
}

// ── Drawing helpers ───────────────────────────────────────────────────────────
static void setColor(SDL_Renderer* r, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}
static void fillRect(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c) {
    setColor(r, c);
    SDL_Rect rect{x, y, w, h};
    SDL_RenderFillRect(r, &rect);
}
static void drawHLine(SDL_Renderer* r, int x1, int x2, int y, SDL_Color c) {
    setColor(r, c);
    SDL_RenderDrawLine(r, x1, y, x2, y);
}
static void drawVLine(SDL_Renderer* r, int x, int y1, int y2, SDL_Color c) {
    setColor(r, c);
    SDL_RenderDrawLine(r, x, y1, x, y2);
}
static void fillCircle(SDL_Renderer* r, int cx, int cy, int rad, SDL_Color c) {
    setColor(r, c);
    for (int dy = -rad; dy <= rad; ++dy) {
        int dx = (int)std::sqrt((float)(rad * rad - dy * dy));
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}
static void outlineCircle(SDL_Renderer* r, int cx, int cy, int rad, SDL_Color c) {
    setColor(r, c);
    int x = rad, y = 0, err = 0;
    while (x >= y) {
        for (int dy : {y, -y}) for (int dx : {x, -x}) SDL_RenderDrawPoint(r, cx + dx, cy + dy);
        for (int dy : {x, -x}) for (int dx : {y, -y}) SDL_RenderDrawPoint(r, cx + dx, cy + dy);
        if (err <= 0) err += 2 * (++y) + 1;
        else          err += 2 * (--x - y) + 1;
    }
}

// Returns true if the UTF-8 string contains any CJK Unified Ideograph (U+4E00–U+9FFF).
static bool hasCJK(const std::string& s) {
    for (std::size_t i = 0; i + 2 < s.size(); ++i) {
        auto b0 = (unsigned char)s[i];
        auto b1 = (unsigned char)s[i + 1];
        auto b2 = (unsigned char)s[i + 2];
        if (b0 >= 0xE4 && b0 <= 0xE9 &&
            (b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80)
            return true;
    }
    return false;
}

// ── Text helpers ──────────────────────────────────────────────────────────────
static TTF_Font* loadFont(int size) {
    const char* paths[] = {
        // CJK-capable fonts first so Chinese text renders without boxes
        "/System/Library/Fonts/PingFang.ttc",          // macOS 12+ (best CJK)
        "/System/Library/Fonts/STHeiti Medium.ttc",    // macOS 10–11 CJK
        "/System/Library/Fonts/STHeiti Light.ttc",
        // Latin fallbacks
        "/System/Library/Fonts/Helvetica.ttc",
        "/Library/Fonts/Arial.ttf",
        "/System/Library/Fonts/SFNSDisplay.ttf",
        "/System/Library/Fonts/SFCompact.ttf",
        nullptr
    };
    for (int i = 0; paths[i]; ++i) {
        TTF_Font* f = TTF_OpenFont(paths[i], size);
        if (f) { logf("font sz=%d  path=%s", size, paths[i]); return f; }
    }
    logf("WARN: no font found for size %d", size);
    return nullptr;
}

static void drawText(SDL_Renderer* r, TTF_Font* font,
                     const std::string& text, int x, int y, SDL_Color c) {
    if (!font || text.empty()) return;
    SDL_Surface* s = TTF_RenderUTF8_Blended(font, text.c_str(), c);
    if (!s) return;
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    if (!t) return;
    int w, h;
    SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
    SDL_Rect dst{x, y, w, h};
    SDL_RenderCopy(r, t, nullptr, &dst);
    SDL_DestroyTexture(t);
}

static void drawTextCX(SDL_Renderer* r, TTF_Font* font,
                       const std::string& text, int cx, int y, SDL_Color c) {
    if (!font || text.empty()) return;
    SDL_Surface* s = TTF_RenderUTF8_Blended(font, text.c_str(), c);
    if (!s) return;
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    if (!t) return;
    int w, h;
    SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
    SDL_Rect dst{cx - w / 2, y, w, h};
    SDL_RenderCopy(r, t, nullptr, &dst);
    SDL_DestroyTexture(t);
}

// Word-wrap text into maxW, draw starting at (x, y), capped at maxH px tall.
static void drawWrapped(SDL_Renderer* r, TTF_Font* font, const std::string& text,
                        int x, int y, int maxW, int maxH, SDL_Color c) {
    if (!font) return;
    int lineH = TTF_FontLineSkip(font) + 1;
    int curY  = y;

    std::string para;
    std::vector<std::string> paras;
    for (char ch : text) {
        if (ch == '\n') { paras.push_back(para); para.clear(); }
        else            para += ch;
    }
    paras.push_back(para);

    for (const auto& p : paras) {
        if (curY + lineH > y + maxH) return;
        std::string line;
        std::string word;
        auto flush = [&](const std::string& w) {
            std::string test = line.empty() ? w : line + " " + w;
            int tw = 0;
            TTF_SizeUTF8(font, test.c_str(), &tw, nullptr);
            if (tw > maxW && !line.empty()) {
                if (curY + lineH > y + maxH) return;
                drawText(r, font, line, x, curY, c);
                curY += lineH;
                line  = w;
            } else {
                line = test;
            }
        };
        for (char ch : p + " ") {
            if (ch == ' ') {
                if (!word.empty()) { flush(word); word.clear(); }
            } else word += ch;
        }
        if (!line.empty() && curY + lineH <= y + maxH) {
            drawText(r, font, line, x, curY, c);
            curY += lineH;
        }
    }
}

// ── Game objects ──────────────────────────────────────────────────────────────

// ── Fractal Brownian motion (fBm) for organic background / music ──────────────
// Deterministic integer hash → float in [0, 1]
static float noiseHash(int x, int y) {
    unsigned n = (unsigned)(x * 1619 + y * 31337);
    n ^= n << 13; n ^= n >> 17; n ^= n << 5;
    return (n & 0x7fffffffu) / 2147483647.f;
}
// Bilinear value noise, smoothstep-interpolated
static float valueNoise(float x, float y) {
    int   ix = (int)std::floor(x), iy = (int)std::floor(y);
    float fx = x - ix,             fy = y - iy;
    float a = noiseHash(ix,   iy),  b = noiseHash(ix+1, iy);
    float c = noiseHash(ix,   iy+1),d = noiseHash(ix+1, iy+1);
    float ux = fx*fx*(3.f-2.f*fx), uy = fy*fy*(3.f-2.f*fy);
    return a + (b-a)*ux + (c-a)*uy + (a-b-c+d)*ux*uy;
}
// 3-octave fBm: successive halvings of amplitude, doublings of frequency
static float fbm(float x, float y) {
    return 0.500f * valueNoise(x,         y)
         + 0.250f * valueNoise(x*2.f+5.3f, y*2.f+1.7f)
         + 0.125f * valueNoise(x*4.f+2.1f, y*4.f+8.9f);
}

// Per-theme aurora palette (r, g, b)
static const int AURORA_C[5][3] = {
    {255, 210,  80},   // Day:    warm golden shimmer
    {220,  60, 180},   // Sunset: rose-purple aurora
    {  0, 220, 120},   // Night:  classic green aurora borealis
    {  0, 200, 255},   // Neon:   electric cyan discharge
    {160,  60, 255},   // Cave:   bioluminescent violet
};

// Organic aurora overlay — fBm drives per-row color and opacity.
// Activates at streak >= 4, ramps up in intensity over the next 4 streaks.
static void drawAurora(SDL_Renderer* r, int theme, float scroll, int streak) {
    if (streak < 4) return;
    float intensity = std::min(1.f, (streak - 3) / 4.f);  // 0 → 1 across streaks 4 → 7
    float t = scroll * 0.003f;                             // slow time drift

    const int* ac = AURORA_C[theme];
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    for (int y = GAME_TOP; y < WIN_H - 20; y += 2) {
        float ny = (y - GAME_TOP) / (float)GAME_H;
        // Two independent fBm layers: shape and texture
        float n1 = fbm(ny * 2.8f + t * 0.4f,  t);
        float n2 = fbm(ny * 5.0f + 7.3f,       t * 0.6f + 3.f);
        float alpha = n1 * n1 * intensity * 0.65f;
        if (alpha < 0.02f) continue;
        // Modulate hue slightly with n2 for colour depth
        Uint8 rr = (Uint8)(ac[0] * (0.55f + 0.45f * n2));
        Uint8 gg = (Uint8)(ac[1] * (0.50f + 0.50f * n1));
        Uint8 bb = (Uint8)(ac[2] * (0.55f + 0.45f * (1.f - n2)));
        Uint8 aa = (Uint8)(alpha * 210.f);
        SDL_SetRenderDrawColor(r, rr, gg, bb, aa);
        SDL_RenderDrawLine(r, 0, y, WIN_W, y);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

// ── Generative pentatonic melody ──────────────────────────────────────────────
// C major pentatonic over two octaves (A3 → A5), always consonant.
static const int PENTA[10] = {57,60,62,64,67, 69,72,74,76,79};

// Called (from main thread) when streak reaches 4+; lock-safe.
static void generateMelody(unsigned seed) {
    // Isolated LCG so this doesn't disturb the game's std::rand state.
    auto lcg = [](unsigned& s) -> unsigned {
        s = s * 1664525u + 1013904223u;
        return s >> 16;
    };

    float bpm = 118.f + (float)(lcg(seed) % 90);   // 118–208 BPM
    int   pos = 3 + (int)(lcg(seed) % 4);           // start near middle of scale

    const int LEN = 32;
    int notes[LEN];
    for (int i = 0; i < LEN; ++i) {
        unsigned r = lcg(seed);
        if ((r & 0xFF) < 38) { notes[i] = 0; continue; }  // ~15% rest
        int bias = (r >> 8) & 0xF;
        int step = (bias < 2) ? -2 : (bias < 6) ? -1 : (bias < 8) ? 0
                 : (bias < 12) ? 1 : 2;
        // Elastic boundary: pull toward centre when at edges
        if (pos <= 1) step += 1;
        if (pos >= 8) step -= 1;
        pos = std::max(0, std::min(9, pos + step));
        notes[i] = PENTA[pos];
    }

    SDL_LockAudioDevice(gAudio.devId);
    for (int i = 0; i < LEN; ++i) gGenNotes[i] = notes[i];
    gGenLen     = LEN;
    gGenBpm     = bpm;
    gGenMode    = true;
    gAudio.noteIdx  = 0;
    gAudio.sampLeft = 0;
    gAudio.phase    = 0.f;
    SDL_UnlockAudioDevice(gAudio.devId);
}

// ── Themed background renderer ────────────────────────────────────────────────
static void drawBackground(SDL_Renderer* r, int theme, float scroll) {
    const VisTheme& vt = VIS_THEMES[theme];

    // Sky gradient: 20 horizontal bands from top to bottom of game area
    const int BANDS = 20;
    for (int b = 0; b < BANDS; ++b) {
        float t  = (float)b / (BANDS - 1);
        SDL_Color c = {
            (Uint8)(vt.sky1.r + (int)(t * ((int)vt.sky2.r - vt.sky1.r))),
            (Uint8)(vt.sky1.g + (int)(t * ((int)vt.sky2.g - vt.sky1.g))),
            (Uint8)(vt.sky1.b + (int)(t * ((int)vt.sky2.b - vt.sky1.b))),
            255
        };
        int y = GAME_TOP + b * GAME_H / BANDS;
        fillRect(r, 0, y, WIN_W, GAME_H / BANDS + 1, c);
    }
    // Ground strip
    fillRect(r, 0, WIN_H - 20, WIN_W, 20, vt.ground);

    // Parallax props
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Color pc = vt.prop;
    const int TILE = 320;
    int off = (int)scroll % TILE;

    if (vt.kind == 0) {
        // Clouds (day / sunset) — two puffs per tile
        for (int tx = -TILE + (TILE - off); tx < WIN_W + TILE; tx += TILE) {
            // Cloud A
            fillRect(r, tx+10,  GAME_TOP+22, 68, 18, pc);
            fillRect(r, tx,     GAME_TOP+32, 90, 20, pc);
            fillRect(r, tx+12,  GAME_TOP+48, 66, 14, pc);
            // Cloud B (smaller)
            fillRect(r, tx+190, GAME_TOP+58, 48, 14, pc);
            fillRect(r, tx+183, GAME_TOP+66, 62, 16, pc);
        }
    } else if (vt.kind == 1) {
        // Stars (night) — dense field
        for (int tx = -TILE + (TILE - off); tx < WIN_W + TILE; tx += TILE) {
            int pts[][2] = {{12,18},{45,44},{90,12},{130,55},{170,28},{210,48},
                            {240,16},{280,62},{30,80},{110,76},{200,88},{265,35}};
            for (auto& p : pts) {
                int sx = tx + p[0], sy = GAME_TOP + p[1];
                SDL_SetRenderDrawColor(r, pc.r, pc.g, pc.b, pc.a);
                SDL_RenderDrawPoint(r, sx,   sy);
                SDL_RenderDrawPoint(r, sx+1, sy);
                SDL_RenderDrawPoint(r, sx,   sy+1);
            }
        }
        // Moon in fixed position (near top-right of game area)
        fillCircle(r, WIN_W - 80, GAME_TOP + 40, 18, {240, 230, 180, 200});
        fillCircle(r, WIN_W - 72, GAME_TOP + 34, 14, {20, 14, 60, 210});
    } else if (vt.kind == 2) {
        // Neon grid
        SDL_SetRenderDrawColor(r, pc.r, pc.g, pc.b, 22);
        for (int gy = GAME_TOP; gy < WIN_H; gy += 55)
            SDL_RenderDrawLine(r, 0, gy, WIN_W, gy);
        for (int tx = -TILE + (TILE - off); tx < WIN_W + TILE; tx += TILE) {
            SDL_SetRenderDrawColor(r, pc.r, pc.g, pc.b, 18);
            SDL_RenderDrawLine(r, tx, GAME_TOP, tx, WIN_H);
            // Glowing dots
            SDL_SetRenderDrawColor(r, pc.r, pc.g, pc.b, pc.a);
            int pts[][2] = {{30,50},{80,120},{140,60},{200,140},{260,40},{300,100}};
            for (auto& p : pts)
                fillRect(r, tx+p[0]-1, GAME_TOP+p[1]-1, 3, 3, pc);
        }
    } else {
        // Cave — stalactites hanging from ceiling + glowing crystals
        SDL_Color drip = {pc.r, pc.g, pc.b, (Uint8)(pc.a / 2)};
        for (int tx = -TILE + (TILE - off); tx < WIN_W + TILE; tx += TILE) {
            // Stalactites
            int sX[] = {15, 60, 110, 160, 220, 270};
            int sH[] = {45, 28, 55,  38,  62,  32 };
            for (int s = 0; s < 6; ++s) {
                int cx = tx + sX[s], h = sH[s];
                fillRect(r, cx,   GAME_TOP,    6,  h,    pc);
                fillRect(r, cx+2, GAME_TOP,    2,  h+10, pc);
                fillRect(r, cx-2, GAME_TOP+h-8, 10, 8,   drip);
            }
            // Floor crystals
            int fX[] = {40,  90,  150, 200, 260};
            int fH[] = {20,  30,  18,  26,  22 };
            for (int f = 0; f < 5; ++f) {
                int cx = tx + fX[f], h = fH[f];
                fillRect(r, cx,   WIN_H-20-h, 5, h, pc);
                fillRect(r, cx+2, WIN_H-20-h-6, 1, 6, pc);
            }
        }
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

struct Bird {
    float x = 130.f;
    float y = 0.f;
    float vy = 0.f;
};

struct Pipe {
    float x;
    float gapY;
    float gapYBase   = 0.f;  // oscillation centre
    float phaseOff   = 0.f;  // per-pipe phase so they don't all move in sync
    float gapY_B     = 0.f;
    bool  scored     = false;
    bool  isDecision = false;
};

// ── State ─────────────────────────────────────────────────────────────────────
enum class Phase     { Playing, Result, Complete };
enum class HintState { None, Waiting, Shown };

struct State {
    Bird              bird;
    std::vector<Pipe> pipes;
    Phase             phase       = Phase::Playing;
    bool              alive       = true;
    bool              decided     = false;
    bool              choseA      = false;
    bool              won         = false;
    bool              paused      = false;
    bool              wantSent    = false; // true after we wrote the .want signal file
    int               score       = 0;
    int               frame       = 0;
    int               qIdx        = 0;
    int               qTotal      = 1;
    int               flashTimer  = 0;
    int               streak      = 0;    // correct answers since last death; drives pipe speed
    int               plantTimer  = -1;   // -1 = no plant; >=0 = animation frame
    int               plantPipeX  = 0;   // left edge of pipe the plant emerges from
    int               plantCapY   = 0;   // y of the cap top (head rises upward from here)
    bool              gameSaved   = false; // user has saved this question to Saved Convos
    HintState         hintState   = HintState::None;
    std::string       hintText;
};

static float randGapY() {
    float mn = GAME_TOP + PIPE_GAP / 2.f + 35.f;
    float mx = WIN_H    - PIPE_GAP / 2.f - 35.f;
    return mn + (float)std::rand() / RAND_MAX * (mx - mn);
}

static void resetPipes(State& s) {
    auto makePipe = [](float x, float gy, float gyB, bool dec) {
        Pipe p;
        p.x          = x;
        p.gapY       = gy;
        p.gapYBase   = gy;
        p.phaseOff   = ((float)(std::rand() % 628)) / 100.f;  // 0..2π
        p.gapY_B     = gyB;
        p.isDecision = dec;
        return p;
    };
    s.pipes = {
        makePipe(WIN_W + 50.f,  randGapY(), 0.f,     false),
        makePipe(WIN_W + 360.f, randGapY(), 0.f,     false),
        makePipe(WIN_W + 680.f, DPIPE_GA,   DPIPE_GB, true),
    };
}

static void resetState(State& s, int qIdx, int qTotal) {
    setAudioTheme(gTheme);     // snap back to theme music; clears gGenMode
    s.bird  = {130.f, GAME_TOP + GAME_H / 2.f, 0.f};
    resetPipes(s);
    s.phase      = Phase::Playing;
    s.alive      = true;
    s.decided    = false;
    s.choseA     = false;
    s.won        = false;
    s.score      = 0;
    s.frame      = 0;
    s.flashTimer = 0;
    s.wantSent   = false;
    s.streak     = 0;
    s.plantTimer = -1;
    s.gameSaved  = false;
    s.hintState  = HintState::None;
    s.hintText.clear();
    s.qIdx       = qIdx;
    s.qTotal     = qTotal;
}

// Advance to next question while keeping the bird in the air.
static void advanceKeepBird(State& s) {
    int nextIdx = s.qIdx + 1;
    if (nextIdx >= s.qTotal) {
        // No more questions yet — wait; preserve qIdx so we resume at the right place.
        s.qIdx  = nextIdx;
        s.phase = Phase::Complete;
        return;
    }
    Bird saved    = s.bird;
    int  qTotal   = s.qTotal;
    bool wantSent = s.wantSent;
    int  streak   = s.streak + 1;  // increment before resetState clears it
    resetState(s, nextIdx, qTotal);
    s.bird       = saved;
    s.wantSent   = wantSent;
    s.streak     = streak;
    s.flashTimer = 50;
    gTheme = nextTheme();
    setAudioTheme(gTheme);
    if (streak >= 4)
        generateMelody((unsigned)(SDL_GetTicks() ^ (unsigned)(streak * 8191u)));
}

// ── Pixel-art pipe helpers ────────────────────────────────────────────────────

// Draw the body strip of a pipe (width = PIPE_W).
// PIPE_W = 74: 3 dark | 11 hi1 | 7 hi2 | 41 body | 9 shad | 3 dark
static void pipeBody(SDL_Renderer* r, int x, int y, int h) {
    if (h <= 0) return;
    const int w = (int)PIPE_W;
    fillRect(r, x,       y, 3,          h, CP_DARK);
    fillRect(r, x+3,     y, 11,         h, CP_HI1);
    fillRect(r, x+14,    y, 7,          h, CP_HI2);
    fillRect(r, x+21,    y, w-21-9-3,   h, CP_BODY);
    fillRect(r, x+w-12,  y, 9,          h, CP_SHAD);
    fillRect(r, x+w-3,   y, 3,          h, CP_DARK);
}

// Draw the wider cap at position (pipeX, capY), height = CAP_H.
// The cap extends CAP_EXT px beyond the body on each side.
// Horizontal highlight at the top of the interior gives the classic "lip" look.
static void pipeCap(SDL_Renderer* r, int pipeX, int capY) {
    const int capX = pipeX - CAP_EXT;
    const int capW = (int)PIPE_W + CAP_EXT * 2;

    // Outer dark border
    fillRect(r, capX, capY, capW, CAP_H, CP_DARK);

    // Inner area
    const int iX = capX + 3, iW = capW - 6;
    const int iY = capY + 3, iH = CAP_H - 6;

    // Base fill
    fillRect(r, iX, iY, iW, iH, CP_BODY);
    // Horizontal highlight strip at top (the "face" of the cap)
    fillRect(r, iX, iY, iW, 5, CP_HI2);
    // Vertical strips (same proportions as the body, overwrite horizontals)
    fillRect(r, iX,       iY, 11, iH, CP_HI1);
    fillRect(r, iX+11,    iY, 7,  iH, CP_HI2);
    fillRect(r, iX+iW-9,  iY, 9,  iH, CP_SHAD);
}

// ── Carnivorous plant – Piranha Plant head emerges from existing pipe cap ─────
// The head rises from inside the wrong-answer gap's bottom pipe cap up to the
// bird. No new pipe is drawn — the decision pipe is already on screen.
//   0–16  : head rises from cap interior to bird y
//  16–34  : jaws swing open
//  34–46  : jaws snap shut
//  46+    : closed (bird eaten)
static void drawPlant(SDL_Renderer* r, int pipeX, int capY, float birdY, int timer) {
    // Horizontal centre of the pipe
    const int cx = pipeX + (int)(PIPE_W / 2.f);

    const SDL_Color C_HEAD   = {55,  175, 55,  255};
    const SDL_Color C_HSPOT  = {180, 230, 140, 255};
    const SDL_Color C_LIP    = {35,  130, 25,  255};
    const SDL_Color C_INSIDE = {190, 20,  20,  255};
    const SDL_Color C_TOOTH  = {245, 245, 215, 255};
    const SDL_Color C_NECK   = {70,  155, 40,  255};

    auto ease = [](float t) -> float {
        if (t <= 0.f) return 0.f;
        if (t >= 1.f) return 1.f;
        return t * t * (3.f - 2.f * t);
    };

    // ── Head rises (frames 0–16) ──────────────────────────────────────────────
    const int HEAD_R = 26;
    int hcyIn  = capY + HEAD_R + 6;    // head centre just inside the pipe opening
    int hcyOut = (int)birdY;           // head centre at bird position
    int hcy    = (int)(hcyIn + ease(timer / 16.f) * (float)(hcyOut - hcyIn));

    // Neck strip visible once the head clears the cap top
    int neckTop = hcy + HEAD_R;
    if (neckTop < capY)
        fillRect(r, cx - 8, neckTop, 16, capY - neckTop, C_NECK);

    // ── Jaw fraction ─────────────────────────────────────────────────────────
    float jaw;
    if      (timer < 16) jaw = 0.f;
    else if (timer < 34) jaw = ease((timer - 16) / 18.f);
    else if (timer < 36) jaw = 1.f;
    else if (timer < 46) jaw = 1.f - ease((timer - 36) / 10.f);
    else                 jaw = 0.f;

    const float MAX_OPEN = 22.f;
    float       gap      = jaw * MAX_OPEN;
    const int   mCy      = hcy + 9;
    const int   JAW_W    = 46;
    const int   jx       = cx - JAW_W / 2;

    // Head circle
    fillCircle(r, cx, hcy, HEAD_R, C_HEAD);

    // Mouth interior (over head circle)
    if (gap > 1.f)
        fillRect(r, jx + 5, mCy - (int)gap, JAW_W - 10, (int)(gap * 2.f), C_INSIDE);

    // Teeth (visible only when open)
    if (gap > 2.f) {
        const int NT = 3, TW = 8, TH = 10, ts = (JAW_W - 16) / (NT - 1);
        for (int t = 0; t < NT; ++t) {
            int tx = jx + 8 + t * ts;
            fillRect(r, tx, mCy - (int)gap,           TW, TH, C_TOOTH);
            fillRect(r, tx, mCy + (int)gap - TH + 1,  TW, TH, C_TOOTH);
        }
    }

    // Lip trim (closed-mouth crease when gap == 0)
    fillRect(r, jx, mCy - (int)gap - 2, JAW_W, 3, C_LIP);
    fillRect(r, jx, mCy + (int)gap - 1, JAW_W, 3, C_LIP);

    // Highlight spots
    fillCircle(r, cx - 10, hcy - 13, 5, C_HSPOT);
    fillCircle(r, cx +  5, hcy - 18, 4, C_HSPOT);
}

// ── Pipe drawing ──────────────────────────────────────────────────────────────
static void drawPipe(SDL_Renderer* r, const Pipe& p) {
    const int px   = (int)p.x;
    const int gapT = (int)(p.gapY - PIPE_GAP / 2.f);
    const int gapB = (int)(p.gapY + PIPE_GAP / 2.f);

    // Top pipe: body from GAME_TOP down to cap, then cap
    pipeBody(r, px, GAME_TOP, gapT - GAME_TOP - CAP_H);
    pipeCap (r, px, gapT - CAP_H);

    // Bottom pipe: cap then body down to WIN_H
    pipeCap (r, px, gapB);
    pipeBody(r, px, gapB + CAP_H, WIN_H - gapB - CAP_H);
}

// ── Decision-pipe drawing (two gaps: A=top, B=bottom) ─────────────────────────
static void drawDecisionPipe(SDL_Renderer* r, const Pipe& p) {
    const int px = (int)p.x;
    constexpr float hg = DPIPE_GAP / 2.f;
    const int gA_top = (int)(p.gapY   - hg);
    const int gA_bot = (int)(p.gapY   + hg);
    const int gB_top = (int)(p.gapY_B - hg);
    const int gB_bot = (int)(p.gapY_B + hg);

    // Top section
    pipeBody(r, px, GAME_TOP, gA_top - GAME_TOP - CAP_H);
    pipeCap (r, px, gA_top - CAP_H);

    // Gap A — blue tint
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 31, 111, 235, 80);
    { SDL_Rect rc{px, gA_top, (int)PIPE_W, (int)DPIPE_GAP}; SDL_RenderFillRect(r, &rc); }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // Middle section: cap facing up, body, cap facing down
    pipeCap (r, px, gA_bot);
    pipeBody(r, px, gA_bot + CAP_H, gB_top - CAP_H - (gA_bot + CAP_H));
    pipeCap (r, px, gB_top - CAP_H);

    // Gap B — purple tint
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 110, 64, 201, 80);
    { SDL_Rect rc{px, gB_top, (int)PIPE_W, (int)DPIPE_GAP}; SDL_RenderFillRect(r, &rc); }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // Bottom section: cap then body
    pipeCap (r, px, gB_bot);
    pipeBody(r, px, gB_bot + CAP_H, WIN_H - gB_bot - CAP_H);
}

// ── Bird drawing — pixel-art Flappy Bird style ────────────────────────────────
static void drawBird(SDL_Renderer* r, const Bird& b, int frame) {
    // Each entry is a "logical pixel" of size PS rendered as a filled rect.
    // The grid faces right: col 0 = leftmost (back), col 11 = rightmost (beak tip).
    // Colours:
    //   0 transparent  1 yellow body   2 dark outline
    //   3 white eye    4 dark pupil    5 red beak
    //   6 dark red     7 cream wing
    static const SDL_Color PAL[] = {
        {0,   0,   0,   0  },  // 0 transparent
        {247, 201, 72,  255},  // 1 yellow
        {100, 70,  10,  255},  // 2 dark outline
        {255, 255, 240, 255},  // 3 white eye
        {20,  20,  20,  255},  // 4 pupil
        {220, 58,  18,  255},  // 5 red beak
        {155, 30,  8,   255},  // 6 dark red
        {240, 225, 150, 255},  // 7 cream wing
    };

    // 10 rows × 12 cols.  Row 0 = top.
    static const uint8_t GRID[10][12] = {
        {0,0,2,2,2,2,0, 0,0,0,0,0},
        {0,2,1,1,1,1,2, 0,0,0,0,0},
        {2,1,7,1,3,3,1, 2,0,0,0,0},
        {2,1,7,1,3,4,1, 1,5,5,0,0},
        {2,1,7,1,1,1,1, 5,5,6,0,0},
        {2,1,1,1,1,1,1, 5,6,0,0,0},
        {2,1,1,1,1,1,2, 0,0,0,0,0},
        {0,2,1,1,1,2,0, 0,0,0,0,0},
        {0,0,2,2,2,0,0, 0,0,0,0,0},
        {0,0,0,0,0,0,0, 0,0,0,0,0},
    };

    // Wing flap: swap row-2 col-2 between cream (7) and yellow (1) every 8 frames.
    bool wingUp = (frame / 8) % 2 == 0;

    const int PS  = 4;   // SDL pixels per logical pixel
    const int OX  = (int)b.x - 5 * PS;   // left edge of grid
    const int OY  = (int)b.y - 4 * PS;   // top edge of grid

    // Drop shadow
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 50);
    for (int row = 0; row < 10; ++row)
        for (int col = 0; col < 12; ++col)
            if (GRID[row][col] != 0) {
                SDL_Rect rc{ OX + col*PS + 3, OY + row*PS + 4, PS, PS };
                SDL_RenderFillRect(r, &rc);
            }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    for (int row = 0; row < 10; ++row) {
        for (int col = 0; col < 12; ++col) {
            uint8_t p = GRID[row][col];
            if (p == 0) continue;
            // Wing animation: row 2-4, col 2 toggles between 7 and 1
            if (col == 2 && row >= 2 && row <= 4 && p == 7)
                p = wingUp ? 7 : 1;
            SDL_Color c = PAL[p];
            SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
            SDL_Rect rc{ OX + col*PS, OY + row*PS, PS, PS };
            SDL_RenderFillRect(r, &rc);
        }
    }
}

// ── Decision zone ─────────────────────────────────────────────────────────────
static void drawDecisionZone(SDL_Renderer* r, TTF_Font* medFont, TTF_Font* smFont,
                              const GameData& d, int theme) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    // On bright (day/sunset) backgrounds use a dark opaque panel so light text stays readable.
    SDL_Color bgA = VIS_THEMES[theme].lightBg ? SDL_Color{0,  20, 110, 230} : C_A_BG;
    SDL_Color bgB = VIS_THEMES[theme].lightBg ? SDL_Color{45,  0, 140, 230} : C_B_BG;

    // Zone A background (top half)
    fillRect(r, GATE_X, GAME_TOP, WIN_W - GATE_X, GAME_H / 2, bgA);
    // Zone B background (bottom half)
    fillRect(r, GATE_X, GATE_MID, WIN_W - GATE_X, GAME_H - GAME_H / 2, bgB);

    // Solid edge line separating obstacle area from decision zone
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    for (int yy = GAME_TOP; yy < WIN_H; yy += 10)
        drawVLine(r, GATE_X - 2, yy, yy + 5, {255, 255, 255, 40});

    // Divider between A and B
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    drawHLine(r, GATE_X, WIN_W, GATE_MID, {255, 255, 255, 80});

    int zx  = GATE_X + 12;
    int zw  = WIN_W - GATE_X - 16;

    // "A" big label
    drawText(r, medFont, "A", zx, GAME_TOP + 8, C_A_LABEL);
    // Choice A wrapped text
    drawWrapped(r, smFont, d.choiceA, zx + 22, GAME_TOP + 10,
                zw - 24, GAME_H / 2 - 20, C_A_TEXT);

    // "B" big label
    drawText(r, medFont, "B", zx, GATE_MID + 8, C_B_LABEL);
    // Choice B wrapped text
    drawWrapped(r, smFont, d.choiceB, zx + 22, GATE_MID + 10,
                zw - 24, GAME_H / 2 - 20, C_B_TEXT);
}

// ── Question bar ──────────────────────────────────────────────────────────────
static void drawQBar(SDL_Renderer* r, TTF_Font* smFont, const GameData& d, int streak) {
    fillRect(r, 0, 0, WIN_W, QBAR_H, C_QBAR);
    drawHLine(r, 0, WIN_W, QBAR_H - 1, C_BORDER);

    drawText(r, smFont, "Q", 14, 10, C_MUTED);
    drawWrapped(r, smFont, d.question, 34, 10, GATE_X - 50, QBAR_H - 16, C_TEXT);

    if (streak > 0) {
        std::string badge = "\xF0\x9F\x94\xA5 " + std::to_string(streak);  // 🔥
        drawText(r, smFont, badge, GATE_X - 78, QBAR_H - 22, {255, 160, 30, 255});
    }
}

// ── Result / Complete overlay ─────────────────────────────────────────────────
static void drawResult(SDL_Renderer* r, TTF_Font* lgFont, TTF_Font* medFont,
                       TTF_Font* smFont, const State& s, const GameData& d) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    fillRect(r, 0, GAME_TOP, WIN_W, GAME_H, C_OVERLAY);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    int cx  = WIN_W / 2;
    int mid = GAME_TOP + GAME_H / 2;

    if (s.phase == Phase::Complete) {
        static const char* dots[] = {".", "..", "..."};
        Uint32 t = SDL_GetTicks();
        drawTextCX(r, medFont, "Loading more questions" + std::string(dots[(t / 500) % 3]),
                   cx, mid, C_MUTED);
        gSaveBtnRect = {0, 0, 0, 0};
        return;
    }

    // Wrong answer or crash — title differs based on cause of death.
    const char* title = s.decided ? "Wrong!" : "Crash!";
    drawTextCX(r, lgFont,  title,                    cx, mid - 90, C_ERR);
    drawTextCX(r, smFont,  "The correct answer was:", cx, mid - 40, C_MUTED);
    std::string corr = d.correctIsA ? ("A: " + d.choiceA) : ("B: " + d.choiceB);
    drawWrapped(r, medFont, corr, cx - 280, mid - 18, 560, 70, C_TEXT);
    drawTextCX(r, medFont, "Space or click to try again", cx, mid + 60, C_MUTED);
    drawTextCX(r, smFont,  "Q to quit",               cx, mid + 92, C_MUTED);

    // Save button
    const int BW = 160, BH = 34;
    const int bx = cx - BW / 2, by = mid + 124;
    gSaveBtnRect = {bx, by, BW, BH};

    SDL_Color btnBg  = s.gameSaved ? SDL_Color{50, 120, 50, 220} : SDL_Color{30, 80, 160, 220};
    SDL_Color btnFg  = {230, 240, 255, 255};
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    fillRect(r, bx, by, BW, BH, btnBg);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    const char* label = s.gameSaved ? "\xE2\x9C\x93 Saved" : "\xF0\x9F\x94\x96 Save";
    drawTextCX(r, smFont, label, cx, by + 9, btnFg);
}

// ── Entry point ───────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    logOpen();
    logf("=== test-taker-game starting ===");

    if (argc < 2) {
        logf("ERROR: no data file argument");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "test-taker-game",
                                 "Usage: test-taker-game <data-file>", nullptr);
        return 1;
    }
    logf("data file: %s", argv[1]);

    std::vector<GameData> questions = ReadGameFiles(argv[1]);
    logf("questions loaded: %d", (int)questions.size());
    if (questions.empty() || questions[0].question.empty()) {
        logf("ERROR: empty question list or blank first question");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "test-taker-game",
                                 "Could not read game data file.", nullptr);
        return 1;
    }
    for (int i = 0; i < (int)questions.size(); ++i)
        logf("  q[%d]: \"%s\" | A=\"%s\" | B=\"%s\" | correctIsA=%d",
             i, questions[i].question.substr(0,60).c_str(),
             questions[i].choiceA.substr(0,40).c_str(),
             questions[i].choiceB.substr(0,40).c_str(),
             (int)questions[i].correctIsA);

    if (SDL_Init(SDL_INIT_VIDEO) < 0 || TTF_Init() < 0) {
        logf("ERROR: SDL_Init or TTF_Init failed: %s", SDL_GetError());
        return 1;
    }
    logf("SDL_Init OK");

    SDL_Window* win = SDL_CreateWindow(
        "Quiz Challenge — Flappy Bird",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) { logf("ERROR: CreateWindow: %s", SDL_GetError()); SDL_Quit(); return 1; }

    // Log logical vs drawable size (HiDPI check)
    {
        int ww, wh, dw, dh;
        SDL_GetWindowSize(win, &ww, &wh);
        SDL_GL_GetDrawableSize(win, &dw, &dh);
        logf("window logical=%dx%d  drawable=%dx%d  ratio=%.2f",
             ww, wh, dw, dh, dw > 0 ? (float)dw/ww : 0.f);
    }

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        logf("WARN: vsync renderer failed (%s) – trying without vsync", SDL_GetError());
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    }
    if (!ren) { logf("ERROR: CreateRenderer: %s", SDL_GetError()); SDL_DestroyWindow(win); SDL_Quit(); return 1; }

    // Log renderer info
    {
        SDL_RendererInfo ri;
        if (SDL_GetRendererInfo(ren, &ri) == 0) {
            int ow, oh;
            SDL_GetRendererOutputSize(ren, &ow, &oh);
            logf("renderer name=%s  flags=0x%x  output=%dx%d",
                 ri.name, ri.flags, ow, oh);
            logf("vsync active: %s",
                 (ri.flags & SDL_RENDERER_PRESENTVSYNC) ? "YES" : "NO");
        }
    }

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    TTF_Font* lgFont     = loadFont(27);
    TTF_Font* medFont    = loadFont(16);
    TTF_Font* smFont     = loadFont(13);
    // Larger variants used when the current question contains CJK characters.
    TTF_Font* medFontCJK = loadFont(21);
    TTF_Font* smFontCJK  = loadFont(17);

    std::srand((unsigned)SDL_GetTicks());
    initAudio();
    shuffleThemeQueue(-1);
    gTheme = gThemeQueue[0];
    gThemePos = 0;
    setAudioTheme(gTheme);

    const std::string dataFilePath = argv[1];
    State state;
    resetState(state, 0, (int)questions.size());
    logf("initial state: qTotal=%d  bird y=%.1f  GAME_TOP=%d  WIN_H=%d",
         state.qTotal, state.bird.y, GAME_TOP, WIN_H);

    Uint32 fpsTimer    = SDL_GetTicks();
    int    fpsCount    = 0;

    bool quit   = false;
    bool allWon = false;
    while (!quit) {

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT:
                    quit = true;
                    break;
                case SDL_KEYDOWN: {
                    auto sym = ev.key.keysym.sym;
                    if (sym == SDLK_ESCAPE) {
                        state.paused = !state.paused;
                    } else if (sym == SDLK_q) {
                        quit = true;
                    } else if (state.paused && sym == SDLK_h
                               && state.phase == Phase::Playing
                               && state.hintState == HintState::None) {
                        const GameData& gd = questions[std::min(state.qIdx, (int)questions.size() - 1)];
                        std::ofstream hf(dataFilePath + ".hintreq");
                        hf << "Q: " << gd.question << "\n"
                           << "A: " << gd.choiceA  << "\n"
                           << "B: " << gd.choiceB  << "\n";
                        state.hintState = HintState::Waiting;
                        logf("HINT requested q=%d", state.qIdx);
                    } else if (!state.paused && (sym == SDLK_SPACE || sym == SDLK_UP)) {
                        if (state.phase == Phase::Playing && state.alive && !state.decided) {
                            state.bird.vy = FLAP;
                            logf("FLAP key  frame=%d y=%.1f vy=%.2f", state.frame, state.bird.y, state.bird.vy);
                        } else if (state.phase == Phase::Result && !state.won
                                   && (state.plantTimer < 0 || state.plantTimer >= 46))
                            resetState(state, state.qIdx, state.qTotal);
                    }
                    break;
                }
                case SDL_MOUSEBUTTONDOWN:
                    if (!state.paused && ev.button.button == SDL_BUTTON_LEFT) {
                        int mx = ev.button.x, my = ev.button.y;
                        bool inSaveBtn = gSaveBtnRect.w > 0
                                      && mx >= gSaveBtnRect.x
                                      && mx <  gSaveBtnRect.x + gSaveBtnRect.w
                                      && my >= gSaveBtnRect.y
                                      && my <  gSaveBtnRect.y + gSaveBtnRect.h;
                        if (inSaveBtn && state.phase == Phase::Result
                                      && !state.won && !state.gameSaved) {
                            // Write .save signal file for the main app to pick up
                            const GameData& gd = questions[state.qIdx];
                            std::string corr = gd.correctIsA ? gd.choiceA : gd.choiceB;
                            std::ofstream sf(dataFilePath + ".save");
                            sf << "Q: " << gd.question << "\n"
                               << "A: " << corr << "\n";
                            state.gameSaved = true;
                            logf("SAVE q=%d", state.qIdx);
                        } else if (state.phase == Phase::Playing && state.alive && !state.decided) {
                            state.bird.vy = FLAP;
                            logf("FLAP click frame=%d y=%.1f vy=%.2f", state.frame, state.bird.y, state.bird.vy);
                        } else if (!inSaveBtn && state.phase == Phase::Result && !state.won
                                   && (state.plantTimer < 0 || state.plantTimer >= 46))
                            resetState(state, state.qIdx, state.qTotal);
                    }
                    break;
                default: break;
            }
        }

        // ── Poll for hint response ────────────────────────────────────────────
        if (state.hintState == HintState::Waiting && state.frame % 30 == 0) {
            std::ifstream rf(dataFilePath + ".hintresp");
            if (rf.good()) {
                std::ostringstream ss;
                ss << rf.rdbuf();
                rf.close();
                state.hintText = ss.str();
                while (!state.hintText.empty() &&
                       (state.hintText.back() == '\n' || state.hintText.back() == ' '))
                    state.hintText.pop_back();
                std::remove((dataFilePath + ".hintresp").c_str());
                state.hintState = HintState::Shown;
                logf("HINT received: %s", state.hintText.substr(0, 60).c_str());
            }
        }

        // ── Poll file for new questions every ~2 s ────────────────────────────
        if (state.frame % 120 == 0 && state.frame > 0) {
            auto fresh = ReadGameFiles(dataFilePath);
            if ((int)fresh.size() > (int)questions.size()) {
                logf("POLL new questions: %d -> %d", (int)questions.size(), (int)fresh.size());
                questions    = std::move(fresh);
                state.qTotal = (int)questions.size();
                state.wantSent = false;  // allow another signal for the new batch
                if (state.phase == Phase::Complete && state.qIdx < state.qTotal) {
                    // Seamlessly resume from where we left off.
                    Bird saved = state.bird;
                    resetState(state, state.qIdx, state.qTotal);
                    state.bird       = saved;
                    state.flashTimer = 30;
                }
            }
        }

        // Rebind after any potential vector reallocation above.
        const GameData& data = questions[std::min(state.qIdx, state.qTotal - 1)];

        // ── Signal for more questions when 2 away from the end ───────────────
        if (!state.wantSent && state.phase == Phase::Playing
                && state.qIdx + 2 >= state.qTotal) {
            std::ofstream wf(dataFilePath + ".want");
            state.wantSent = true;
            logf("WANT written at q=%d/%d", state.qIdx+1, state.qTotal);
        }

        // ── Physics update ────────────────────────────────────────────────────
        if (!state.paused && state.phase == Phase::Playing && state.alive && !state.decided) {
            state.bird.vy += GRAVITY;
            state.bird.y  += state.bird.vy;

            if (state.bird.y - BIRD_R < GAME_TOP - COLL_GRACE || state.bird.y + BIRD_R > WIN_H + COLL_GRACE) {
                logf("DEATH boundary  frame=%d y=%.1f vy=%.2f  (top=%d bot=%d)",
                     state.frame, state.bird.y, state.bird.vy, GAME_TOP, WIN_H);
                state.alive = false;
                state.phase = Phase::Result;
            }

            // Oscillate regular-pipe gaps based on streak.
            // amplitude grows with streak (capped at 75 px); frequency also grows.
            {
                float amp  = std::min(state.streak * 9.f, 75.f);
                float freq = 0.022f + std::min(state.streak * 0.003f, 0.038f);
                float mn   = GAME_TOP + PIPE_GAP / 2.f + 35.f;
                float mx   = WIN_H    - PIPE_GAP / 2.f - 35.f;
                for (auto& p : state.pipes) {
                    if (!p.isDecision)
                        p.gapY = std::max(mn, std::min(mx,
                            p.gapYBase + amp * std::sin(freq * state.frame + p.phaseOff)));
                }
            }

            for (auto& p : state.pipes) {
                p.x -= SPEED;
                bool inX = state.bird.x + BIRD_R > p.x &&
                           state.bird.x - BIRD_R < p.x + PIPE_W;
                if (p.isDecision) {
                    constexpr float hg = DPIPE_GAP / 2.f;
                    if (inX) {
                        bool inGapA = state.bird.y - BIRD_R >= p.gapY   - hg - COLL_GRACE &&
                                      state.bird.y + BIRD_R <= p.gapY   + hg + COLL_GRACE;
                        bool inGapB = state.bird.y - BIRD_R >= p.gapY_B - hg - COLL_GRACE &&
                                      state.bird.y + BIRD_R <= p.gapY_B + hg + COLL_GRACE;
                        if (!inGapA && !inGapB) {
                            logf("DEATH decision-pipe  frame=%d y=%.1f  gapA=[%.1f,%.1f] gapB=[%.1f,%.1f]  bird=[%.1f,%.1f]",
                                 state.frame, state.bird.y,
                                 p.gapY-hg, p.gapY+hg, p.gapY_B-hg, p.gapY_B+hg,
                                 state.bird.y-BIRD_R, state.bird.y+BIRD_R);
                            state.alive = false;
                            state.phase = Phase::Result;
                        }
                    }
                    if (!p.scored && state.bird.x >= p.x + PIPE_W / 2.f) {
                        p.scored      = true;
                        state.choseA  = (state.bird.y >= p.gapY - hg &&
                                         state.bird.y <= p.gapY + hg);
                        state.decided = true;
                        state.won     = (state.choseA == data.correctIsA);
                        logf("DECISION  frame=%d y=%.1f choseA=%d correctIsA=%d won=%d",
                             state.frame, state.bird.y,
                             (int)state.choseA, (int)data.correctIsA, (int)state.won);
                        if (state.won) {
                            logf("ADVANCE q%d -> q%d", state.qIdx, state.qIdx+1);
                            advanceKeepBird(state);   // zero interruption on correct
                        } else {
                            state.phase      = Phase::Result;
                            state.plantTimer = 0;     // start carnivorous-plant animation
                            // Plant emerges from the bottom cap of the wrong-answer gap
                            constexpr float hgp = DPIPE_GAP / 2.f;
                            state.plantPipeX = (int)p.x;
                            state.plantCapY  = state.choseA
                                ? (int)(p.gapY   + hgp)   // bottom cap of gap A
                                : (int)(p.gapY_B + hgp);  // bottom cap of gap B
                        }
                    }
                } else {
                    if (inX) {
                        bool inGap = state.bird.y - BIRD_R >= p.gapY - PIPE_GAP / 2.f - COLL_GRACE &&
                                     state.bird.y + BIRD_R <= p.gapY + PIPE_GAP / 2.f + COLL_GRACE;
                        if (!inGap) {
                            logf("DEATH pipe  frame=%d y=%.1f gapY=%.1f gap=[%.1f,%.1f]  bird=[%.1f,%.1f]",
                                 state.frame, state.bird.y, p.gapY,
                                 p.gapY-PIPE_GAP/2.f, p.gapY+PIPE_GAP/2.f,
                                 state.bird.y-BIRD_R, state.bird.y+BIRD_R);
                            state.alive = false;
                            state.phase = Phase::Result;
                        }
                    }
                    if (!p.scored && state.bird.x > p.x + PIPE_W) {
                        p.scored = true;
                        ++state.score;
                    }
                }
            }
        }

        // Advance carnivorous-plant animation
        if (!state.paused && state.phase == Phase::Result && state.plantTimer >= 0)
            ++state.plantTimer;

        // ── Render ────────────────────────────────────────────────────────────
        if (!state.paused && state.phase == Phase::Playing)
            gBgScroll += SPEED * 0.25f;

        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        fillRect(ren, 0, 0, WIN_W, QBAR_H, C_BG);
        drawBackground(ren, gTheme, gBgScroll);
        drawAurora(ren, gTheme, gBgScroll, state.streak);

        for (const auto& p : state.pipes) {
            if (p.isDecision) drawDecisionPipe(ren, p);
            else              drawPipe(ren, p);
        }

        // Bird is hidden once the plant jaws snap shut (timer >= 46)
        bool plantEating = state.phase == Phase::Result
                        && state.plantTimer >= 46;
        if (state.alive && !plantEating)
            drawBird(ren, state.bird, state.frame);

        // Draw plant animation (wrong-answer only)
        if (state.phase == Phase::Result && state.plantTimer >= 0)
            drawPlant(ren, state.plantPipeX, state.plantCapY, state.bird.y, state.plantTimer);

        // Pick larger fonts when any part of the current question is CJK.
        bool cjk = hasCJK(data.question) || hasCJK(data.choiceA) || hasCJK(data.choiceB);
        TTF_Font* activeMed = cjk ? medFontCJK : medFont;
        TTF_Font* activeSm  = cjk ? smFontCJK  : smFont;

        drawDecisionZone(ren, activeMed, activeSm, data, gTheme);

        drawQBar(ren, activeSm, data, state.streak);

        // "Tap to flap" hint (fades out)
        if (state.frame < 140 && state.phase == Phase::Playing) {
            Uint8 alpha = (Uint8)std::max(0, 190 - state.frame * 2);
            drawTextCX(ren, activeSm, "Click  or  Space  to  flap",
                       WIN_W / 2 - 40, WIN_H - 26, {255, 255, 255, alpha});
        }

        // Show result overlay after plant animation finishes (or immediately if no plant)
        bool plantDone = state.plantTimer < 0 || state.plantTimer >= 65;
        if ((state.phase == Phase::Result || state.phase == Phase::Complete) && plantDone)
            drawResult(ren, lgFont, activeMed, activeSm, state, data);

        // ── "✓ Correct!" flash badge after seamless advance ───────────────────
        if (state.flashTimer > 0 && !state.paused) {
            --state.flashTimer;
            Uint8 a = (Uint8)(state.flashTimer * 5);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 20, 80, 20, (Uint8)(a / 2));
            SDL_Rect badge{8, GAME_TOP + 4, 130, 26};
            SDL_RenderFillRect(ren, &badge);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
            drawText(ren, smFont, "✓  Correct!", 14, GAME_TOP + 8,
                     {(Uint8)C_OK.r, (Uint8)C_OK.g, (Uint8)C_OK.b, a});
        }

        // ── Pause overlay ─────────────────────────────────────────────────────
        if (state.paused) {
            // Light veil — game is still fully visible beneath.
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 140);
            SDL_Rect fullGame{0, GAME_TOP, WIN_W, GAME_H};
            SDL_RenderFillRect(ren, &fullGame);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

            int cx  = WIN_W / 2;
            int mid = GAME_TOP + GAME_H / 2;
            drawTextCX(ren, lgFont,  "Paused",        cx, mid - 60, C_TEXT);
            drawTextCX(ren, medFont, "ESC to resume", cx, mid -  4, C_MUTED);

            // Hint section (only while playing, not on result/complete screen)
            if (state.phase == Phase::Playing) {
                if (state.hintState == HintState::None) {
                    drawTextCX(ren, smFont, "H  \xe2\x80\x94  hint",  cx, mid + 26, C_MUTED);
                } else if (state.hintState == HintState::Waiting) {
                    SDL_Color amber = {220, 160, 30, 255};
                    drawTextCX(ren, smFont, "\xe2\x8f\xb3 fetching hint\xe2\x80\xa6", cx, mid + 26, amber);
                } else {
                    SDL_Color gold  = {240, 200, 50, 255};
                    SDL_Color light = {230, 230, 230, 255};
                    drawTextCX(ren, smFont, "\xf0\x9f\x92\xa1 Hint:", cx, mid + 22, gold);
                    drawWrapped(ren, smFont, state.hintText,
                                cx - 260, mid + 44, 520, 110, light);
                }
            }

            drawTextCX(ren, smFont, "Q  \xe2\x80\x94  quit", cx, mid + 150, C_MUTED);

            // Debug: bird position
            std::string dbg = "bird y=" + std::to_string((int)state.bird.y)
                            + "  vy=" + std::to_string((int)(state.bird.vy * 100) / 100)
                            + "  frame=" + std::to_string(state.frame)
                            + "  q=" + std::to_string(state.qIdx + 1) + "/" + std::to_string(state.qTotal);
            drawText(ren, smFont, dbg, 10, WIN_H - 20, C_MUTED);
        }

        SDL_RenderPresent(ren);
        ++state.frame;
        ++fpsCount;

        // Frame cap fallback: if vsync didn't hold, enforce ~60 fps via SDL_Delay.
        {
            static Uint32 lastTick = 0;
            Uint32 now = SDL_GetTicks();
            if (lastTick == 0) lastTick = now;
            Uint32 elapsed = now - lastTick;
            if (elapsed < 16) SDL_Delay(16 - elapsed);
            lastTick = SDL_GetTicks();
        }

        // Log FPS every 300 frames.
        if (state.frame % 300 == 0) {
            Uint32 now = SDL_GetTicks();
            float fps = fpsCount * 1000.f / (float)(now - fpsTimer + 1);
            logf("FPS=%.1f  frame=%d  q=%d/%d  bird y=%.1f vy=%.2f  phase=%d  alive=%d",
                 fps, state.frame, state.qIdx+1, state.qTotal,
                 state.bird.y, state.bird.vy,
                 (int)state.phase, (int)state.alive);
            fpsTimer = now; fpsCount = 0;
        }
    }
    logf("=== game loop exited ===");
    logClose();
    (void)allWon;

    closeAudio();
    TTF_CloseFont(lgFont);
    TTF_CloseFont(medFont);
    TTF_CloseFont(smFont);
    TTF_CloseFont(medFontCJK);
    TTF_CloseFont(smFontCJK);
    TTF_Quit();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return (state.won && state.decided) ? 0 : 1;
}
