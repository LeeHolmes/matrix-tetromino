// Matrix Tetris Screen Saver
// A Windows screensaver that combines Matrix digital rain with Tetris gameplay.
// - Falling Matrix-style character streams with Tetris pieces at the bottom of each stream
// - Tetris pieces randomly rotate as they fall
// - Pieces occasionally "hard drop" rapidly to the bottom
// - Landed pieces accumulate at the bottom like real Tetris
// - Every 30 seconds, 4 rows clear with an animation

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

#include "resource.h"

// ─── Constants ───────────────────────────────────────────────────────────────

static const wchar_t CLASS_NAME[]  = L"MatrixTetrisScrSaver";
static const int     TIMER_ID      = 1;
static const int     FRAME_MS      = 45;        // ~22 fps
static const int     CELL           = 16;        // pixel size of one grid cell
static const float   FILL_CLEAR_PCT = 0.30f;      // trigger clear when a monitor reaches 30% fill
static const int     ROWS_TO_CLEAR  = 4;

// Matrix green palette (exponential curve: bright head → dark green tail)
static const COLORREF MATRIX_GREENS[] = {
    RGB(218, 255, 228),   // 0 – bright white-green (very tip)
    RGB(120, 255, 160),   // 1 – bright green-white
    RGB(60, 255, 120),    // 2 – vivid bright green
    RGB(20, 250, 90),     // 3 – bright green
    RGB(0, 230, 75),      // 4 – strong green
    RGB(0, 200, 60),      // 5
    RGB(0, 165, 48),      // 6
    RGB(0, 130, 36),      // 7
    RGB(0, 95, 26),       // 8
    RGB(0, 65, 18),       // 9
    RGB(0, 42, 11),       // 10
    RGB(0, 28, 7),        // 11 – very dark
};
static const int NUM_GREENS = sizeof(MATRIX_GREENS) / sizeof(MATRIX_GREENS[0]);

// Tetris piece colors (all given a green/matrix tint)
static const COLORREF TETRIS_COLORS[] = {
    RGB(0, 255, 100),   // I  – bright green
    RGB(0, 200, 80),    // O  – medium green
    RGB(50, 255, 130),  // T  – lime
    RGB(0, 180, 60),    // S  – forest
    RGB(30, 230, 90),   // Z  – emerald
    RGB(0, 160, 70),    // J  – teal-green
    RGB(80, 255, 140),  // L  – mint
};

// ─── Tetris Piece Definitions ────────────────────────────────────────────────
// Each piece is 4 rotations of 4×4 bitmask (stored as 4 rows of 4 bits)

struct PieceDef {
    int cells[4][4][4]; // [rotation][row][col]  1=filled
};

static const PieceDef PIECES[7] = {
    // I
    {{{
        {0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
        {{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0}},
        {{0,0,0,0},{0,0,0,0},{1,1,1,1},{0,0,0,0}},
        {{0,1,0,0},{0,1,0,0},{0,1,0,0},{0,1,0,0}}
    }},
    // O
    {{{
        {0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}
    }},
    // T
    {{{
        {0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,1,0},{0,1,0,0},{0,0,0,0}},
        {{0,1,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}
    }},
    // S
    {{{
        {0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,1,0},{0,0,1,0},{0,0,0,0}},
        {{0,0,0,0},{0,1,1,0},{1,1,0,0},{0,0,0,0}},
        {{1,0,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}
    }},
    // Z
    {{{
        {1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,0,1,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,0,0},{0,1,1,0},{0,0,0,0}},
        {{0,1,0,0},{1,1,0,0},{1,0,0,0},{0,0,0,0}}
    }},
    // J
    {{{
        {1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,1,0},{0,0,1,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,0,0},{1,1,0,0},{0,0,0,0}}
    }},
    // L
    {{{
        {0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,0,0},{0,1,1,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,1,0},{1,0,0,0},{0,0,0,0}},
        {{1,1,0,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}}
    }}
};

// ─── Matrix rain character stream ────────────────────────────────────────────

struct MatrixStream {
    int   col;              // grid column
    float y;                // current head position (grid row, fractional)
    float speed;            // cells per tick
    int   length;           // tail length in cells
    std::vector<wchar_t> chars; // characters in the tail

    // Tetris piece at the bottom of this stream
    int   pieceType;        // 0-6
    int   rotation;         // 0-3
    COLORREF pieceColor;
    int   ticksToRotate;    // ticks until next rotation change
    bool  hardDropping;     // currently doing a fast drop
    float origSpeed;        // speed before hard-drop
    int   ticksToHardDrop;  // ticks until a hard-drop triggers
    int   monitorIdx;       // which monitor this stream belongs to
    bool  hasPiece;         // false = tail-only stream (no tetromino)
    std::vector<COLORREF> tailColors; // pre-computed color gradient (cached)
    std::vector<int> tailColorIndices; // color index for cache lookup

    // Pre-rendered tail bitmap for fast blitting
    HDC     tailDC;
    HBITMAP tailBmp;
    HBITMAP tailOldBmp;
    bool    tailDirty;      // true if tail needs re-rendering
};

// ─── Landed Tetris grid ──────────────────────────────────────────────────────

struct LandedCell {
    bool     filled;
    COLORREF color;
    int      brightness; // 0-255, for glow effect on placement
};

// ─── Monitor info ────────────────────────────────────────────────────────────

struct MonitorGrid {
    int left, top, right, bottom;  // grid-coordinate bounds (inclusive-exclusive)
};

// ─── Globals ─────────────────────────────────────────────────────────────────

static int          g_gridCols    = 0;
static int          g_gridRows    = 0;
static int          g_screenW     = 0;
static int          g_screenH     = 0;
static int          g_virtualX    = 0;  // virtual screen origin in screen coords
static int          g_virtualY    = 0;
static HFONT        g_font        = nullptr;
static HFONT        g_fontSmall   = nullptr;

// Character cache - pre-rendered Matrix characters at each color
static HDC          g_charCacheDC = nullptr;
static HBITMAP      g_charCacheBmp = nullptr;
static HBITMAP      g_charCacheOldBmp = nullptr;

// Black background cache for fast clearing
static HDC          g_blackDC = nullptr;
static HBITMAP      g_blackBmp = nullptr;
static HBITMAP      g_blackOldBmp = nullptr;

static std::vector<MonitorGrid>             g_monitors;

static std::vector<MatrixStream>            g_streams;
static std::vector<std::vector<LandedCell>> g_landed;  // [row][col]

// Per-monitor clear tracking — each monitor clears independently
enum ClearPhase { CLEAR_IDLE, CLEAR_FLASH, CLEAR_DROP };
struct MonitorClearInfo {
    int monIdx;             // which monitor
    ClearPhase phase;       // per-monitor clear phase
    int flashTick;          // countdown for flash
    std::vector<int> rows;  // rows being cleared (in grid coords)
    float dropOffset;       // current pixel offset during drop anim
    float dropTarget;       // target pixel offset
    int   lowestRow;        // lowest (bottom-most) cleared row
    int   highestRow;       // highest (top-most) cleared row
};
static std::vector<MonitorClearInfo> g_monitorClears; // one per monitor

static bool  g_isPreview = false;
static POINT g_initCursorPos;
static bool  g_active = true;
static int   g_targetMonitor = -1; // /m N switch: -1 = all monitors, 0+ = specific monitor index
static int   g_targetMonX = 0;    // pixel origin of targeted monitor
static int   g_targetMonY = 0;

// Persistent double-buffer
static HDC     g_memDC  = nullptr;
static HBITMAP g_memBmp = nullptr;
static HBITMAP g_oldBmp = nullptr;

// Cached GDI pens for rendering
static HPEN g_highlightPen = nullptr;  // bright edge for blocks
static HPEN g_scanlinePen  = nullptr;  // scanline overlay

// ─── Helpers ─────────────────────────────────────────────────────────────────

static inline int RandInt(int lo, int hi) {
    return lo + rand() % (hi - lo + 1);
}
static inline float RandFloat(float lo, float hi) {
    return lo + (float)rand() / RAND_MAX * (hi - lo);
}
static wchar_t RandMatrixChar() {
    // Half-width katakana + digits + latin
    int r = rand() % 3;
    if (r == 0) return (wchar_t)(0xFF66 + rand() % 56);   // katakana
    if (r == 1) return (wchar_t)('0' + rand() % 10);       // digits
    return (wchar_t)('A' + rand() % 26);                    // latin
}

// ─── Monitor enumeration ─────────────────────────────────────────────────────

static BOOL CALLBACK MonitorEnumProc(HMONITOR, HDC, LPRECT lprc, LPARAM) {
    MonitorGrid mg;
    mg.left   = (lprc->left   - g_virtualX) / CELL;
    mg.top    = (lprc->top    - g_virtualY) / CELL;
    mg.right  = (lprc->right  - g_virtualX + CELL - 1) / CELL;
    mg.bottom = (lprc->bottom - g_virtualY + CELL - 1) / CELL;
    // Clamp to grid bounds
    if (mg.left < 0) mg.left = 0;
    if (mg.top  < 0) mg.top  = 0;
    if (mg.right  > g_gridCols) mg.right  = g_gridCols;
    if (mg.bottom > g_gridRows) mg.bottom = g_gridRows;
    g_monitors.push_back(mg);
    return TRUE;
}

// Forward declarations
static void ComputeTailColors(MatrixStream& s);
static void CreateTailBitmap(MatrixStream& s, HDC screenDC);
static void RenderTailBitmap(MatrixStream& s);
static void CleanupTailBitmap(MatrixStream& s);

// ─── Character Cache Creation ────────────────────────────────────────────────

static void CreateCharacterCache(HDC screenDC) {
    // Create bitmap to hold pre-rendered characters at each color
    // We'll cache the most common Matrix characters at each of the 12 green shades
    int cacheW = 128 * CELL;  // Wide enough for diverse character set
    int cacheH = NUM_GREENS * CELL;

    g_charCacheDC = CreateCompatibleDC(screenDC);
    g_charCacheBmp = CreateCompatibleBitmap(screenDC, cacheW, cacheH);
    g_charCacheOldBmp = (HBITMAP)SelectObject(g_charCacheDC, g_charCacheBmp);

    // Clear to black
    RECT rcAll = {0, 0, cacheW, cacheH};
    FillRect(g_charCacheDC, &rcAll, (HBRUSH)GetStockObject(BLACK_BRUSH));

    // Set up text rendering
    SetBkMode(g_charCacheDC, TRANSPARENT);
    SelectObject(g_charCacheDC, g_font);

    // Pre-render characters at each color
    wchar_t chars[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789$#@*+-=<>[]{}|\\/:;,.!?";
    int numChars = static_cast<int>(wcslen(chars));

    for (int colorIdx = 0; colorIdx < NUM_GREENS; colorIdx++) {
        SetTextColor(g_charCacheDC, MATRIX_GREENS[colorIdx]);
        int y = colorIdx * CELL;

        for (int i = 0; i < numChars && i < 128; i++) {
            wchar_t str[2] = {chars[i], 0};
            int x = i * CELL;
            TextOutW(g_charCacheDC, x, y, str, 1);
        }
    }
}

static inline int GetCharCacheIndex(wchar_t ch) {
    // Map character to cache position
    if (ch >= L'A' && ch <= L'Z') return ch - L'A';
    if (ch >= L'a' && ch <= L'z') return 26 + (ch - L'a');
    if (ch >= L'0' && ch <= L'9') return 52 + (ch - L'0');
    // Special characters
    switch(ch) {
        case L'$': return 62;
        case L'#': return 63;
        case L'@': return 64;
        case L'*': return 65;
        case L'+': return 66;
        case L'-': return 67;
        default: return 0; // Fallback to 'A'
    }
}

// ─── Tail Bitmap Management ──────────────────────────────────────────────────

static void CreateTailBitmap(MatrixStream& s, HDC screenDC) {
    // Create a vertical bitmap strip for this stream's tail
    // Width: CELL, Height: length * CELL
    int w = CELL;
    int h = s.length * CELL;

    s.tailDC = CreateCompatibleDC(screenDC);
    s.tailBmp = CreateCompatibleBitmap(screenDC, w, h);
    s.tailOldBmp = (HBITMAP)SelectObject(s.tailDC, s.tailBmp);
    s.tailDirty = true;
}

static void RenderTailBitmap(MatrixStream& s) {
    // Render the entire tail to its bitmap
    // Clear to black first
    RECT rc = {0, 0, CELL, s.length * CELL};
    FillRect(s.tailDC, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));

    // Render each character from the character cache
    // Reverse order: index 0 (head/brightest) at bottom, index length-1 (tail/darkest) at top
    for (int i = 0; i < s.length; i++) {
        int colorIdx = s.tailColorIndices[i];
        int charIdx = GetCharCacheIndex(s.chars[i]);
        int srcX = charIdx * CELL;
        int srcY = colorIdx * CELL;
        int dstY = (s.length - 1 - i) * CELL;  // Reverse order in bitmap

        // BitBlt from character cache to tail bitmap
        BitBlt(s.tailDC, 0, dstY, CELL, CELL, 
               g_charCacheDC, srcX, srcY, SRCCOPY);
    }

    s.tailDirty = false;
}

static void CleanupTailBitmap(MatrixStream& s) {
    if (s.tailDC) {
        SelectObject(s.tailDC, s.tailOldBmp);
        DeleteObject(s.tailBmp);
        DeleteDC(s.tailDC);
        s.tailDC = nullptr;
        s.tailBmp = nullptr;
        s.tailOldBmp = nullptr;
    }
}

// ─── Initialization ──────────────────────────────────────────────────────────

static void InitGrid(int w, int h) {
    g_screenW  = w;
    g_screenH  = h;
    g_gridCols = w / CELL;
    g_gridRows = h / CELL;

    // Enumerate monitors
    g_monitors.clear();
    if (g_targetMonitor >= 0) {
        // Single-monitor mode: origin is target monitor's pixel position
        g_virtualX = g_targetMonX;
        g_virtualY = g_targetMonY;
        // Single monitor fills the entire grid
        g_monitors.push_back({0, 0, g_gridCols, g_gridRows});
    } else {
        // All monitors
        g_virtualX = GetSystemMetrics(SM_XVIRTUALSCREEN);
        g_virtualY = GetSystemMetrics(SM_YVIRTUALSCREEN);
        EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, 0);
        // If no monitors found (e.g., preview mode), treat entire surface as one monitor
        if (g_monitors.empty()) {
            g_monitors.push_back({0, 0, g_gridCols, g_gridRows});
        }
    }

    // init landed grid
    g_landed.assign(g_gridRows, std::vector<LandedCell>(g_gridCols, {false, 0, 0}));

    // create font for matrix characters
    g_font = CreateFontW(
        CELL, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

    g_fontSmall = CreateFontW(
        CELL - 2, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

    // create streams — per-monitor: tetromino streams + tail-only streams
    g_streams.clear();
    for (int mi = 0; mi < (int)g_monitors.size(); mi++) {
        auto& m = g_monitors[mi];
        int monW = m.right - m.left;
        int monH = m.bottom - m.top;
        int numPieceStreams = monW;
        if (numPieceStreams < 15) numPieceStreams = 15;
        int numTailOnly = numPieceStreams / 2;
        int totalStreams = numPieceStreams + numTailOnly;
        for (int i = 0; i < totalStreams; i++) {
            MatrixStream s;
            s.monitorIdx = mi;
            s.hasPiece   = (i < numPieceStreams);
            s.col    = RandInt(m.left, m.right - 1);
            s.y      = RandFloat((float)(m.top - 20), (float)m.top);
            s.speed  = RandFloat(0.08f, 1.2f);
            // Slow streams get long tails, fast streams get short tails (Matrix look)
            int maxLen = (s.speed < 0.3f) ? monH / 2 : (s.speed < 0.6f) ? monH / 3 : monH / 5;
            maxLen = maxLen * 5 / 4; // 25% longer tails
            if (maxLen < 8) maxLen = 8;
            s.length = RandInt(6, maxLen);
            s.chars.resize(s.length);
            for (int j = 0; j < s.length; j++) s.chars[j] = RandMatrixChar();
            s.pieceType   = RandInt(0, 6);
            s.rotation    = RandInt(0, 3);
            s.pieceColor  = TETRIS_COLORS[s.pieceType];
            s.ticksToRotate   = RandInt(10, 50);
            s.hardDropping    = false;
            s.origSpeed       = s.speed;
            s.ticksToHardDrop = RandInt(200, 800);

            // Initialize tail bitmap fields
            s.tailDC = nullptr;
            s.tailBmp = nullptr;
            s.tailOldBmp = nullptr;
            s.tailDirty = true;

            // Pre-compute tail color gradient
            ComputeTailColors(s);

            g_streams.push_back(s);
        }
    }

    // Init per-monitor clear tracking
    g_monitorClears.resize(g_monitors.size());
    for (int i = 0; i < (int)g_monitors.size(); i++) {
        g_monitorClears[i].monIdx = i;
        g_monitorClears[i].phase = CLEAR_IDLE;
        g_monitorClears[i].flashTick = 0;
        g_monitorClears[i].dropOffset = 0.0f;
        g_monitorClears[i].dropTarget = 0.0f;
        g_monitorClears[i].lowestRow = -1;
        g_monitorClears[i].highestRow = -1;
    }
}

// ─── Check if piece can land ─────────────────────────────────────────────────

static int GetPieceBottomRow(const MatrixStream& s) {
    // The piece is drawn at the head of the stream
    int headRow = (int)s.y;
    return headRow;
}

static bool CanPieceFitAt(int pieceType, int rotation, int gridRow, int gridCol, const MonitorGrid& mon) {
    const auto& cells = PIECES[pieceType].cells[rotation];
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (!cells[r][c]) continue;
            int gr = gridRow + r;
            int gc = gridCol + c - 1; // center the piece on the column
            if (gr < mon.top) continue;                    // above this monitor: skip
            if (gc < mon.left || gc >= mon.right) continue;// off-screen sideways for this monitor: clip
            if (gr >= mon.bottom) return false;             // below this monitor's floor
            if (g_landed[gr][gc].filled) return false;      // hit a landed block within our monitor
        }
    }
    return true;
}

static void LandPiece(MatrixStream& s) {
    int headRow = (int)s.y;
    int pieceCol = s.col;
    const auto& cells = PIECES[s.pieceType].cells[s.rotation];
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (!cells[r][c]) continue;
            int gr = headRow + r;
            int gc = pieceCol + c - 1;
            if (gr >= 0 && gr < g_gridRows && gc >= 0 && gc < g_gridCols) {
                g_landed[gr][gc].filled     = true;
                g_landed[gr][gc].color      = s.pieceColor;
                g_landed[gr][gc].brightness = 255;
            }
        }
    }
}

// Pre-compute color gradient for a tail (cached to avoid per-frame calculation)
static void ComputeTailColors(MatrixStream& s) {
    s.tailColors.resize(s.length);
    s.tailColorIndices.resize(s.length);
    for (int i = 0; i < s.length; i++) {
        float t = (float)i / (float)s.length;
        float exp_t = t * t; // quadratic curve
        int colorIdx = (int)(exp_t * (NUM_GREENS - 1));
        if (colorIdx >= NUM_GREENS) colorIdx = NUM_GREENS - 1;
        COLORREF clr = MATRIX_GREENS[colorIdx];
        // First few characters (head) are extra bright / near-white
        // Use color index 0 (brightest) for these
        if (i == 0 || i <= 2) {
            clr = (i == 0) ? RGB(240, 255, 245) : RGB(200, 255, 215);
            colorIdx = 0; // Use brightest cached color for heads
        }
        s.tailColors[i] = clr;
        s.tailColorIndices[i] = colorIdx;
    }
}

static void ResetStream(MatrixStream& s) {
    // Respawn within same monitor, keep same stream type (piece vs tail-only)
    auto& m = g_monitors[s.monitorIdx];
    int monH = m.bottom - m.top;
    s.col    = RandInt(m.left, m.right - 1);
    s.y      = RandFloat((float)(m.top - 20), (float)(m.top - 4));
    s.speed  = RandFloat(0.08f, 1.2f);
    int maxLen = (s.speed < 0.3f) ? monH / 2 : (s.speed < 0.6f) ? monH / 3 : monH / 5;
    maxLen = maxLen * 5 / 4; // 25% longer tails
    if (maxLen < 8) maxLen = 8;
    s.length = RandInt(6, maxLen);
    s.chars.resize(s.length);
    for (int j = 0; j < s.length; j++) s.chars[j] = RandMatrixChar();
    s.pieceType       = RandInt(0, 6);
    s.rotation        = RandInt(0, 3);
    s.pieceColor      = TETRIS_COLORS[s.pieceType];
    s.ticksToRotate   = RandInt(10, 50);
    s.hardDropping    = false;
    s.origSpeed       = s.speed;
    s.ticksToHardDrop = RandInt(200, 800);

    // Pre-compute tail color gradient
    ComputeTailColors(s);

    // Tail bitmap needs re-rendering with new length/characters
    if (s.tailDC) {
        // If length changed, recreate bitmap with new size
        int oldH = 0;
        BITMAP bm;
        if (GetObject(s.tailBmp, sizeof(bm), &bm)) {
            oldH = bm.bmHeight;
        }
        int newH = s.length * CELL;
        if (oldH != newH) {
            CleanupTailBitmap(s);
            HDC screenDC = GetDC(nullptr);
            CreateTailBitmap(s, screenDC);
            ReleaseDC(nullptr, screenDC);
        }
        RenderTailBitmap(s);
    }
}

// ─── Row clearing ────────────────────────────────────────────────────────────

static void StartClearForMonitor(MonitorClearInfo& mci) {
    auto& m = g_monitors[mci.monIdx];
    mci.dropOffset = 0.0f;
    mci.lowestRow = -1;
    mci.highestRow = -1;
    // Search from monitor's bottom upward for rows with content
    std::vector<int> contentRows;
    for (int r = m.bottom - 1; r >= m.top && (int)contentRows.size() < ROWS_TO_CLEAR; r--) {
        bool hasContent = false;
        for (int c = m.left; c < m.right; c++) {
            if (g_landed[r][c].filled) { hasContent = true; break; }
        }
        if (hasContent) {
            contentRows.push_back(r);
        }
    }
    if (contentRows.empty()) return;
    // contentRows[0] = lowest (bottom-most), contentRows.back() = highest (top-most)
    mci.lowestRow  = contentRows[0];
    mci.highestRow = contentRows.back();
    // Build the full contiguous span from highestRow to lowestRow
    mci.rows.clear();
    for (int r = mci.highestRow; r <= mci.lowestRow; r++) {
        mci.rows.push_back(r);
    }
    int span = mci.lowestRow - mci.highestRow + 1;
    mci.dropTarget = (float)(span * CELL);
    mci.phase = CLEAR_FLASH;
    mci.flashTick = 20;
}

static void ApplyClearAndStartDrop(MonitorClearInfo& mci) {
    // Clear the marked rows within this monitor
    auto& m = g_monitors[mci.monIdx];
    for (int r : mci.rows) {
        for (int c = m.left; c < m.right; c++) {
            g_landed[r][c] = {false, 0, 0};
        }
    }
    mci.dropOffset = 0.0f;
    mci.phase = CLEAR_DROP;
}

static void ApplyGravityForMonitor(const MonitorGrid& m, int numRows) {
    // Structure-preserving shift: move all rows above the cleared zone
    // down by numRows, keeping their relative positions intact.
    // Shift from bottom to top to avoid overwriting.
    // Find the topmost row that has content within this monitor
    int topContent = m.bottom;
    for (int r = m.top; r < m.bottom; r++) {
        for (int c = m.left; c < m.right; c++) {
            if (g_landed[r][c].filled) { topContent = r; goto found; }
        }
    }
    found:
    // Shift rows down by numRows
    for (int r = m.bottom - 1; r >= topContent + numRows; r--) {
        int src = r - numRows;
        for (int c = m.left; c < m.right; c++) {
            g_landed[r][c] = g_landed[src][c];
        }
    }
    // Clear the top numRows rows that were vacated
    for (int r = topContent; r < topContent + numRows && r < m.bottom; r++) {
        for (int c = m.left; c < m.right; c++) {
            g_landed[r][c] = {false, 0, 0};
        }
    }
}

// ─── Fill-level tracking ─────────────────────────────────────────────────────

static float GetMonitorFillPct(const MonitorGrid& m) {
    int monH = m.bottom - m.top;
    if (monH <= 0) return 0.0f;
    int filledRows = 0;
    for (int r = m.top; r < m.bottom; r++) {
        for (int c = m.left; c < m.right; c++) {
            if (g_landed[r][c].filled) { filledRows++; break; }
        }
    }
    return (float)filledRows / (float)monH;
}

// ─── Update ──────────────────────────────────────────────────────────────────

static void Update() {
    // ── Per-monitor row clearing state machine ────────────────────────
    DWORD now = GetTickCount();

    for (auto& mci : g_monitorClears) {
        if (mci.phase == CLEAR_IDLE) {
            // Check if this monitor has reached the fill threshold
            float fillPct = GetMonitorFillPct(g_monitors[mci.monIdx]);
            if (fillPct >= FILL_CLEAR_PCT) {
                StartClearForMonitor(mci);
            }
        } else if (mci.phase == CLEAR_FLASH) {
            mci.flashTick--;
            if (mci.flashTick <= 0) {
                ApplyClearAndStartDrop(mci);
            }
        } else if (mci.phase == CLEAR_DROP) {
            if (mci.dropOffset < mci.dropTarget) {
                float dropSpeed = 3.0f + mci.dropOffset * 0.05f;
                mci.dropOffset += dropSpeed;
                if (mci.dropOffset >= mci.dropTarget) {
                    mci.dropOffset = mci.dropTarget;
                    ApplyGravityForMonitor(g_monitors[mci.monIdx], (int)mci.rows.size());
                    mci.phase = CLEAR_IDLE;
                }
            } else {
                mci.phase = CLEAR_IDLE;
            }
        }
    }

    // ── Update streams ───────────────────────────────────────────────
    for (auto& s : g_streams) {
        // Check if this stream's monitor is currently clearing
        bool monitorClearing = false;
        if (s.monitorIdx < (int)g_monitorClears.size()) {
            monitorClearing = (g_monitorClears[s.monitorIdx].phase != CLEAR_IDLE);
        }
        float speedMul = monitorClearing ? 0.20f : 1.0f;

        // Rotation timer — only for piece streams
        if (s.hasPiece) {
            s.ticksToRotate--;
            if (s.ticksToRotate <= 0) {
                int newRot = (s.rotation + RandInt(1, 3)) % 4;
                if (CanPieceFitAt(s.pieceType, newRot, (int)s.y, s.col, g_monitors[s.monitorIdx])) {
                    s.rotation = newRot;
                }
                s.ticksToRotate = RandInt(10, 50);
            }

            // Hard drop trigger
            if (!s.hardDropping) {
                s.ticksToHardDrop--;
                if (s.ticksToHardDrop <= 0) {
                    s.hardDropping = true;
                    s.origSpeed = s.speed;
                    s.speed = RandFloat(1.5f, 5.0f); // very fast
                }
            }
        }

        // Move — step row by row so fast pieces can't skip through blocks
        float newY = s.y + s.speed * speedMul;
        int startRow = (int)s.y;
        int endRow   = (int)newY;
        bool landed  = false;

        // Randomly change a character in the tail
        if (rand() % 5 == 0 && s.chars.size() > 0) {
            int idx = rand() % s.chars.size();
            s.chars[idx] = RandMatrixChar();

            // Update just this character in the tail bitmap
            if (s.tailDC) {
                int colorIdx = s.tailColorIndices[idx];
                int charIdx = GetCharCacheIndex(s.chars[idx]);
                int srcX = charIdx * CELL;
                int srcY = colorIdx * CELL;
                int dstY = (s.length - 1 - idx) * CELL;  // Reverse order to match RenderTailBitmap
                BitBlt(s.tailDC, 0, dstY, CELL, CELL, 
                       g_charCacheDC, srcX, srcY, SRCCOPY);
            }
        }

        // Tail-only streams: just move and wrap, no collision
        if (!s.hasPiece) {
            s.y = newY;
            const auto& mon = g_monitors[s.monitorIdx];
            if ((int)s.y - s.length > mon.bottom + 10) {
                ResetStream(s);
            }
            continue;
        }

        // ── Collision detection: step through each row ───────────────
        const auto& mon = g_monitors[s.monitorIdx];
        if (endRow >= -3) {
            // Make sure we check from at least startRow
            int checkFrom = (startRow < -3) ? -3 : startRow;
            int landRow = -999;
            for (int testRow = checkFrom; testRow <= endRow; testRow++) {
                if (!CanPieceFitAt(s.pieceType, s.rotation, testRow, s.col, mon)) {
                    landRow = testRow - 1;  // last row that fit
                    break;
                }
            }
            if (landRow != -999) {
                // Land the piece at the last valid row
                if (landRow >= -3) {
                    s.y = (float)landRow;
                    bool anyOnScreen = false;
                    const auto& pcells = PIECES[s.pieceType].cells[s.rotation];
                    for (int r = 0; r < 4; r++)
                        for (int c2 = 0; c2 < 4; c2++)
                            if (pcells[r][c2] && landRow + r >= 0 && landRow + r < g_gridRows)
                                anyOnScreen = true;
                    if (anyOnScreen) LandPiece(s);
                }
                ResetStream(s);
                continue;
            }
        }
        s.y = newY;

        // If stream has gone fully off screen (past its monitor's floor)
        if ((int)s.y - s.length > mon.bottom + 10) {
            ResetStream(s);
        }
    }

    // Fade brightness of landed cells
    for (int r = 0; r < g_gridRows; r++) {
        for (int c = 0; c < g_gridCols; c++) {
            if (g_landed[r][c].brightness > 80) {
                g_landed[r][c].brightness -= 3;
            }
        }
    }
}

// ─── Rendering ───────────────────────────────────────────────────────────────

static COLORREF DimColor(COLORREF base, int brightness) {
    int rr = GetRValue(base) * brightness / 255;
    int gg = GetGValue(base) * brightness / 255;
    int bb = GetBValue(base) * brightness / 255;
    return RGB(rr, gg, bb);
}

static void Render(HDC hdc) {
    // Clear to black using BitBlt from pre-filled black bitmap (faster than FillRect)
    BitBlt(hdc, 0, 0, g_screenW, g_screenH, g_blackDC, 0, 0, SRCCOPY);

    SetBkMode(hdc, TRANSPARENT);
    HFONT oldFont = (HFONT)SelectObject(hdc, g_font);

    // ── Draw landed Tetris blocks ────────────────────────────────────────
    // Cache for brush/pen to avoid recreating identical colors
    HBRUSH cachedBr = nullptr;
    COLORREF cachedBrColor = 0xFFFFFFFF;
    HPEN cachedPen = nullptr;
    COLORREF cachedPenColor = 0xFFFFFFFF;

    for (int r = 0; r < g_gridRows; r++) {
        for (int c = 0; c < g_gridCols; c++) {
            if (!g_landed[r][c].filled) continue;
            int x = c * CELL;
            int y = r * CELL;

            // During drop animation, shift cells above cleared zone per-monitor
            for (auto& mci : g_monitorClears) {
                if (mci.phase != CLEAR_DROP) continue;
                auto& m = g_monitors[mci.monIdx];
                if (c >= m.left && c < m.right && r >= m.top && r < mci.highestRow) {
                    y += (int)mci.dropOffset;
                    if (y >= m.bottom * CELL) goto skip_cell;
                    break;
                }
            }

            {
                COLORREF col = DimColor(g_landed[r][c].color, g_landed[r][c].brightness);
                COLORREF penColor = DimColor(RGB(150, 255, 180), g_landed[r][c].brightness / 2);

                // Reuse brush if same color
                if (col != cachedBrColor) {
                    if (cachedBr) DeleteObject(cachedBr);
                    cachedBr = CreateSolidBrush(col);
                    cachedBrColor = col;
                }
                RECT rc = {x + 1, y + 1, x + CELL - 1, y + CELL - 1};
                FillRect(hdc, &rc, cachedBr);

                // Reuse pen if same color
                if (penColor != cachedPenColor) {
                    if (cachedPen) DeleteObject(cachedPen);
                    cachedPen = CreatePen(PS_SOLID, 1, penColor);
                    cachedPenColor = penColor;
                }
                HPEN oldPen = (HPEN)SelectObject(hdc, cachedPen);
                MoveToEx(hdc, x + 1, y + 1, nullptr);
                LineTo(hdc, x + CELL - 2, y + 1);
                MoveToEx(hdc, x + 1, y + 1, nullptr);
                LineTo(hdc, x + 1, y + CELL - 2);
                SelectObject(hdc, oldPen);
            }
            skip_cell:;
        }
    }
    // Clean up cached objects
    if (cachedBr) DeleteObject(cachedBr);
    if (cachedPen) DeleteObject(cachedPen);

    // ── Flash animation for cleared rows (per-monitor) ───────────────────
    for (auto& mci : g_monitorClears) {
        if (mci.phase != CLEAR_FLASH) continue;
        int alpha = mci.flashTick * 12;
        if (alpha > 255) alpha = 255;
        HBRUSH flashBr = CreateSolidBrush(RGB(0, alpha, alpha / 3));
        auto& m = g_monitors[mci.monIdx];
        for (int row : mci.rows) {
            RECT rc = {m.left * CELL, row * CELL, m.right * CELL, (row + 1) * CELL};
            FillRect(hdc, &rc, flashBr);
        }
        DeleteObject(flashBr);
    }

    // ── Draw Matrix streams and Tetris pieces ────────────────────────────
    for (const auto& s : g_streams) {
        int headRow = (int)s.y;

        // Find the topmost filled row of the piece so the tail connects snugly
        int pieceTopRow = 4; // default: no piece/cells found
        if (s.hasPiece) {
            const auto& cells = PIECES[s.pieceType].cells[s.rotation];
            for (int r = 0; r < 4; r++) {
                for (int c = 0; c < 4; c++) {
                    if (cells[r][c]) { pieceTopRow = r; goto found_top; }
                }
            }
            found_top:;
        }

        // Clip rendering to this stream's monitor
        const auto& mon = g_monitors[s.monitorIdx];

        // Draw character tail using pre-rendered tail bitmap
        // Single TransparentBlt for entire tail (black pixels are transparent)
        // Tail grows UPWARD from the head, so we need to calculate the top of the tail
        int tailStartRow = s.hasPiece ? (headRow + pieceTopRow - 1) : headRow;
        int dstX = s.col * CELL;
        int tailHeight = s.length * CELL;
        // Tail extends upward, so start from (tailStartRow - length + 1)
        int dstY = (tailStartRow - s.length + 1) * CELL;

        // Clip tail to monitor boundaries
        int srcY = 0;
        int clipTop = mon.top * CELL;
        int clipBottom = mon.bottom * CELL;

        if (dstY < clipTop) {
            // Tail extends above monitor - clip top portion
            int clipAmount = clipTop - dstY;
            srcY = clipAmount;
            tailHeight -= clipAmount;
            dstY = clipTop;
        }
        if (dstY + tailHeight > clipBottom) {
            // Tail extends below monitor - clip bottom portion
            tailHeight = clipBottom - dstY;
        }

        // Only draw if visible
        if (tailHeight > 0 && dstX >= mon.left * CELL && dstX < mon.right * CELL) {
            // Use TransparentBlt with black as transparent color so tails can overlap
            TransparentBlt(hdc, dstX, dstY, CELL, tailHeight,
                           s.tailDC, 0, srcY, CELL, tailHeight,
                           RGB(0, 0, 0));  // Black is transparent
        }

        // Skip piece drawing for tail-only streams
        if (!s.hasPiece) continue;

        // Draw Tetris piece at head position
        const auto& cells = PIECES[s.pieceType].cells[s.rotation];
        HPEN oldPP = (HPEN)SelectObject(hdc, g_highlightPen);

        // Create brushes/pens once per piece instead of per cell
        COLORREF pc = s.pieceColor;
        HBRUSH pieceBr = CreateSolidBrush(pc);
        HPEN shadowPen = CreatePen(PS_SOLID, 1, DimColor(pc, 100));

        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                if (!cells[r][c]) continue;
                int gr = headRow + r;
                int gc = s.col + c - 1;
                if (gr < mon.top || gr >= mon.bottom || gc < mon.left || gc >= mon.right) continue;

                int px = gc * CELL;
                int py = gr * CELL;

                RECT prc = {px + 1, py + 1, px + CELL - 1, py + CELL - 1};
                FillRect(hdc, &prc, pieceBr);

                // Bright edge (cached pen)
                MoveToEx(hdc, px + 1, py + 1, nullptr);
                LineTo(hdc, px + CELL - 2, py + 1);
                MoveToEx(hdc, px + 1, py + 1, nullptr);
                LineTo(hdc, px + 1, py + CELL - 2);

                // Shadow edge
                SelectObject(hdc, shadowPen);
                MoveToEx(hdc, px + CELL - 2, py + 1, nullptr);
                LineTo(hdc, px + CELL - 2, py + CELL - 2);
                MoveToEx(hdc, px + 1, py + CELL - 2, nullptr);
                LineTo(hdc, px + CELL - 2, py + CELL - 2);
                SelectObject(hdc, g_highlightPen);
            }
        }

        DeleteObject(pieceBr);
        DeleteObject(shadowPen);
        SelectObject(hdc, oldPP);
    }

    SelectObject(hdc, oldFont);

    // ── Scanline overlay for CRT effect ──────────────────────────────────
    HPEN oldScanPen = (HPEN)SelectObject(hdc, g_scanlinePen);
    for (int yy = 0; yy < g_screenH; yy += 3) {
        MoveToEx(hdc, 0, yy, nullptr);
        LineTo(hdc, g_screenW, yy);
    }
    SelectObject(hdc, oldScanPen);
}

// ─── Window Procedure ────────────────────────────────────────────────────────

static LRESULT CALLBACK ScreenSaverProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // Hide cursor
        ShowCursor(FALSE);
        GetCursorPos(&g_initCursorPos);

        RECT rc;
        GetClientRect(hWnd, &rc);
        srand((unsigned)time(nullptr));
        InitGrid(rc.right, rc.bottom);

        // Create persistent double-buffer
        HDC screenDC = GetDC(hWnd);
        g_memDC  = CreateCompatibleDC(screenDC);
        g_memBmp = CreateCompatibleBitmap(screenDC, g_screenW, g_screenH);
        g_oldBmp = (HBITMAP)SelectObject(g_memDC, g_memBmp);

        // Create character cache
        CreateCharacterCache(screenDC);

        // Create tail bitmaps for all streams
        for (auto& s : g_streams) {
            CreateTailBitmap(s, screenDC);
            RenderTailBitmap(s);
        }

        // Create pre-filled black bitmap for fast screen clearing
        g_blackDC = CreateCompatibleDC(screenDC);
        g_blackBmp = CreateCompatibleBitmap(screenDC, g_screenW, g_screenH);
        g_blackOldBmp = (HBITMAP)SelectObject(g_blackDC, g_blackBmp);
        RECT rcBlack = {0, 0, g_screenW, g_screenH};
        FillRect(g_blackDC, &rcBlack, (HBRUSH)GetStockObject(BLACK_BRUSH));

        ReleaseDC(hWnd, screenDC);

        // Cache pens
        g_highlightPen = CreatePen(PS_SOLID, 1, RGB(200, 255, 220));
        g_scanlinePen  = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));

        SetTimer(hWnd, TIMER_ID, FRAME_MS, nullptr);
        return 0;
    }
    case WM_TIMER:
        if (wParam == TIMER_ID) {
            Update();
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        Render(g_memDC);
        BitBlt(hdc, 0, 0, g_screenW, g_screenH, g_memDC, 0, 0, SRCCOPY);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
        if (!g_isPreview) {
            POINT pt;
            GetCursorPos(&pt);
            int dx = abs(pt.x - g_initCursorPos.x);
            int dy = abs(pt.y - g_initCursorPos.y);
            if (dx > 5 || dy > 5) {
                PostMessage(hWnd, WM_CLOSE, 0, 0);
            }
        }
        return 0;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_KEYDOWN:
        if (!g_isPreview) PostMessage(hWnd, WM_CLOSE, 0, 0);
        return 0;

    case WM_DESTROY:
        KillTimer(hWnd, TIMER_ID);
        if (g_font)      { DeleteObject(g_font); g_font = nullptr; }
        if (g_fontSmall)  { DeleteObject(g_fontSmall); g_fontSmall = nullptr; }
        if (g_memDC) {
            SelectObject(g_memDC, g_oldBmp);
            DeleteObject(g_memBmp);
            DeleteDC(g_memDC);
            g_memDC = nullptr;
        }
        if (g_charCacheDC) {
            SelectObject(g_charCacheDC, g_charCacheOldBmp);
            DeleteObject(g_charCacheBmp);
            DeleteDC(g_charCacheDC);
            g_charCacheDC = nullptr;
        }
        if (g_blackDC) {
            SelectObject(g_blackDC, g_blackOldBmp);
            DeleteObject(g_blackBmp);
            DeleteDC(g_blackDC);
            g_blackDC = nullptr;
        }
        // Clean up tail bitmaps
        for (auto& s : g_streams) {
            CleanupTailBitmap(s);
        }
        if (g_highlightPen) { DeleteObject(g_highlightPen); g_highlightPen = nullptr; }
        if (g_scanlinePen)  { DeleteObject(g_scanlinePen); g_scanlinePen = nullptr; }
        ShowCursor(TRUE);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ─── Config dialog ───────────────────────────────────────────────────────────

static INT_PTR CALLBACK ConfigDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// ─── Entry Point ─────────────────────────────────────────────────────────────
// Windows screensaver protocol:
//   /s           → run screensaver fullscreen (all monitors)
//   /s /m        → run screensaver on primary monitor only
//   /s /m N      → run screensaver on monitor N (0-based)
//   /m [N]       → same as /s /m [N]
//   /c           → show configuration dialog
//   /p <hwnd>    → preview in the little monitor in Display Properties

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int) {
    // Declare per-monitor DPI awareness so we get real physical pixel coordinates
    // on mixed-DPI multi-monitor setups
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Parse command line using GetCommandLineW + CommandLineToArgvW
    // (lpCmdLine can be stripped by Windows .scr shell handler, so use full command line)
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    bool doPreview = false;
    bool doConfig  = false;
    bool doRun     = false;
    HWND parentHwnd = nullptr;

    for (int i = 1; i < argc; i++) {
        wchar_t* arg = argv[i];
        // Strip leading / or -
        if (*arg == L'/' || *arg == L'-') arg++;

        if (_wcsicmp(arg, L"s") == 0) {
            doRun = true;
        } else if (_wcsicmp(arg, L"m") == 0) {
            // /m [N] — optional monitor index
            if (i + 1 < argc && argv[i+1][0] >= L'0' && argv[i+1][0] <= L'9') {
                g_targetMonitor = _wtoi(argv[++i]);
            } else {
                g_targetMonitor = 0; // /m with no number = primary
            }
            doRun = true;
        } else if (_wcsicmp(arg, L"c") == 0) {
            doConfig = true;
        } else if (_wcsicmp(arg, L"p") == 0) {
            doPreview = true;
            if (i + 1 < argc) {
                // Handle /p:HWND or /p HWND
                wchar_t* hwndStr = argv[++i];
                if (*hwndStr == L':') hwndStr++;
                parentHwnd = (HWND)(LONG_PTR)_wtol(hwndStr);
            }
        } else if (wcslen(arg) > 1 && (arg[0] == L'p' || arg[0] == L'P') && arg[1] == L':') {
            // Handle /p:HWND as a single token (no space)
            doPreview = true;
            parentHwnd = (HWND)(LONG_PTR)_wtol(arg + 2);
        }
    }

    // No recognized argument → config
    if (!doRun && !doPreview && !doConfig) {
        doConfig = true;
    }

    LocalFree(argv);

    if (doConfig) {
        DialogBox(hInstance, MAKEINTRESOURCE(IDD_CONFIG), nullptr, ConfigDlgProc);
        return 0;
    }

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = ScreenSaverProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);

    if (doPreview) {
        g_isPreview = true;
        RECT rc;
        GetClientRect(parentHwnd, &rc);
        HWND hWnd = CreateWindowExW(
            0, CLASS_NAME, L"",
            WS_CHILD | WS_VISIBLE,
            0, 0, rc.right, rc.bottom,
            parentHwnd, nullptr, hInstance, nullptr);
        if (!hWnd) return 1;

        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        return (int)msg.wParam;
    }

    if (doRun) {
        int vx, vy, sw, sh;
        if (g_targetMonitor >= 0) {
            // Use specific monitor by index
            struct MonEnum { int idx; int target; RECT rc; bool found; };
            MonEnum me = {0, g_targetMonitor, {}, false};
            EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR, HDC, LPRECT lprc, LPARAM data) -> BOOL {
                auto* p = (MonEnum*)data;
                if (p->idx == p->target) {
                    p->rc = *lprc;
                    p->found = true;
                    return FALSE; // stop enumerating
                }
                p->idx++;
                return TRUE;
            }, (LPARAM)&me);
            if (me.found) {
                vx = me.rc.left;
                vy = me.rc.top;
                sw = me.rc.right - me.rc.left;
                sh = me.rc.bottom - me.rc.top;
                g_targetMonX = vx;
                g_targetMonY = vy;
            } else {
                // Fallback to primary if index out of range
                vx = 0;
                vy = 0;
                sw = GetSystemMetrics(SM_CXSCREEN);
                sh = GetSystemMetrics(SM_CYSCREEN);
            }
        } else {
            // Fullscreen across ALL monitors
            vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
            vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
            sw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            sh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        }
        HWND hWnd = CreateWindowExW(
            WS_EX_TOPMOST, CLASS_NAME, L"Matrix Tetris",
            WS_POPUP | WS_VISIBLE,
            vx, vy, sw, sh,
            nullptr, nullptr, hInstance, nullptr);
        if (!hWnd) return 1;

        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        return (int)msg.wParam;
    }

    return 0;
}
