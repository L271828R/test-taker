// test-taker-asteroids — quiz asteroids game
// Separate binary; same data-file / IPC protocol as test-taker-game (flappy bird).
// Build: cmake target test-taker-asteroids
// Usage: test-taker-asteroids <data-file>

#include <SDL.h>
#include <SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "game_data.h"

// ── Logger ────────────────────────────────────────────────────────────────────
static FILE* gLog = nullptr;
static void logOpen()  { gLog = fopen("/tmp/tt-asteroids.log", "w"); }
static void logClose() { if (gLog) { fclose(gLog); gLog = nullptr; } }
static void logf(const char* fmt, ...) {
    if (!gLog) return;
    fprintf(gLog, "[%6u] ", SDL_GetTicks());
    va_list ap; va_start(ap, fmt); vfprintf(gLog, fmt, ap); va_end(ap);
    fputc('\n', gLog); fflush(gLog);
}

// ── Audio (square-wave chiptune + synthesised drums) ─────────────────────────
static constexpr int AUDIO_FREQ = 44100;

// 5 space-themed melody patterns (MIDI, 0=rest), 16 quarter-note steps
static const int THEME_NOTES[5][16] = {
    {57, 0,60,62, 64, 0,62,57,  59, 0,60,64, 62,60,59,57},  // Drift:  A minor
    {62, 0,65,67, 69,67,65, 0,  64, 0,65,69, 67,65,64,62},  // Pulse:  D minor
    {64,67, 0,69, 71, 0,74,71,  69,67, 0,64, 67,69,67, 0},  // Chase:  E minor
    {67, 0,70, 0, 72,70,67, 0,  65, 0,67,70, 72,70,67,65},  // Surge:  G minor
    {60, 0,63,67, 70,67,63, 0,  65, 0,67,72, 70,67,65,60},  // Blaze:  C minor
};
static const float THEME_BPM[5] = {90.f, 100.f, 115.f, 108.f, 120.f};

static const int DRUM_KICK[5][16] = {
    {1,0,0,0, 0,0,0,0, 1,0,0,0, 0,0,0,0},
    {1,0,0,0, 0,0,1,0, 1,0,0,0, 0,0,0,0},
    {1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,0},
    {1,0,0,0, 0,0,1,0, 1,0,0,0, 0,1,0,0},
    {1,0,1,0, 0,0,1,0, 1,0,0,0, 1,0,1,0},
};
static const int DRUM_SNARE[5][16] = {
    {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0},
    {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0},
    {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0},
    {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,1},
    {0,0,0,0, 1,0,1,0, 0,0,0,0, 1,0,0,0},
};
static const int DRUM_HAT[5][16] = {
    {1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0},
    {1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1},
    {1,0,1,1, 1,0,1,0, 1,0,1,1, 1,0,1,0},
    {1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0},
    {1,1,0,1, 1,1,0,1, 1,1,0,1, 1,1,0,1},
};

struct AudioCtx {
    SDL_AudioDeviceID devId = 0;
    int   theme = 0, noteIdx = 0, sampLeft = 0;
    float phase = 0.f;
    int   drumStep = 0, drumSampLeft = 0;
    float kickAmp = 0.f, kickFreq = 80.f, kickPhase = 0.f;
    float snareAmp = 0.f, hatAmp = 0.f;
    Uint32 rng = 0xBEEF5678u;
};
static AudioCtx gAudio;
static bool     gMuted = false;

static void audioCallback(void*, Uint8* stream, int len) {
    Sint16* buf = (Sint16*)stream;
    int     n   = len / 2;
    float   bpm = THEME_BPM[gAudio.theme];
    for (int i = 0; i < n; ++i) {
        if (gAudio.sampLeft <= 0) {
            gAudio.noteIdx  = (gAudio.noteIdx + 1) % 16;
            gAudio.sampLeft = (int)(AUDIO_FREQ * 60.f / bpm);
            gAudio.phase    = 0.f;
        }
        float melodyF = 0.f;
        int note = THEME_NOTES[gAudio.theme][gAudio.noteIdx];
        if (note > 0) {
            float freq = 440.f * std::pow(2.f, (note - 69) / 12.f);
            melodyF = (gAudio.phase < 0.5f) ? 1.f : -1.f;
            gAudio.phase += freq / AUDIO_FREQ;
            if (gAudio.phase >= 1.f) gAudio.phase -= 1.f;
        }
        --gAudio.sampLeft;

        if (gAudio.drumSampLeft <= 0) {
            gAudio.drumSampLeft = (int)(AUDIO_FREQ * 60.f / (bpm * 4.f));
            gAudio.drumStep     = (gAudio.drumStep + 1) % 16;
            int t = gAudio.theme;
            if (DRUM_KICK [t][gAudio.drumStep]) { gAudio.kickAmp = 1.f; gAudio.kickFreq = 150.f; gAudio.kickPhase = 0.f; }
            if (DRUM_SNARE[t][gAudio.drumStep]) gAudio.snareAmp = 1.f;
            if (DRUM_HAT  [t][gAudio.drumStep]) gAudio.hatAmp   = 1.f;
        }
        --gAudio.drumSampLeft;

        float kickF = 0.f, snareF = 0.f, hatF = 0.f;
        if (gAudio.kickAmp > 0.001f) {
            kickF = gAudio.kickAmp * std::sin(gAudio.kickPhase * 6.28318f);
            gAudio.kickPhase += gAudio.kickFreq / AUDIO_FREQ;
            if (gAudio.kickPhase >= 1.f) gAudio.kickPhase -= 1.f;
            gAudio.kickFreq  *= 0.9999f;
            gAudio.kickAmp   *= 0.9993f;
        }
        if (gAudio.snareAmp > 0.001f) {
            gAudio.rng = gAudio.rng * 1664525u + 1013904223u;
            snareF = gAudio.snareAmp * ((float)(int)(gAudio.rng >> 1) / (float)0x40000000 - 1.f);
            gAudio.snareAmp *= 0.9985f;
        }
        if (gAudio.hatAmp > 0.001f) {
            gAudio.rng = gAudio.rng * 1664525u + 1013904223u;
            hatF = gAudio.hatAmp * ((float)(int)(gAudio.rng >> 1) / (float)0x40000000 - 1.f);
            gAudio.hatAmp *= 0.984f;
        }

        float mix = melodyF * 3000.f + kickF * 5500.f + snareF * 2500.f + hatF * 900.f;
        buf[i] = (Sint16)std::max(-30000.f, std::min(30000.f, mix));
    }
}

static void initAudio() {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) { logf("AUDIO init failed"); return; }
    SDL_AudioSpec want{}, got{};
    want.freq = AUDIO_FREQ; want.format = AUDIO_S16SYS;
    want.channels = 1; want.samples = 512; want.callback = audioCallback;
    gAudio.devId = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
    if (!gAudio.devId) { logf("AUDIO open failed: %s", SDL_GetError()); return; }
    SDL_PauseAudioDevice(gAudio.devId, 0);
    logf("AUDIO started freq=%d", got.freq);
}
static void closeAudio() {
    if (gAudio.devId) { SDL_CloseAudioDevice(gAudio.devId); gAudio.devId = 0; }
}
static void setAudioTheme(int t) {
    if (!gAudio.devId) return;
    SDL_LockAudioDevice(gAudio.devId);
    gAudio.theme = t; gAudio.noteIdx = 0; gAudio.sampLeft = 0; gAudio.phase = 0.f;
    gAudio.drumStep = 0; gAudio.drumSampLeft = 0;
    gAudio.kickAmp = 0.f; gAudio.snareAmp = 0.f; gAudio.hatAmp = 0.f;
    SDL_UnlockAudioDevice(gAudio.devId);
}

// ── Theme shuffle queue ───────────────────────────────────────────────────────
static int gThemeQueue[5] = {0,1,2,3,4};
static int gThemePos      = 0;
static int gTheme         = 0;

static void shuffleThemes(int excluding) {
    // Fisher-Yates on the queue, then ensure first != excluding
    for (int i = 4; i > 0; --i) {
        int j = std::rand() % (i + 1);
        std::swap(gThemeQueue[i], gThemeQueue[j]);
    }
    if (excluding >= 0 && gThemeQueue[0] == excluding && 5 > 1)
        std::swap(gThemeQueue[0], gThemeQueue[1]);
}
static void nextTheme() {
    ++gThemePos;
    if (gThemePos >= 5) { shuffleThemes(gTheme); gThemePos = 0; }
    gTheme = gThemeQueue[gThemePos];
    setAudioTheme(gTheme);
}

// ── Font ─────────────────────────────────────────────────────────────────────
static TTF_Font* loadFont(int size) {
    const char* paths[] = {
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/STHeiti Medium.ttc",
        "/System/Library/Fonts/Helvetica.ttc",
        "/Library/Fonts/Arial.ttf",
        "/System/Library/Fonts/SFNSDisplay.ttf",
        nullptr
    };
    for (int i = 0; paths[i]; ++i) {
        TTF_Font* f = TTF_OpenFont(paths[i], size);
        if (f) { logf("font sz=%d path=%s", size, paths[i]); return f; }
    }
    logf("WARN: no font sz=%d", size);
    return nullptr;
}

// ── Layout constants ──────────────────────────────────────────────────────────
static constexpr int WIN_W   = 1100;
static constexpr int WIN_H   = 700;
static constexpr int QBAR_H  = 120;  // question bar height
static constexpr int GAME_T  = QBAR_H;
static constexpr int GAME_H  = WIN_H - QBAR_H;
static constexpr int GAME_CX = WIN_W / 2;
static constexpr int GAME_CY = GAME_T + GAME_H / 2;

// ── Colours ───────────────────────────────────────────────────────────────────
static constexpr SDL_Color C_BG     = {  8,  8, 18,255};
static constexpr SDL_Color C_QBAR   = { 18, 18, 32,255};
static constexpr SDL_Color C_TEXT   = {220,220,235,255};
static constexpr SDL_Color C_MUTED  = {120,120,140,255};
static constexpr SDL_Color C_BORDER = { 50, 50, 70,255};
static constexpr SDL_Color C_OK     = { 80,220, 80,255};
static constexpr SDL_Color C_ERR    = {220, 80, 80,255};
static constexpr SDL_Color C_GOLD   = {240,200, 50,255};
static constexpr SDL_Color C_AMBER  = {220,160, 30,255};
static constexpr SDL_Color C_A      = { 80,160,240,255};  // choice A  — blue
static constexpr SDL_Color C_B      = {240,140, 60,255};  // choice B  — orange

// ── Draw helpers ──────────────────────────────────────────────────────────────
static void fillRect(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_Rect rc{x,y,w,h}; SDL_RenderFillRect(r, &rc);
}
static void drawText(SDL_Renderer* r, TTF_Font* f, const std::string& t,
                     int x, int y, SDL_Color c) {
    if (!f || t.empty()) return;
    SDL_Surface* s = TTF_RenderUTF8_Blended(f, t.c_str(), c);
    if (!s) return;
    SDL_Texture* tx = SDL_CreateTextureFromSurface(r, s); SDL_FreeSurface(s);
    if (!tx) return;
    int w, h; SDL_QueryTexture(tx, nullptr, nullptr, &w, &h);
    SDL_Rect dst{x,y,w,h}; SDL_RenderCopy(r, tx, nullptr, &dst);
    SDL_DestroyTexture(tx);
}
static void drawTextCX(SDL_Renderer* r, TTF_Font* f, const std::string& t,
                       int cx, int y, SDL_Color c) {
    if (!f || t.empty()) return;
    SDL_Surface* s = TTF_RenderUTF8_Blended(f, t.c_str(), c);
    if (!s) return;
    SDL_Texture* tx = SDL_CreateTextureFromSurface(r, s); SDL_FreeSurface(s);
    if (!tx) return;
    int w, h; SDL_QueryTexture(tx, nullptr, nullptr, &w, &h);
    SDL_Rect dst{cx-w/2, y, w, h}; SDL_RenderCopy(r, tx, nullptr, &dst);
    SDL_DestroyTexture(tx);
}
static void drawWrapped(SDL_Renderer* r, TTF_Font* font, const std::string& text,
                        int x, int y, int maxW, int maxH, SDL_Color c) {
    if (!font) return;
    int lineH = TTF_FontLineSkip(font) + 1;
    int curY  = y;
    std::vector<std::string> paras;
    std::string para;
    for (char ch : text) {
        if (ch == '\n') { paras.push_back(para); para.clear(); }
        else             para += ch;
    }
    paras.push_back(para);
    for (const auto& p : paras) {
        if (curY + lineH > y + maxH) return;
        std::string line, word;
        auto flush = [&](const std::string& w) {
            std::string test = line.empty() ? w : line + " " + w;
            int tw = 0; TTF_SizeUTF8(font, test.c_str(), &tw, nullptr);
            if (tw > maxW && !line.empty()) {
                if (curY + lineH > y + maxH) return;
                drawText(r, font, line, x, curY, c); curY += lineH; line = w;
            } else { line = test; }
        };
        for (char ch : p + " ") {
            if (ch == ' ') { if (!word.empty()) { flush(word); word.clear(); } }
            else word += ch;
        }
        if (!line.empty() && curY + lineH <= y + maxH) {
            drawText(r, font, line, x, curY, c); curY += lineH;
        }
    }
}

// ── Starfield (static, deterministic) ────────────────────────────────────────
struct Star { int x, y, r; Uint8 bright; };
static std::vector<Star> gStars;
static void initStars() {
    Uint32 rng = 0xABCD1234u;
    auto rand1 = [&]() { rng = rng*1664525u+1013904223u; return rng; };
    gStars.resize(200);
    for (auto& s : gStars) {
        s.x      = (int)(rand1() % WIN_W);
        s.y      = (int)(rand1() % GAME_H) + GAME_T;
        s.r      = (int)(rand1() % 2) + 1;
        s.bright = (Uint8)(100 + rand1() % 156);
    }
}
static void drawStars(SDL_Renderer* r) {
    for (const auto& s : gStars) {
        SDL_SetRenderDrawColor(r, s.bright, s.bright, s.bright, 255);
        SDL_Rect rc{s.x, s.y, s.r, s.r}; SDL_RenderFillRect(r, &rc);
    }
}

// ── Asteroid shapes (5 variants, 10 vertices each on unit circle) ─────────────
static constexpr int AST_VERTS = 10;
static float gAstShapes[5][AST_VERTS][2];  // [shape][vert][x,y]

static void initAsteroidShapes() {
    Uint32 rng = 0xDEADBEEFu;
    auto r01 = [&]() { rng = rng*1664525u+1013904223u; return (float)(rng>>8)/(float)(1<<24); };
    for (int s = 0; s < 5; ++s)
        for (int v = 0; v < AST_VERTS; ++v) {
            float ang = (float)v * 6.28318f / AST_VERTS;
            float rad = 0.65f + r01() * 0.35f;
            gAstShapes[s][v][0] = rad * std::cos(ang);
            gAstShapes[s][v][1] = rad * std::sin(ang);
        }
}

static void drawAsteroidPoly(SDL_Renderer* r, float cx, float cy,
                              float radius, float angle, int shape, SDL_Color c) {
    SDL_Point pts[AST_VERTS + 1];
    float ca = std::cos(angle), sa = std::sin(angle);
    for (int i = 0; i < AST_VERTS; ++i) {
        float vx = gAstShapes[shape][i][0], vy = gAstShapes[shape][i][1];
        pts[i].x = (int)(cx + (vx*ca - vy*sa) * radius);
        pts[i].y = (int)(cy + (vx*sa + vy*ca) * radius);
    }
    pts[AST_VERTS] = pts[0];
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLines(r, pts, AST_VERTS + 1);
}

// ── Ship drawing ──────────────────────────────────────────────────────────────
// angle=0 → pointing up; increases clockwise.
// nose_dir = (sin(a), -cos(a)) in screen space.
static void drawShip(SDL_Renderer* r, float cx, float cy, float angle,
                     bool thrusting, int frame) {
    // Forward = (sin(a), -cos(a)), right = (cos(a), sin(a))
    float sn = std::sin(angle), cs = std::cos(angle);
    auto pt = [&](float fwd, float side) -> SDL_Point {
        return { (int)(cx + side*cs + fwd*sn),
                 (int)(cy + side*sn - fwd*cs) };
    };
    SDL_Point ship[4] = { pt(16,0), pt(-10,-10), pt(-5,0), pt(-10,10) };
    ship[3] = ship[0]; // not used as last — draw 3 lines manually
    SDL_SetRenderDrawColor(r, 200, 220, 255, 255);
    SDL_RenderDrawLine(r, ship[0].x, ship[0].y, ship[1].x, ship[1].y);
    SDL_RenderDrawLine(r, ship[1].x, ship[1].y, ship[2].x, ship[2].y);
    SDL_RenderDrawLine(r, ship[2].x, ship[2].y, ship[3 - 1].x, ship[3 - 1].y);  // left wing to notch
    // close: right wing
    SDL_Point rw = pt(-10, 10);
    SDL_RenderDrawLine(r, ship[0].x, ship[0].y, rw.x, rw.y);
    SDL_RenderDrawLine(r, rw.x, rw.y, ship[2].x, ship[2].y);

    // Thrust flame
    if (thrusting && (frame % 6) < 4) {
        float flen = 10.f + (frame % 3) * 3.f;
        SDL_Point base_l = pt(-5, -6), base_r = pt(-5, 6);
        SDL_Point tip    = pt(-5 - flen, 0);
        SDL_SetRenderDrawColor(r, 255, 140, 30, 200);
        SDL_RenderDrawLine(r, base_l.x, base_l.y, tip.x, tip.y);
        SDL_RenderDrawLine(r, base_r.x, base_r.y, tip.x, tip.y);
    }
}

// ── Game state ────────────────────────────────────────────────────────────────
struct Vec2 { float x = 0, y = 0; };

struct Ship {
    Vec2  pos = {(float)GAME_CX, (float)GAME_CY};
    Vec2  vel;
    float angle = 0.f;     // radians, 0=up, clockwise
    bool  thrusting = false;
    int   invincible = 0;  // frames of invincibility remaining
};

struct Asteroid {
    Vec2  pos, vel;
    float angle = 0.f, spin = 0.f;
    float radius = 55.f;
    int   shape  = 0;
    bool  correct = false;   // carries the correct answer?
    bool  alive   = true;
};

struct Bullet {
    Vec2 pos, vel;
    int  life = 0;
    bool alive = false;
};

struct Particle {
    Vec2  pos, vel;
    int   life = 0;
    Uint8 r, g, b;
};

enum class Phase { Playing, WrongFlash, CorrectFlash, GameOver, Complete };

enum class HintState { None, Waiting, Shown };

struct State {
    Ship                  ship;
    std::vector<Asteroid> asteroids;
    std::vector<Bullet>   bullets;
    std::vector<Particle> particles;
    Phase     phase     = Phase::Playing;
    HintState hintState = HintState::None;
    std::string hintText;
    bool  paused     = false;
    int   lives      = 3;
    int   score      = 0;
    int   qIdx       = 0;
    int   qTotal     = 1;
    int   frame      = 0;
    int   flashTimer = 0;    // counts down for WrongFlash / CorrectFlash
    int   shootCool  = 0;    // frames until next shot allowed
    int   rotDir     = 0;    // -1 left, 0 none, +1 right
    bool  thrustHeld = false;
    bool  wantSent   = false;
    bool  gameSaved  = false;
};

// ── Random helpers ────────────────────────────────────────────────────────────
static float randf(float lo, float hi) {
    return lo + (hi - lo) * (float)std::rand() / (float)RAND_MAX;
}

// ── Asteroid spawning ─────────────────────────────────────────────────────────
static Asteroid makeAsteroid(bool correct) {
    Asteroid a;
    a.correct = correct;
    a.shape   = std::rand() % 5;
    a.radius  = randf(45.f, 70.f);
    a.spin    = randf(0.008f, 0.035f) * (std::rand() % 2 ? 1.f : -1.f);

    // Start from a random edge of the game area
    int edge = std::rand() % 4;
    switch (edge) {
        case 0: a.pos = {randf(0,WIN_W), (float)GAME_T};            break; // top
        case 1: a.pos = {randf(0,WIN_W), (float)WIN_H};             break; // bottom
        case 2: a.pos = {0,             randf(GAME_T, WIN_H)};      break; // left
        default:a.pos = {(float)WIN_W,  randf(GAME_T, WIN_H)};      break; // right
    }
    // Drift toward center with some random offset
    float dx = GAME_CX - a.pos.x + randf(-150,150);
    float dy = GAME_CY - a.pos.y + randf(-150,150);
    float len = std::sqrt(dx*dx + dy*dy);
    if (len < 1.f) len = 1.f;
    float spd = randf(0.8f, 2.2f);
    a.vel = {dx/len*spd, dy/len*spd};
    return a;
}

static void spawnQuestion(State& s) {
    s.asteroids.clear();
    s.bullets.clear();
    s.particles.clear();
    s.hintState = HintState::None;
    s.hintText.clear();
    s.gameSaved = false;
    // 3 correct + 3 wrong
    for (int i = 0; i < 3; ++i) s.asteroids.push_back(makeAsteroid(true));
    for (int i = 0; i < 3; ++i) s.asteroids.push_back(makeAsteroid(false));
}

static void spawnWrongExtras(State& s) {
    for (int i = 0; i < 2; ++i) s.asteroids.push_back(makeAsteroid(false));
}

// ── Wrap position to game area ────────────────────────────────────────────────
static void wrapPos(Vec2& p) {
    if (p.x < 0)      p.x += WIN_W;
    if (p.x >= WIN_W) p.x -= WIN_W;
    if (p.y < GAME_T) p.y += GAME_H;
    if (p.y >= WIN_H) p.y -= GAME_H;
}

// ── Explosion particles ───────────────────────────────────────────────────────
static void explode(State& s, Vec2 pos, SDL_Color c) {
    for (int i = 0; i < 20; ++i) {
        float ang = randf(0.f, 6.28318f);
        float spd = randf(1.f, 4.f);
        Particle p;
        p.pos  = pos;
        p.vel  = {std::cos(ang)*spd, std::sin(ang)*spd};
        p.life = 35 + std::rand() % 20;
        p.r = c.r; p.g = c.g; p.b = c.b;
        s.particles.push_back(p);
    }
}

// ── Question bar ──────────────────────────────────────────────────────────────
static void drawQBar(SDL_Renderer* r, TTF_Font* sm, TTF_Font* med,
                     const GameData& d, int lives, int score, int remaining) {
    fillRect(r, 0, 0, WIN_W, QBAR_H, C_QBAR);
    SDL_SetRenderDrawColor(r, C_BORDER.r, C_BORDER.g, C_BORDER.b, 255);
    SDL_RenderDrawLine(r, 0, QBAR_H-1, WIN_W, QBAR_H-1);

    // Question — left portion
    drawText(r, sm, "Q", 10, 8, C_MUTED);
    drawWrapped(r, sm, d.question, 28, 8, 620, 60, C_TEXT);

    // Divider
    SDL_SetRenderDrawColor(r, C_BORDER.r, C_BORDER.g, C_BORDER.b, 255);
    SDL_RenderDrawLine(r, 660, 6, 660, QBAR_H-6);

    // Choice A (blue)
    drawText(r, sm, "A", 672, 8, C_A);
    drawWrapped(r, sm, d.choiceA, 690, 8, 390, 48, C_TEXT);

    // Choice B (orange)
    drawText(r, sm, "B", 672, 66, C_B);
    drawWrapped(r, sm, d.choiceB, 690, 66, 390, 48, C_TEXT);

    // Lives + score — bottom-left
    std::string lifeStr;
    for (int i = 0; i < lives; ++i) lifeStr += "\xe2\x99\xa5 ";  // ♥
    drawText(r, sm, lifeStr, 10, QBAR_H - 22, C_ERR);
    // Remaining correct rocks counter
    if (remaining > 0) {
        std::string remStr = "\xf0\x9f\x92\xab " + std::to_string(remaining) + " left";
        int rw = 0; TTF_SizeUTF8(sm, remStr.c_str(), &rw, nullptr);
        drawText(r, sm, remStr, WIN_W - rw - 10, 8, C_OK);
    }
    std::string scoreStr = "Score " + std::to_string(score);
    int sw = 0; TTF_SizeUTF8(sm, scoreStr.c_str(), &sw, nullptr);
    drawText(r, sm, scoreStr, WIN_W - sw - 10, QBAR_H - 22, C_GOLD);
}

// ── Save button ───────────────────────────────────────────────────────────────
static SDL_Rect gSaveBtnRect = {0,0,0,0};
static void drawSaveBtn(SDL_Renderer* r, TTF_Font* sm, bool saved, int cx, int y) {
    constexpr int BW = 140, BH = 32;
    gSaveBtnRect = {cx - BW/2, y, BW, BH};
    SDL_Color bg = saved ? SDL_Color{50,120,50,220} : SDL_Color{30,80,160,220};
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    fillRect(r, gSaveBtnRect.x, y, BW, BH, bg);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    const char* lbl = saved ? "\xe2\x9c\x93 Saved" : "\xf0\x9f\x92\xbe Save";
    drawTextCX(r, sm, lbl, cx, y + 8, {230,240,255,255});
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    logOpen();
    logf("=== test-taker-asteroids starting ===");

    if (argc < 2) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "test-taker-asteroids",
                                 "Usage: test-taker-asteroids <data-file>", nullptr);
        return 1;
    }
    const std::string dataFilePath = argv[1];

    std::vector<GameData> questions = ReadGameFiles(dataFilePath);
    logf("questions loaded: %d", (int)questions.size());
    if (questions.empty()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "test-taker-asteroids",
                                 "Could not read game data file.", nullptr);
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0 || TTF_Init() < 0) {
        logf("SDL_Init/TTF_Init failed: %s", SDL_GetError()); return 1;
    }

    SDL_Window* win = SDL_CreateWindow(
        "Quiz Challenge — Asteroids",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) { logf("CreateWindow: %s", SDL_GetError()); return 1; }

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) { logf("CreateRenderer: %s", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    TTF_Font* smFont  = loadFont(13);
    TTF_Font* medFont = loadFont(17);
    TTF_Font* lgFont  = loadFont(28);
    TTF_Font* xlFont  = loadFont(36);  // asteroid labels

    std::srand((unsigned)SDL_GetTicks());
    initAudio();
    shuffleThemes(-1);
    gTheme = gThemeQueue[0];
    setAudioTheme(gTheme);
    initStars();
    initAsteroidShapes();

    State state;
    state.qTotal = (int)questions.size();
    spawnQuestion(state);

    const std::string wantFile = dataFilePath + ".want";
    const std::string saveFile = dataFilePath + ".save";
    const std::string hintReq  = dataFilePath + ".hintreq";
    const std::string hintResp = dataFilePath + ".hintresp";

    bool quit = false;

    while (!quit) {
        // ── Rebind data after any potential question vector reallocation ────────
        const GameData& data = questions[std::min(state.qIdx, state.qTotal - 1)];

        // ── Events ────────────────────────────────────────────────────────────
        state.rotDir     = 0;
        state.thrustHeld = false;

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { quit = true; break; }
            if (ev.type == SDL_KEYDOWN) {
                auto sym = ev.key.keysym.sym;
                if (sym == SDLK_q)      { quit = true; break; }
                if (sym == SDLK_ESCAPE) {
                    state.paused = !state.paused;
                } else if (sym == SDLK_m && state.paused) {
                    gMuted = !gMuted;
                    SDL_PauseAudioDevice(gAudio.devId, gMuted ? 1 : 0);
                } else if (sym == SDLK_h && state.paused
                           && state.phase == Phase::Playing
                           && state.hintState == HintState::None) {
                    std::ofstream hf(hintReq);
                    hf << "Q: " << data.question << "\n"
                       << "A: " << data.choiceA  << "\n"
                       << "B: " << data.choiceB  << "\n";
                    state.hintState = HintState::Waiting;
                    logf("HINT requested q=%d", state.qIdx);
                } else if (sym == SDLK_SPACE && !state.paused
                           && state.phase == Phase::Playing
                           && state.shootCool <= 0) {
                    Bullet b;
                    b.alive = true;
                    b.pos   = state.ship.pos;
                    float sn = std::sin(state.ship.angle);
                    float cs = std::cos(state.ship.angle);
                    // Bullet goes in facing direction (sin(a), -cos(a)) + ship vel
                    b.vel = { state.ship.vel.x + sn * 10.f,
                              state.ship.vel.y - cs * 10.f };
                    b.life = 90;
                    // Add to free slot or push
                    bool placed = false;
                    for (auto& slot : state.bullets) {
                        if (!slot.alive) { slot = b; placed = true; break; }
                    }
                    if (!placed) state.bullets.push_back(b);
                    state.shootCool = 12;
                } else if ((sym == SDLK_SPACE || sym == SDLK_RETURN)
                           && state.phase == Phase::GameOver) {
                    // Retry: reset ship + asteroids, keep qIdx and score
                    state.ship       = Ship{};
                    state.ship.pos   = {(float)GAME_CX, (float)GAME_CY};
                    state.lives      = 3;
                    state.phase      = Phase::Playing;
                    state.gameSaved  = false;
                    spawnQuestion(state);
                }
            }
            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                int mx = ev.button.x, my = ev.button.y;
                bool inSave = gSaveBtnRect.w > 0
                           && mx >= gSaveBtnRect.x && mx < gSaveBtnRect.x + gSaveBtnRect.w
                           && my >= gSaveBtnRect.y && my < gSaveBtnRect.y + gSaveBtnRect.h;
                if (inSave && !state.gameSaved) {
                    std::string corr = data.correctIsA ? data.choiceA : data.choiceB;
                    std::ofstream sf(saveFile);
                    sf << "Q: " << data.question << "\nA: " << corr << "\n";
                    state.gameSaved = true;
                    logf("SAVE q=%d", state.qIdx);
                }
            }
        }

        // ── Held keys ─────────────────────────────────────────────────────────
        {
            const Uint8* ks = SDL_GetKeyboardState(nullptr);
            if (ks[SDL_SCANCODE_LEFT]  || ks[SDL_SCANCODE_A]) state.rotDir     = -1;
            if (ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_D]) state.rotDir     =  1;
            if (ks[SDL_SCANCODE_UP]    || ks[SDL_SCANCODE_W]) state.thrustHeld = true;
        }

        // ── Hint response poll ────────────────────────────────────────────────
        if (state.hintState == HintState::Waiting && state.frame % 30 == 0) {
            std::ifstream rf(hintResp);
            if (rf.good()) {
                std::ostringstream ss; ss << rf.rdbuf(); rf.close();
                state.hintText = ss.str();
                while (!state.hintText.empty() &&
                       (state.hintText.back() == '\n' || state.hintText.back() == ' '))
                    state.hintText.pop_back();
                std::remove(hintResp.c_str());
                state.hintState = HintState::Shown;
                logf("HINT received: %.60s", state.hintText.c_str());
            }
        }

        // ── New question poll (~2 s) ──────────────────────────────────────────
        if (state.frame % 120 == 0 && state.frame > 0) {
            auto fresh = ReadGameFiles(dataFilePath);
            if ((int)fresh.size() > (int)questions.size()) {
                logf("POLL new questions: %d -> %d", (int)questions.size(), (int)fresh.size());
                questions    = std::move(fresh);
                state.qTotal = (int)questions.size();
                if (state.phase == Phase::Complete && state.qIdx < state.qTotal) {
                    state.phase = Phase::Playing;
                    spawnQuestion(state);
                    nextTheme();
                }
            }
        }

        // ── Want signal ───────────────────────────────────────────────────────
        if (!state.wantSent && state.qIdx + 2 >= state.qTotal) {
            std::ofstream wf(wantFile); state.wantSent = true;
            logf("WANT written at q=%d/%d", state.qIdx+1, state.qTotal);
        }

        // ── Physics (skipped when paused) ─────────────────────────────────────
        if (!state.paused && state.phase == Phase::Playing) {
            // Ship rotation & thrust
            state.ship.angle += state.rotDir * 0.052f;
            state.ship.thrusting = state.thrustHeld;
            if (state.thrustHeld) {
                state.ship.vel.x += std::sin(state.ship.angle) * 0.3f;
                state.ship.vel.y -= std::cos(state.ship.angle) * 0.3f;
                float spd = std::sqrt(state.ship.vel.x*state.ship.vel.x +
                                      state.ship.vel.y*state.ship.vel.y);
                if (spd > 8.f) {
                    state.ship.vel.x = state.ship.vel.x / spd * 8.f;
                    state.ship.vel.y = state.ship.vel.y / spd * 8.f;
                }
            }
            state.ship.vel.x *= 0.97f;
            state.ship.vel.y *= 0.97f;
            state.ship.pos.x += state.ship.vel.x;
            state.ship.pos.y += state.ship.vel.y;
            wrapPos(state.ship.pos);
            if (state.ship.invincible > 0) --state.ship.invincible;
            if (state.shootCool > 0) --state.shootCool;

            // Bullets
            for (auto& b : state.bullets) {
                if (!b.alive) continue;
                b.pos.x += b.vel.x; b.pos.y += b.vel.y;
                wrapPos(b.pos);
                if (--b.life <= 0) b.alive = false;
            }

            // Asteroids
            for (auto& a : state.asteroids) {
                if (!a.alive) continue;
                a.pos.x += a.vel.x; a.pos.y += a.vel.y;
                wrapPos(a.pos);
                a.angle += a.spin;
            }

            // Particles
            for (auto& p : state.particles) {
                if (p.life <= 0) continue;
                p.pos.x += p.vel.x; p.pos.y += p.vel.y;
                --p.life;
            }

            // Bullet-asteroid collisions
            for (auto& b : state.bullets) {
                if (!b.alive) continue;
                for (auto& a : state.asteroids) {
                    if (!a.alive) continue;
                    float dx = b.pos.x - a.pos.x, dy = b.pos.y - a.pos.y;
                    if (dx*dx + dy*dy < (a.radius + 3.f)*(a.radius + 3.f)) {
                        b.alive = false;
                        a.alive = false;
                        if (a.correct) {
                            explode(state, a.pos, C_OK);
                            ++state.score;
                            logf("CORRECT hit q=%d score=%d", state.qIdx, state.score);
                            // Check if any correct asteroids remain
                            bool anyLeft = false;
                            for (const auto& o : state.asteroids)
                                if (o.alive && o.correct) { anyLeft = true; break; }
                            if (!anyLeft) {
                                state.phase      = Phase::CorrectFlash;
                                state.flashTimer = 80;
                                logf("ALL CLEARED q=%d", state.qIdx);
                            }
                        } else {
                            // Wrong — spawn more enemies
                            explode(state, a.pos, C_ERR);
                            spawnWrongExtras(state);
                            state.phase      = Phase::WrongFlash;
                            state.flashTimer = 60;
                            logf("WRONG q=%d", state.qIdx);
                        }
                        break;
                    }
                }
                if (!b.alive) break;
            }

            // Ship-asteroid collision (when not invincible)
            if (state.ship.invincible == 0) {
                for (auto& a : state.asteroids) {
                    if (!a.alive) continue;
                    float dx = state.ship.pos.x - a.pos.x;
                    float dy = state.ship.pos.y - a.pos.y;
                    if (dx*dx + dy*dy < (a.radius + 12.f)*(a.radius + 12.f)) {
                        explode(state, state.ship.pos, {200,200,255,255});
                        --state.lives;
                        logf("HIT lives=%d q=%d", state.lives, state.qIdx);
                        if (state.lives <= 0) {
                            state.phase = Phase::GameOver;
                        } else {
                            // Respawn in center with invincibility
                            state.ship.pos        = {(float)GAME_CX, (float)GAME_CY};
                            state.ship.vel         = {};
                            state.ship.invincible  = 120;
                        }
                        break;
                    }
                }
            }
        }

        // ── Flash-phase countdowns ────────────────────────────────────────────
        if ((state.phase == Phase::WrongFlash || state.phase == Phase::CorrectFlash)
                && !state.paused) {
            if (--state.flashTimer <= 0) {
                if (state.phase == Phase::CorrectFlash) {
                    ++state.qIdx;
                    if (state.qIdx >= state.qTotal) {
                        state.phase    = Phase::Complete;
                        state.wantSent = false;
                    } else {
                        state.phase = Phase::Playing;
                        spawnQuestion(state);
                        nextTheme();
                    }
                } else {
                    state.phase = Phase::Playing;
                }
            }
        }

        // ── Render ────────────────────────────────────────────────────────────
        fillRect(ren, 0, GAME_T, WIN_W, GAME_H, C_BG);
        drawStars(ren);

        // Asteroids
        for (const auto& a : state.asteroids) {
            if (!a.alive) continue;
            SDL_Color col = a.correct ? C_OK : C_ERR;
            col.r = (Uint8)(col.r * 3 / 4 + 30);
            col.g = (Uint8)(col.g * 3 / 4 + 30);
            col.b = (Uint8)(col.b * 3 / 4 + 30);
            drawAsteroidPoly(ren, a.pos.x, a.pos.y, a.radius, a.angle, a.shape, col);
            // Label "A" or "B" at center
            SDL_Color lc = a.correct ? (data.correctIsA ? C_A : C_B)
                                     : (data.correctIsA ? C_B : C_A);
            const char* lbl = a.correct ? (data.correctIsA ? "A" : "B")
                                        : (data.correctIsA ? "B" : "A");
            drawTextCX(ren, xlFont, lbl, (int)a.pos.x, (int)a.pos.y - 18, lc);
        }

        // Bullets
        SDL_SetRenderDrawColor(ren, 255, 255, 180, 255);
        for (const auto& b : state.bullets) {
            if (!b.alive) continue;
            SDL_Rect br{(int)b.pos.x-2, (int)b.pos.y-2, 4, 4};
            SDL_RenderFillRect(ren, &br);
        }

        // Particles
        for (const auto& p : state.particles) {
            if (p.life <= 0) continue;
            Uint8 alpha = (Uint8)std::min(255, p.life * 7);
            SDL_SetRenderDrawColor(ren, p.r, p.g, p.b, alpha);
            SDL_Rect pr{(int)p.pos.x-1, (int)p.pos.y-1, 3, 3};
            SDL_RenderFillRect(ren, &pr);
        }

        // Ship (flashes when invincible)
        bool shipVisible = state.ship.invincible == 0 || (state.frame % 8) < 5;
        if (state.phase != Phase::GameOver && shipVisible)
            drawShip(ren, state.ship.pos.x, state.ship.pos.y,
                     state.ship.angle, state.ship.thrusting, state.frame);

        // Question bar (drawn on top so it's always readable)
        int correctLeft = 0;
        for (const auto& a : state.asteroids)
            if (a.alive && a.correct) ++correctLeft;
        drawQBar(ren, smFont, medFont, data, state.lives, state.score, correctLeft);

        // ── Overlays ──────────────────────────────────────────────────────────
        int cx  = WIN_W / 2;
        int mid = GAME_T + GAME_H / 2;

        if (state.phase == Phase::WrongFlash) {
            // Brief wrong-answer indicator
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 180, 20, 20, 60);
            SDL_Rect full{0, GAME_T, WIN_W, GAME_H};
            SDL_RenderFillRect(ren, &full);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
            drawTextCX(ren, lgFont, "Wrong!", cx, mid - 20, C_ERR);
            drawTextCX(ren, smFont, "Two more incoming…", cx, mid + 22, C_MUTED);
        }

        if (state.phase == Phase::CorrectFlash) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 20, 120, 20, 60);
            SDL_Rect full{0, GAME_T, WIN_W, GAME_H};
            SDL_RenderFillRect(ren, &full);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
            drawTextCX(ren, lgFont, "Correct!", cx, mid - 20, C_OK);
        }

        if (state.phase == Phase::GameOver) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 170);
            SDL_Rect full{0, GAME_T, WIN_W, GAME_H};
            SDL_RenderFillRect(ren, &full);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
            drawTextCX(ren, lgFont,  "Game Over",               cx, mid - 60, C_ERR);
            drawTextCX(ren, medFont, "Space or Enter to retry", cx, mid -  8, C_MUTED);
            drawSaveBtn(ren, smFont, state.gameSaved, cx, mid + 34);
        }

        if (state.phase == Phase::Complete) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 170);
            SDL_Rect full{0, GAME_T, WIN_W, GAME_H};
            SDL_RenderFillRect(ren, &full);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
            static const char* dots[] = {".", "..", "..."};
            drawTextCX(ren, medFont,
                       "Loading more questions" + std::string(dots[(SDL_GetTicks()/500)%3]),
                       cx, mid, C_MUTED);
            gSaveBtnRect = {0,0,0,0};
        }

        // ── Pause overlay ─────────────────────────────────────────────────────
        if (state.paused) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 150);
            SDL_Rect full{0, GAME_T, WIN_W, GAME_H};
            SDL_RenderFillRect(ren, &full);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

            drawTextCX(ren, lgFont,  "Paused",        cx, mid - 70, C_TEXT);
            drawTextCX(ren, medFont, "ESC to resume", cx, mid - 14, C_MUTED);

            if (state.phase == Phase::Playing) {
                if (state.hintState == HintState::None) {
                    drawTextCX(ren, smFont, "H  \xe2\x80\x94  hint", cx, mid + 18, C_MUTED);
                } else if (state.hintState == HintState::Waiting) {
                    drawTextCX(ren, smFont, "\xe2\x8f\xb3 fetching hint\xe2\x80\xa6", cx, mid + 18, C_AMBER);
                } else {
                    drawTextCX(ren, smFont, "\xf0\x9f\x92\xa1 Hint:", cx, mid + 18, C_GOLD);
                    drawWrapped(ren, smFont, state.hintText, cx - 260, mid + 40, 520, 110, C_TEXT);
                }
            }

            {
                SDL_Color mc = gMuted ? SDL_Color{220,100,80,255} : C_MUTED;
                drawTextCX(ren, smFont, gMuted ? "M  \xe2\x80\x94  unmute"
                                               : "M  \xe2\x80\x94  mute",
                           cx, mid + 140, mc);
            }
            drawTextCX(ren, smFont, "Q  \xe2\x80\x94  quit", cx, mid + 160, C_MUTED);
        }

        SDL_RenderPresent(ren);
        ++state.frame;

        // Frame-rate cap fallback
        {
            static Uint32 lastTick = 0;
            Uint32 now = SDL_GetTicks();
            if (lastTick == 0) lastTick = now;
            Uint32 elapsed = now - lastTick;
            if (elapsed < 16) SDL_Delay(16 - elapsed);
            lastTick = SDL_GetTicks();
        }
    }

    logf("Quit. score=%d qIdx=%d", state.score, state.qIdx);
    closeAudio();
    if (smFont)  TTF_CloseFont(smFont);
    if (medFont) TTF_CloseFont(medFont);
    if (lgFont)  TTF_CloseFont(lgFont);
    if (xlFont)  TTF_CloseFont(xlFont);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    logClose();
    return 0;
}
