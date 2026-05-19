#include <SDL.h>
#include <SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>
#include "game_data.h"

// ── Layout ────────────────────────────────────────────────────────────────────
static constexpr int WIN_W    = 900;
static constexpr int WIN_H    = 580;
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

// Decision-pipe gap centres: midpoint of the A-zone and B-zone respectively.
static constexpr float DPIPE_GA = (GAME_TOP + GATE_MID) / 2.f;
static constexpr float DPIPE_GB = (GATE_MID + WIN_H)    / 2.f;

// ── Colour palette ────────────────────────────────────────────────────────────
static const SDL_Color C_BG        = {13,  17,  23,  255};
static const SDL_Color C_QBAR      = {22,  27,  34,  255};
static const SDL_Color C_BORDER    = {48,  54,  61,  255};
static const SDL_Color C_TEXT      = {230, 237, 243, 255};
static const SDL_Color C_MUTED     = {125, 133, 144, 255};
static const SDL_Color C_PIPE      = {35,  134, 54,  255};
static const SDL_Color C_PIPE_CAP  = {46,  160, 67,  255};
static const SDL_Color C_BIRD      = {247, 201, 72,  255};
static const SDL_Color C_BIRD_RING = {180, 130, 30,  255};
static const SDL_Color C_WHITE     = {255, 255, 255, 255};
static const SDL_Color C_OK        = {46,  160, 67,  255};
static const SDL_Color C_ERR       = {248, 81,  73,  255};
static const SDL_Color C_OVERLAY   = {0,   0,   0,   210};
static const SDL_Color C_A_BG      = {31,  111, 235, 50};
static const SDL_Color C_B_BG      = {110, 64,  201, 50};
static const SDL_Color C_A_TEXT    = {88,  166, 255, 255};
static const SDL_Color C_B_TEXT    = {163, 113, 247, 255};
static const SDL_Color C_A_LABEL   = {31,  111, 235, 255};
static const SDL_Color C_B_LABEL   = {110, 64,  201, 255};

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

// ── Text helpers ──────────────────────────────────────────────────────────────
static TTF_Font* loadFont(int size) {
    const char* paths[] = {
        "/System/Library/Fonts/Helvetica.ttc",
        "/Library/Fonts/Arial.ttf",
        "/System/Library/Fonts/SFNSDisplay.ttf",
        "/System/Library/Fonts/SFCompact.ttf",
        nullptr
    };
    for (int i = 0; paths[i]; ++i) {
        TTF_Font* f = TTF_OpenFont(paths[i], size);
        if (f) return f;
    }
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
struct Bird {
    float x = 130.f;
    float y = 0.f;
    float vy = 0.f;
};

struct Pipe {
    float x;
    float gapY;
    float gapY_B     = 0.f;
    bool  scored     = false;
    bool  isDecision = false;
};

// ── State ─────────────────────────────────────────────────────────────────────
enum class Phase { Playing, Result, Complete };

struct State {
    Bird              bird;
    std::vector<Pipe> pipes;
    Phase             phase       = Phase::Playing;
    bool              alive       = true;
    bool              decided     = false;
    bool              choseA      = false;
    bool              won         = false;
    bool              paused      = false;
    int               score       = 0;
    int               frame       = 0;
    int               qIdx        = 0;
    int               qTotal      = 1;
    int               flashTimer  = 0;  // "✓ Correct!" badge countdown after seamless advance
};

static float randGapY() {
    float mn = GAME_TOP + PIPE_GAP / 2.f + 35.f;
    float mx = WIN_H    - PIPE_GAP / 2.f - 35.f;
    return mn + (float)std::rand() / RAND_MAX * (mx - mn);
}

static void resetPipes(State& s) {
    s.pipes = {
        {WIN_W + 50.f,  randGapY(), 0.f,      false, false},
        {WIN_W + 330.f, randGapY(), 0.f,      false, false},
        {WIN_W + 620.f, DPIPE_GA,   DPIPE_GB, false, true }
    };
}

static void resetState(State& s, int qIdx, int qTotal) {
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
    s.qIdx       = qIdx;
    s.qTotal     = qTotal;
}

// Advance to next question while keeping the bird in the air.
static void advanceKeepBird(State& s) {
    int nextIdx = s.qIdx + 1;
    if (nextIdx >= s.qTotal) {
        s.phase = Phase::Complete;
        return;
    }
    Bird saved   = s.bird;
    int  qTotal  = s.qTotal;   // preserve (resetState would clobber via param)
    resetState(s, nextIdx, qTotal);
    s.bird       = saved;
    s.flashTimer = 50;
}

// ── Pipe drawing ──────────────────────────────────────────────────────────────
static void drawPipe(SDL_Renderer* r, const Pipe& p) {
    int px   = (int)p.x;
    int topH = (int)(p.gapY - PIPE_GAP / 2.f) - GAME_TOP;
    int botY = (int)(p.gapY + PIPE_GAP / 2.f);

    // Top pipe
    fillRect(r, px, GAME_TOP, (int)PIPE_W, topH,          C_PIPE);
    fillRect(r, px - 5, GAME_TOP + topH - 20, (int)PIPE_W + 10, 20, C_PIPE_CAP);

    // Bottom pipe
    fillRect(r, px - 5, botY, (int)PIPE_W + 10, 20, C_PIPE_CAP);
    fillRect(r, px, botY + 20, (int)PIPE_W, WIN_H - botY - 20, C_PIPE);

    // Subtle highlight strip on left edge
    SDL_SetRenderDrawColor(r, 70, 180, 100, 80);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_RenderDrawLine(r, px + 4, GAME_TOP, px + 4, GAME_TOP + topH - 20);
    SDL_RenderDrawLine(r, px + 4, botY + 20, px + 4, WIN_H);
}

// ── Decision-pipe drawing (two gaps: A=top, B=bottom) ─────────────────────────
static void drawDecisionPipe(SDL_Renderer* r, const Pipe& p) {
    int px = (int)p.x;
    int pw = (int)PIPE_W;
    constexpr float hg = DPIPE_GAP / 2.f;

    int gA_top = (int)(p.gapY   - hg);
    int gA_bot = (int)(p.gapY   + hg);
    int gB_top = (int)(p.gapY_B - hg);
    int gB_bot = (int)(p.gapY_B + hg);

    // Top solid section
    fillRect(r, px, GAME_TOP, pw, gA_top - GAME_TOP, C_PIPE);
    fillRect(r, px-5, gA_top - 16, pw+10, 16, C_PIPE_CAP);

    // Gap A — blue tint so the player knows which opening is A
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 31, 111, 235, 80);
    { SDL_Rect rc{px, gA_top, pw, (int)DPIPE_GAP}; SDL_RenderFillRect(r, &rc); }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // Middle solid section (two caps + body)
    fillRect(r, px-5, gA_bot, pw+10, 16, C_PIPE_CAP);
    int midH = gB_top - 16 - (gA_bot + 16);
    if (midH > 0) fillRect(r, px, gA_bot + 16, pw, midH, C_PIPE);
    fillRect(r, px-5, gB_top - 16, pw+10, 16, C_PIPE_CAP);

    // Gap B — purple tint
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 110, 64, 201, 80);
    { SDL_Rect rc{px, gB_top, pw, (int)DPIPE_GAP}; SDL_RenderFillRect(r, &rc); }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // Bottom solid section
    fillRect(r, px-5, gB_bot, pw+10, 16, C_PIPE_CAP);
    fillRect(r, px, gB_bot + 16, pw, WIN_H - gB_bot - 16, C_PIPE);

    // Left-edge highlight
    SDL_SetRenderDrawColor(r, 70, 180, 100, 80);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_RenderDrawLine(r, px+4, GAME_TOP,   px+4, gA_top - 16);
    SDL_RenderDrawLine(r, px+4, gA_bot + 16, px+4, gB_top - 16);
    SDL_RenderDrawLine(r, px+4, gB_bot + 16, px+4, WIN_H);
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
                              const GameData& d) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    // Zone A background (top half)
    fillRect(r, GATE_X, GAME_TOP, WIN_W - GATE_X, GAME_H / 2, C_A_BG);
    // Zone B background (bottom half)
    fillRect(r, GATE_X, GATE_MID, WIN_W - GATE_X, GAME_H - GAME_H / 2, C_B_BG);

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
static void drawQBar(SDL_Renderer* r, TTF_Font* smFont, const GameData& d) {
    fillRect(r, 0, 0, WIN_W, QBAR_H, C_QBAR);
    drawHLine(r, 0, WIN_W, QBAR_H - 1, C_BORDER);

    drawText(r, smFont, "Q", 14, 10, C_MUTED);
    drawWrapped(r, smFont, d.question, 34, 10, GATE_X - 50, QBAR_H - 16, C_TEXT);
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
        drawTextCX(r, lgFont,  "All done!",
                   cx, mid - 70, C_OK);
        drawTextCX(r, medFont, "You answered all " + std::to_string(s.qTotal) + " questions.",
                   cx, mid - 16, C_TEXT);
        drawTextCX(r, medFont, "Space or click to play again",
                   cx, mid + 50, C_MUTED);
        drawTextCX(r, smFont,  "Q to quit",
                   cx, mid + 82, C_MUTED);
        return;
    }

    // Wrong answer or crash — show what the correct answer was.
    drawTextCX(r, lgFont,  "Wrong!",                 cx, mid - 90, C_ERR);
    drawTextCX(r, smFont,  "The correct answer was:", cx, mid - 40, C_MUTED);
    std::string corr = d.correctIsA ? ("A: " + d.choiceA) : ("B: " + d.choiceB);
    drawWrapped(r, medFont, corr, cx - 280, mid - 18, 560, 70, C_TEXT);
    drawTextCX(r, medFont, "Space or click to try again", cx, mid + 60, C_MUTED);
    drawTextCX(r, smFont,  "Q to quit",               cx, mid + 92, C_MUTED);
}

// ── Entry point ───────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (argc < 2) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "test-taker-game",
                                 "Usage: test-taker-game <data-file>", nullptr);
        return 1;
    }
    std::vector<GameData> questions = ReadGameFiles(argv[1]);
    if (questions.empty() || questions[0].question.empty()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "test-taker-game",
                                 "Could not read game data file.", nullptr);
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0 || TTF_Init() < 0) return 1;

    SDL_Window* win = SDL_CreateWindow(
        "Quiz Challenge — Flappy Bird",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) { SDL_Quit(); return 1; }

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) { SDL_DestroyWindow(win); SDL_Quit(); return 1; }

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    TTF_Font* lgFont  = loadFont(27);
    TTF_Font* medFont = loadFont(16);
    TTF_Font* smFont  = loadFont(13);

    std::srand((unsigned)SDL_GetTicks());
    const std::string dataFilePath = argv[1];
    State state;
    resetState(state, 0, (int)questions.size());

    bool quit   = false;
    bool allWon = false;
    while (!quit) {
        const GameData& data = questions[std::min(state.qIdx, state.qTotal - 1)];

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
                    } else if (!state.paused && (sym == SDLK_SPACE || sym == SDLK_UP)) {
                        if (state.phase == Phase::Playing && state.alive && !state.decided)
                            state.bird.vy = FLAP;
                        else if (state.phase == Phase::Result && !state.won)
                            resetState(state, state.qIdx, state.qTotal);
                        else if (state.phase == Phase::Complete) {
                            allWon = true;
                            resetState(state, 0, state.qTotal);
                        }
                    }
                    break;
                }
                case SDL_MOUSEBUTTONDOWN:
                    if (!state.paused && ev.button.button == SDL_BUTTON_LEFT) {
                        if (state.phase == Phase::Playing && state.alive && !state.decided)
                            state.bird.vy = FLAP;
                        else if (state.phase == Phase::Result && !state.won)
                            resetState(state, state.qIdx, state.qTotal);
                        else if (state.phase == Phase::Complete) {
                            allWon = true;
                            resetState(state, 0, state.qTotal);
                        }
                    }
                    break;
                default: break;
            }
        }

        // ── Poll file for new questions every ~2 s ────────────────────────────
        if (state.frame % 120 == 0 && state.frame > 0) {
            auto fresh = ReadGameFiles(dataFilePath);
            if ((int)fresh.size() > (int)questions.size()) {
                questions = std::move(fresh);
                state.qTotal = (int)questions.size();
                // If we were on the Complete screen, the new questions just arrived:
                // restart playing automatically.
                if (state.phase == Phase::Complete)
                    resetState(state, 0, state.qTotal);
            }
        }

        // ── Physics update ────────────────────────────────────────────────────
        if (!state.paused && state.phase == Phase::Playing && state.alive && !state.decided) {
            state.bird.vy += GRAVITY;
            state.bird.y  += state.bird.vy;

            if (state.bird.y - BIRD_R < GAME_TOP || state.bird.y + BIRD_R > WIN_H) {
                state.alive = false;
                state.phase = Phase::Result;
            }

            for (auto& p : state.pipes) {
                p.x -= SPEED;
                bool inX = state.bird.x + BIRD_R > p.x &&
                           state.bird.x - BIRD_R < p.x + PIPE_W;
                if (p.isDecision) {
                    constexpr float hg = DPIPE_GAP / 2.f;
                    if (inX) {
                        bool inGapA = state.bird.y - BIRD_R >= p.gapY   - hg &&
                                      state.bird.y + BIRD_R <= p.gapY   + hg;
                        bool inGapB = state.bird.y - BIRD_R >= p.gapY_B - hg &&
                                      state.bird.y + BIRD_R <= p.gapY_B + hg;
                        if (!inGapA && !inGapB) {
                            state.alive = false;
                            state.phase = Phase::Result;
                        }
                    }
                    if (!p.scored && state.bird.x > p.x + PIPE_W) {
                        p.scored      = true;
                        state.choseA  = (state.bird.y >= p.gapY - hg &&
                                         state.bird.y <= p.gapY + hg);
                        state.decided = true;
                        state.won     = (state.choseA == data.correctIsA);
                        if (state.won)
                            advanceKeepBird(state);   // zero interruption on correct
                        else
                            state.phase = Phase::Result;
                    }
                } else {
                    if (inX) {
                        bool inGap = state.bird.y - BIRD_R >= p.gapY - PIPE_GAP / 2.f &&
                                     state.bird.y + BIRD_R <= p.gapY + PIPE_GAP / 2.f;
                        if (!inGap) {
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

        // ── Render ────────────────────────────────────────────────────────────
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        fillRect(ren, 0, 0, WIN_W, WIN_H, C_BG);

        drawDecisionZone(ren, medFont, smFont, data);

        for (const auto& p : state.pipes) {
            if (p.isDecision) drawDecisionPipe(ren, p);
            else              drawPipe(ren, p);
        }

        if (state.alive)
            drawBird(ren, state.bird, state.frame);

        drawQBar(ren, smFont, data);

        // "Tap to flap" hint (fades out)
        if (state.frame < 140 && state.phase == Phase::Playing) {
            Uint8 alpha = (Uint8)std::max(0, 190 - state.frame * 2);
            drawTextCX(ren, smFont, "Click  or  Space  to  flap",
                       WIN_W / 2 - 40, WIN_H - 26, {255, 255, 255, alpha});
        }

        if (state.phase == Phase::Result || state.phase == Phase::Complete)
            drawResult(ren, lgFont, medFont, smFont, state, data);

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
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 120);
            SDL_Rect fullGame{0, GAME_TOP, WIN_W, GAME_H};
            SDL_RenderFillRect(ren, &fullGame);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

            int cx  = WIN_W / 2;
            int mid = GAME_TOP + GAME_H / 2;
            drawTextCX(ren, lgFont,  "Paused",         cx, mid - 52, C_TEXT);
            drawTextCX(ren, medFont, "ESC to resume",  cx, mid +  8, C_MUTED);
            drawTextCX(ren, smFont,  "Q to quit",      cx, mid + 38, C_MUTED);

            // Debug: bird position
            std::string dbg = "bird y=" + std::to_string((int)state.bird.y)
                            + "  vy=" + std::to_string((int)(state.bird.vy * 100) / 100)
                            + "  frame=" + std::to_string(state.frame)
                            + "  q=" + std::to_string(state.qIdx + 1) + "/" + std::to_string(state.qTotal);
            drawText(ren, smFont, dbg, 10, WIN_H - 20, C_MUTED);
        }

        SDL_RenderPresent(ren);
        ++state.frame;
    }
    (void)allWon;

    TTF_CloseFont(lgFont);
    TTF_CloseFont(medFont);
    TTF_CloseFont(smFont);
    TTF_Quit();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return (state.won && state.decided) ? 0 : 1;
}
