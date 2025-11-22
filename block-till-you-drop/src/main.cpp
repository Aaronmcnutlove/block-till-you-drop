#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <cmath>
#include <queue>
#include <string>
#include <cstdio>

static const int SCREEN_WIDTH  = 480;
static const int SCREEN_HEIGHT = 600;
static const int CELL          = 30;
static const int COLS          = SCREEN_WIDTH / CELL;
static const int ROWS          = SCREEN_HEIGHT / CELL;

enum BlockType {
    NORMAL = 0,
    BOMB,
    FREEZE,
    LASER_H,
    LASER_V
};

struct Block {
    int col, row;   // grid position
    BlockType type;
};

struct FallingShape {
    float x, y;                   // top-left in pixels
    float speed;                  // px/s
    std::vector<SDL_Point> cells; // (cx, cy) offsets in CELL units
    std::vector<BlockType> types; // same length as cells
};

bool rectsOverlap(const SDL_Rect &a, const SDL_Rect &b) {
    return (a.x < b.x + b.w &&
            a.x + a.w > b.x &&
            a.y < b.y + b.h &&
            a.y + a.h > b.y);
}

void buildOcc(const std::vector<Block> &blocks, bool occ[ROWS][COLS]) {
    for (int r = 0; r < ROWS; ++r)
        for (int c = 0; c < COLS; ++c)
            occ[r][c] = false;

    for (const auto &b : blocks) {
        if (b.row >= 0 && b.row < ROWS && b.col >= 0 && b.col < COLS)
            occ[b.row][b.col] = true;
    }
}

// Resolve disconnected clusters: unsupported components become falling shapes.
void resolveFloatingClusters(std::vector<Block> &staticBlocks,
                             std::vector<FallingShape> &fallingShapes,
                             float fallSpeed,
                             int pLeftCol,
                             int pRightCol,
                             int pTopRow,
                             int pBotRow)
{
    if (staticBlocks.empty()) return;

    bool occ[ROWS][COLS];
    buildOcc(staticBlocks, occ);

    BlockType typeAt[ROWS][COLS];
    for (int r = 0; r < ROWS; ++r)
        for (int c = 0; c < COLS; ++c)
            typeAt[r][c] = NORMAL;

    for (const auto &b : staticBlocks) {
        if (b.row >= 0 && b.row < ROWS && b.col >= 0 && b.col < COLS)
            typeAt[b.row][b.col] = b.type;
    }

    bool visited[ROWS][COLS] = { false };
    std::vector<Block> newStatic;
    newStatic.reserve(staticBlocks.size());

    for (int r0 = 0; r0 < ROWS; ++r0) {
        for (int c0 = 0; c0 < COLS; ++c0) {
            if (!occ[r0][c0] || visited[r0][c0]) continue;

            std::vector<SDL_Point> cells;
            std::queue<SDL_Point> q;
            q.push({c0, r0});
            visited[r0][c0] = true;

            while (!q.empty()) {
                SDL_Point p = q.front(); q.pop();
                cells.push_back(p);

                const int dc[4] = {1, -1, 0, 0};
                const int dr[4] = {0, 0, 1, -1};
                for (int k = 0; k < 4; ++k) {
                    int nc = p.x + dc[k];
                    int nr = p.y + dr[k];
                    if (nc < 0 || nc >= COLS || nr < 0 || nr >= ROWS) continue;
                    if (!occ[nr][nc] || visited[nr][nc]) continue;
                    visited[nr][nc] = true;
                    q.push({nc, nr});
                }
            }

            bool supported = false;

            for (auto &p : cells) {
                int col = p.x;
                int belowRow = p.y + 1;

                // 1) Ground
                if (p.y == ROWS - 1) {
                    supported = true;
                    break;
                }

                // 2) External tile directly below
                if (belowRow < ROWS && occ[belowRow][col]) {
                    bool belowInComp = false;
                    for (auto &qcell : cells) {
                        if (qcell.x == col && qcell.y == belowRow) {
                            belowInComp = true;
                            break;
                        }
                    }
                    if (!belowInComp) {
                        supported = true;
                        break;
                    }
                }

                // 3) Player support: player directly beneath this column
                if (belowRow >= pTopRow && belowRow <= pBotRow &&
                    col >= pLeftCol && col <= pRightCol) {
                    supported = true;
                    break;
                }
            }

            if (supported) {
                for (auto &p : cells) {
                    Block b;
                    b.col = p.x;
                    b.row = p.y;
                    b.type = typeAt[p.y][p.x];
                    newStatic.push_back(b);
                }
            } else {
                // Turn into one rigid falling shape
                int minC = cells[0].x, maxC = cells[0].x;
                int minR = cells[0].y, maxR = cells[0].y;
                for (auto &p : cells) {
                    if (p.x < minC) minC = p.x;
                    if (p.x > maxC) maxC = p.x;
                    if (p.y < minR) minR = p.y;
                    if (p.y > maxR) maxR = p.y;
                }

                FallingShape fs;
                fs.x = (float)(minC * CELL);
                fs.y = (float)(minR * CELL);
                fs.speed = fallSpeed;

                for (auto &p : cells) {
                    fs.cells.push_back({ p.x - minC, p.y - minR });
                    fs.types.push_back(typeAt[p.y][p.x]);
                }

                fallingShapes.push_back(fs);
            }
        }
    }

    staticBlocks.swap(newStatic);
}

void resetGame(SDL_Rect &player,
               float &playerVy,
               bool &onGround,
               std::vector<Block> &staticBlocks,
               std::vector<FallingShape> &fallingShapes,
               float &spawnTimer,
               float &elapsedTime,
               float &cdLeft,
               float &cdRight,
               float &cdUp,
               float &cdDown,
               float &timeSinceLastPowerup,
               float &freezeTimer,
               bool &gameOver)
{
    player.w = CELL;
    player.h = CELL;
    player.x = (SCREEN_WIDTH - player.w) / 2;
    player.y = SCREEN_HEIGHT - player.h - 10;

    playerVy  = 0.0f;
    onGround  = false;

    staticBlocks.clear();
    fallingShapes.clear();

    spawnTimer           = 0.0f;
    elapsedTime          = 0.0f;
    cdLeft = cdRight = cdUp = cdDown = 0.0f;
    timeSinceLastPowerup = 0.0f;
    freezeTimer          = 0.0f;
    gameOver             = false;
}

int main() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        SDL_Log("SDL_Init Error: %s", SDL_GetError());
        return 1;
    }

    if (TTF_Init() != 0) {
        SDL_Log("TTF_Init Error: %s", TTF_GetError());
    }

    SDL_Window *window = SDL_CreateWindow(
        "Block Till You Drop",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH, SCREEN_HEIGHT,
        0
    );
    if (!window) {
        SDL_Log("CreateWindow Error: %s", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (!renderer) {
        SDL_Log("CreateRenderer Error: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    // Fonts & static texts
    TTF_Font *font = nullptr;
    SDL_Texture *gameOverText = nullptr;
    SDL_Texture *restartText  = nullptr;
    SDL_Rect gameOverRect{}, restartRect{};

    if (TTF_WasInit()) {
        font = TTF_OpenFont(
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 24
        );
        if (font) {
            SDL_Color white{255,255,255,255};
            SDL_Surface *s1 = TTF_RenderText_Blended(font, "GAME OVER", white);
            SDL_Surface *s2 = TTF_RenderText_Blended(font, "Press R to Restart", white);
            if (s1) {
                gameOverText = SDL_CreateTextureFromSurface(renderer, s1);
                gameOverRect.w = s1->w;
                gameOverRect.h = s1->h;
                gameOverRect.x = (SCREEN_WIDTH - s1->w) / 2;
                gameOverRect.y = SCREEN_HEIGHT / 2 - 100;
                SDL_FreeSurface(s1);
            }
            if (s2) {
                restartText = SDL_CreateTextureFromSurface(renderer, s2);
                restartRect.w = s2->w;
                restartRect.h = s2->h;
                restartRect.x = (SCREEN_WIDTH - s2->w) / 2;
                restartRect.y = SCREEN_HEIGHT / 2 + 80;
                SDL_FreeSurface(s2);
            }
        }
    }

    std::srand((unsigned)std::time(nullptr));

    SDL_Rect player;
    float playerVy;
    bool onGround;

    std::vector<Block> staticBlocks;
    std::vector<FallingShape> fallingShapes;
    std::vector<float> highScores;

    float spawnTimer;
    float elapsedTime;
    float cdLeft, cdRight, cdUp, cdDown;
    float timeSinceLastPowerup;
    float freezeTimer;
    bool  gameOver;

    SDL_Texture *finalTimeText = nullptr;
    SDL_Texture *scoreListText = nullptr;
    SDL_Rect finalTimeRect{}, scoreListRect{};

    auto destroyOverlayTexts = [&]() {
        if (finalTimeText) { SDL_DestroyTexture(finalTimeText); finalTimeText = nullptr; }
        if (scoreListText) { SDL_DestroyTexture(scoreListText); scoreListText = nullptr; }
    };

    resetGame(player, playerVy, onGround,
              staticBlocks, fallingShapes,
              spawnTimer, elapsedTime,
              cdLeft, cdRight, cdUp, cdDown,
              timeSinceLastPowerup, freezeTimer,
              gameOver);

    // Tunables
    float playerSpeed      = 220.0f;
    const float GRAVITY    = 900.0f;
    const float JUMP_V     = -430.0f;
    const float ABILITY_CD = 0.5f;       // cooldown per direction

    float spawnInterval    = 0.75f;
    const float baseFall   = 220.0f;
    const float maxExtra   = 60.0f;      // gentle speed-up
    const float POWERUP_MAX_GAP = 15.0f; // guarantee one powerup in this window
    const float FREEZE_DURATION = 10.0f;

    bool running  = true;
    bool prevJump = false;

    Uint64 now  = SDL_GetPerformanceCounter();
    Uint64 last = now;
    double freq = (double)SDL_GetPerformanceFrequency();
    const double targetFrame = 1.0 / 60.0;

    while (running) {
        last = now;
        now  = SDL_GetPerformanceCounter();
        double dt = (now - last) / freq;
        if (dt > 0.05) dt = 0.05;

        if (!gameOver) {
            elapsedTime += (float)dt;
            timeSinceLastPowerup += (float)dt;
        }

        if (freezeTimer > 0.0f) {
            freezeTimer -= (float)dt;
            if (freezeTimer < 0.0f) freezeTimer = 0.0f;
        }

        float fallSpeed = baseFall + std::min(maxExtra, elapsedTime * 5.0f);

        // Cooldowns
        cdLeft  = std::max(0.0f, cdLeft  - (float)dt);
        cdRight = std::max(0.0f, cdRight - (float)dt);
        cdUp    = std::max(0.0f, cdUp    - (float)dt);
        cdDown  = std::max(0.0f, cdDown  - (float)dt);

        // ===== Input =====
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
        }
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        if (keys[SDL_SCANCODE_ESCAPE]) running = false;

        // Restart
        if (gameOver && keys[SDL_SCANCODE_R]) {
            destroyOverlayTexts();
            resetGame(player, playerVy, onGround,
                      staticBlocks, fallingShapes,
                      spawnTimer, elapsedTime,
                      cdLeft, cdRight, cdUp, cdDown,
                      timeSinceLastPowerup, freezeTimer,
                      gameOver);
            continue;
        }

        float oldPx = (float)player.x;
        float oldPy = (float)player.y;
        bool jumpPressed = keys[SDL_SCANCODE_SPACE];

        // ===== Player movement =====
        if (!gameOver) {
            float vx = 0.0f;
            if (keys[SDL_SCANCODE_A]) vx -= playerSpeed;
            if (keys[SDL_SCANCODE_D]) vx += playerSpeed;

            // Horizontal move
            float newX = player.x + vx * (float)dt;
            if (newX < 0) newX = 0;
            if (newX + player.w > SCREEN_WIDTH) newX = SCREEN_WIDTH - player.w;

            SDL_Rect hTest = player;
            hTest.x = (int)newX;
            for (const auto &b : staticBlocks) {
                SDL_Rect br{ b.col * CELL, b.row * CELL, CELL, CELL };
                if (rectsOverlap(hTest, br)) {
                    if (vx > 0) newX = br.x - player.w;
                    else if (vx < 0) newX = br.x + br.w;
                    hTest.x = (int)newX;
                }
            }
            player.x = (int)newX;

            // Jump only if grounded
            if (jumpPressed && !prevJump && onGround) {
                playerVy = JUMP_V;
                onGround = false;
            }

            // Gravity
            playerVy += GRAVITY * (float)dt;
            float newY = player.y + playerVy * (float)dt;

            // Reset & handle vertical collisions
            onGround = false;

            // Floor
            if (newY + player.h >= SCREEN_HEIGHT) {
                newY    = (float)(SCREEN_HEIGHT - player.h);
                playerVy = 0.0f;
                onGround = true;
            }

            SDL_Rect vTest = player;
            vTest.y = (int)newY;

            for (const auto &b : staticBlocks) {
                SDL_Rect br{ b.col * CELL, b.row * CELL, CELL, CELL };
                if (!rectsOverlap(vTest, br)) continue;

                // Landing on top of a block
                if (playerVy > 0 && oldPy + player.h <= br.y) {
                    newY    = (float)(br.y - player.h);
                    playerVy = 0.0f;
                    onGround = true;
                    vTest.y  = (int)newY;
                }
                // Hitting head on block above
                else if (playerVy < 0 && oldPy >= br.y + br.h) {
                    newY    = (float)(br.y + br.h);
                    playerVy = 0.0f;
                    vTest.y  = (int)newY;
                }
            }

            player.y = (int)newY;

            // EXTRA: robust onGround check so jumps don't get "stolen"
            onGround = false;
            int footY = player.y + player.h;
            if (footY >= SCREEN_HEIGHT - 1) {
                onGround = true;
            } else {
                for (const auto &b : staticBlocks) {
                    int top = b.row * CELL;
                    if (top == footY) {
                        int left  = b.col * CELL;
                        int right = left + CELL;
                        if (player.x + player.w > left && player.x < right) {
                            onGround = true;
                            break;
                        }
                    }
                }
            }
        }

        prevJump = jumpPressed;

        // ===== Spawn falling shapes =====
        if (!gameOver) {
            spawnTimer += (float)dt;
            if (spawnTimer >= spawnInterval) {
                spawnTimer = 0.0f;

                int shape = std::rand() % 5;
                int wCells = 1, hCells = 1;
                switch (shape) {
                    case 0: wCells = 1; hCells = 1; break;
                    case 1: wCells = 2; hCells = 1; break;
                    case 2: wCells = 4; hCells = 1; break;
                    case 3: wCells = 1; hCells = 2; break;
                    case 4: wCells = 1; hCells = 4; break;
                }

                int maxCol = COLS - wCells;
                int col = (maxCol > 0) ? (std::rand() % (maxCol + 1)) : 0;

                FallingShape fs;
                fs.x = (float)(col * CELL);
                fs.y = (float)(-hCells * CELL);
                fs.speed = fallSpeed;

                int total = wCells * hCells;
                fs.cells.reserve(total);
                fs.types.reserve(total);
                for (int dy = 0; dy < hCells; ++dy) {
                    for (int dx = 0; dx < wCells; ++dx) {
                        fs.cells.push_back({dx, dy});
                        fs.types.push_back(NORMAL);
                    }
                }

                // Powerup spawn logic
                bool makePowerup = false;
                int powerIndex = -1;
                if (timeSinceLastPowerup >= POWERUP_MAX_GAP) {
                    makePowerup = true;
                    powerIndex = std::rand() % total;
                } else if ((std::rand() % 100) < 7) { // ~7% chance
                    makePowerup = true;
                    powerIndex = std::rand() % total;
                }

                if (makePowerup && total > 0) {
                    int t = std::rand() % 4;
                    BlockType pt =
                        (t == 0) ? BOMB :
                        (t == 1) ? FREEZE :
                        (t == 2) ? LASER_H : LASER_V;
                    fs.types[powerIndex] = pt;
                    timeSinceLastPowerup = 0.0f;
                }

                fallingShapes.push_back(fs);
            }
        }

        // ===== Update falling shapes (respect freeze) =====
        if (!gameOver) {
            bool frozen = (freezeTimer > 0.0f);

            if (!frozen) {
                std::vector<FallingShape> newShapes;
                newShapes.reserve(fallingShapes.size());

                bool occ[ROWS][COLS];
                buildOcc(staticBlocks, occ);

                for (auto &s : fallingShapes) {
                    float newY   = s.y + s.speed * (float)dt;
                    float finalY = newY;
                    bool landed  = false;

                    for (size_t i = 0; i < s.cells.size(); ++i) {
                        SDL_Point cell = s.cells[i];

                        float oldBottom = s.y + (cell.y + 1) * CELL;
                        float newBottom = newY + (cell.y + 1) * CELL;

                        // Ground
                        if (newBottom >= SCREEN_HEIGHT) {
                            float candY = (float)(SCREEN_HEIGHT - (cell.y + 1) * CELL);
                            if (!landed || candY < finalY) {
                                finalY = candY;
                                landed = true;
                            }
                        }

                        // Static below
                        int c = (int)((s.x + cell.x * CELL) / CELL);
                        if (c < 0 || c >= COLS) continue;

                        for (int r = 0; r < ROWS; ++r) {
                            if (!occ[r][c]) continue;
                            float tileTop = (float)(r * CELL);
                            if (oldBottom <= tileTop && newBottom >= tileTop) {
                                float candY = tileTop - (cell.y + 1) * CELL;
                                if (!landed || candY < finalY) {
                                    finalY = candY;
                                    landed = true;
                                }
                            }
                        }
                    }

                    if (landed) {
                        s.y = finalY;
                        // convert to static
                        for (size_t i = 0; i < s.cells.size(); ++i) {
                            SDL_Point cell = s.cells[i];
                            BlockType bt = s.types[i];
                            Block b;
                            b.col = (int)((s.x / CELL) + cell.x);
                            b.row = (int)((s.y / CELL) + cell.y);
                            if (b.col >= 0 && b.col < COLS && b.row >= 0 && b.row < ROWS) {
                                b.type = bt;
                                staticBlocks.push_back(b);
                            }
                        }
                    } else {
                        s.y = newY;
                        newShapes.push_back(s);
                    }
                }

                fallingShapes.swap(newShapes);
            }
        }

        // ===== Directional abilities: arrow keys with cooldowns =====
        if (!gameOver) {
            int pCol = player.x / CELL;
            int pRow = player.y / CELL;

            auto applyPower = [&](BlockType type, int col, int row) {
                if (type == NORMAL) return;

                if (type == BOMB) {
                    int rad = 5;
                    staticBlocks.erase(
                        std::remove_if(staticBlocks.begin(), staticBlocks.end(),
                                       [&](const Block &b) {
                                           return std::abs(b.col - col) <= rad &&
                                                  std::abs(b.row - row) <= rad;
                                       }),
                        staticBlocks.end()
                    );
                    for (auto &s : fallingShapes) {
                        std::vector<SDL_Point> newCells;
                        std::vector<BlockType> newTypes;
                        for (size_t i = 0; i < s.cells.size(); ++i) {
                            SDL_Point cell = s.cells[i];
                            int gc = (int)(s.x / CELL) + cell.x;
                            int gr = (int)(s.y / CELL) + cell.y;
                            if (std::abs(gc - col) <= rad &&
                                std::abs(gr - row) <= rad) {
                                continue;
                            }
                            newCells.push_back(cell);
                            newTypes.push_back(s.types[i]);
                        }
                        s.cells.swap(newCells);
                        s.types.swap(newTypes);
                    }
                    fallingShapes.erase(
                        std::remove_if(fallingShapes.begin(), fallingShapes.end(),
                                       [](const FallingShape &fs) {
                                           return fs.cells.empty();
                                       }),
                        fallingShapes.end()
                    );
                }

                if (type == FREEZE) {
                    // Freeze everything in place: no falling, no new clusters until it ends
                    freezeTimer = FREEZE_DURATION;
                }

                if (type == LASER_H) {
                    staticBlocks.erase(
                        std::remove_if(staticBlocks.begin(), staticBlocks.end(),
                                       [&](const Block &b) { return b.row == row; }),
                        staticBlocks.end()
                    );
                    for (auto &s : fallingShapes) {
                        std::vector<SDL_Point> newCells;
                        std::vector<BlockType> newTypes;
                        for (size_t i = 0; i < s.cells.size(); ++i) {
                            SDL_Point cell = s.cells[i];
                            int gr = (int)(s.y / CELL) + cell.y;
                            if (gr == row) continue;
                            newCells.push_back(cell);
                            newTypes.push_back(s.types[i]);
                        }
                        s.cells.swap(newCells);
                        s.types.swap(newTypes);
                    }
                    fallingShapes.erase(
                        std::remove_if(fallingShapes.begin(), fallingShapes.end(),
                                       [](const FallingShape &fs) {
                                           return fs.cells.empty();
                                       }),
                        fallingShapes.end()
                    );
                }

                if (type == LASER_V) {
                    staticBlocks.erase(
                        std::remove_if(staticBlocks.begin(), staticBlocks.end(),
                                       [&](const Block &b) { return b.col == col; }),
                        staticBlocks.end()
                    );
                    for (auto &s : fallingShapes) {
                        std::vector<SDL_Point> newCells;
                        std::vector<BlockType> newTypes;
                        for (size_t i = 0; i < s.cells.size(); ++i) {
                            SDL_Point cell = s.cells[i];
                            int gc = (int)(s.x / CELL) + cell.x;
                            if (gc == col) continue;
                            newCells.push_back(cell);
                            newTypes.push_back(s.types[i]);
                        }
                        s.cells.swap(newCells);
                        s.types.swap(newTypes);
                    }
                    fallingShapes.erase(
                        std::remove_if(fallingShapes.begin(), fallingShapes.end(),
                                       [](const FallingShape &fs) {
                                           return fs.cells.empty();
                                       }),
                        fallingShapes.end()
                    );
                }
            };

            auto breakAt = [&](int tc, int tr, float &cd, BlockType &usedType) -> bool {
                if (cd > 0.0f) return false;
                if (tc < 0 || tc >= COLS || tr < 0 || tr >= ROWS) return false;
                for (auto it = staticBlocks.begin(); it != staticBlocks.end(); ++it) {
                    if (it->col == tc && it->row == tr) {
                        usedType = it->type;
                        staticBlocks.erase(it);
                        cd = ABILITY_CD;
                        return true;
                    }
                }
                return false;
            };

            BlockType usedType = NORMAL;

            if (keys[SDL_SCANCODE_LEFT]) {
                int tc = pCol - 1, tr = pRow;
                if (breakAt(tc, tr, cdLeft, usedType)) applyPower(usedType, tc, tr);
            }
            if (keys[SDL_SCANCODE_RIGHT]) {
                int tc = pCol + 1, tr = pRow;
                if (breakAt(tc, tr, cdRight, usedType)) applyPower(usedType, tc, tr);
            }
            if (keys[SDL_SCANCODE_UP]) {
                int tc = pCol, tr = pRow - 1;
                if (breakAt(tc, tr, cdUp, usedType)) applyPower(usedType, tc, tr);
            }
            if (keys[SDL_SCANCODE_DOWN]) {
                int tc = pCol, tr = pRow + 1;
                if (breakAt(tc, tr, cdDown, usedType)) applyPower(usedType, tc, tr);
            }
        }

        // ===== Full row clear =====
        if (!gameOver && !staticBlocks.empty()) {
            bool occ[ROWS][COLS];
            buildOcc(staticBlocks, occ);
            std::vector<int> fullRows;
            for (int r = 0; r < ROWS; ++r) {
                bool full = true;
                for (int c = 0; c < COLS; ++c) {
                    if (!occ[r][c]) { full = false; break; }
                }
                if (full) fullRows.push_back(r);
            }
            if (!fullRows.empty()) {
                std::sort(fullRows.begin(), fullRows.end());
                staticBlocks.erase(
                    std::remove_if(staticBlocks.begin(), staticBlocks.end(),
                                   [&](const Block &b) {
                                       return std::binary_search(fullRows.begin(),
                                                                 fullRows.end(), b.row);
                                   }),
                    staticBlocks.end()
                );
                for (auto &b : staticBlocks) {
                    int shift = 0;
                    for (int fr : fullRows)
                        if (fr > b.row) shift++;
                    b.row += shift;
                }
            }
        }

        // ===== Floating clusters -> falling shapes
        // IMPORTANT: do NOT create falling shapes while frozen,
        // or freeze becomes useless (they'd turn red/unbreakable).
        if (!gameOver && freezeTimer <= 0.0f) {
            int pLeftCol  = player.x / CELL;
            int pRightCol = (player.x + player.w - 1) / CELL;
            int pTopRow   = player.y / CELL;
            int pBotRow   = (player.y + player.h - 1) / CELL;
            resolveFloatingClusters(staticBlocks, fallingShapes,
                                    fallSpeed,
                                    pLeftCol, pRightCol,
                                    pTopRow, pBotRow);
        }

        // ===== Game Over check =====
        bool justGameOver = false;
        if (!gameOver) {
            for (const auto &b : staticBlocks) {
                if (b.row <= 0) {
                    gameOver = true;
                    justGameOver = true;
                    SDL_Log("GAME OVER: stack reached the top.");
                    break;
                }
            }
        }

        if (justGameOver) {
            float finalTime = elapsedTime;
            highScores.push_back(finalTime);
            std::sort(highScores.begin(), highScores.end(),
                      std::greater<float>());
            if (highScores.size() > 5) highScores.resize(5);

            destroyOverlayTexts();

            if (font) {
                SDL_Color white{255,255,255,255};

                // Final time
                char buf[64];
                std::snprintf(buf, sizeof(buf), "Time: %.2f s", finalTime);
                SDL_Surface *fs = TTF_RenderText_Blended(font, buf, white);
                if (fs) {
                    finalTimeText = SDL_CreateTextureFromSurface(renderer, fs);
                    finalTimeRect.w = fs->w;
                    finalTimeRect.h = fs->h;
                    finalTimeRect.x = (SCREEN_WIDTH - fs->w) / 2;
                    finalTimeRect.y = gameOverRect.y + gameOverRect.h + 10;
                    SDL_FreeSurface(fs);
                }

                // High score list
                std::string hs = "High Scores:";
                for (size_t i = 0; i < highScores.size(); ++i) {
                    char line[64];
                    std::snprintf(line, sizeof(line),
                                  "\n%d) %.2f s",
                                  (int)(i + 1), highScores[i]);
                    hs += line;
                }
                SDL_Surface *hsSurf =
                    TTF_RenderText_Blended_Wrapped(font, hs.c_str(), white, 360);
                if (hsSurf) {
                    scoreListText = SDL_CreateTextureFromSurface(renderer, hsSurf);
                    scoreListRect.w = hsSurf->w;
                    scoreListRect.h = hsSurf->h;
                    scoreListRect.x = (SCREEN_WIDTH - hsSurf->w) / 2;
                    scoreListRect.y = finalTimeRect.y + finalTimeRect.h + 10;
                    SDL_FreeSurface(hsSurf);
                }

                // Place "Press R" below scores so nothing overlaps
                if (restartText) {
                    restartRect.y = scoreListText
                        ? (scoreListRect.y + scoreListRect.h + 10)
                        : (finalTimeRect.y + finalTimeRect.h + 30);
                }
            }
        }

        // ===== Rendering =====
        SDL_SetRenderDrawColor(renderer, 10,10,25,255);
        SDL_RenderClear(renderer);

        auto drawBombIcon = [&](SDL_Rect r) {
            int margin = r.w / 4;
            SDL_Rect core{ r.x + margin, r.y + margin,
                           r.w - 2*margin, r.h - 2*margin };
            SDL_SetRenderDrawColor(renderer, 40,40,40,255);
            SDL_RenderFillRect(renderer, &core);
        };

        auto drawFreezeIcon = [&](SDL_Rect r) {
            SDL_SetRenderDrawColor(renderer, 255,255,255,255);
            int cx = r.x + r.w/2;
            int cy = r.y + r.h/2;
            SDL_RenderDrawLine(renderer, cx - r.w/3, cy, cx + r.w/3, cy);
            SDL_RenderDrawLine(renderer, cx, cy - r.h/3, cx, cy + r.h/3);
        };

        auto drawLaserHIcon = [&](SDL_Rect r) {
            SDL_SetRenderDrawColor(renderer, 255,50,50,255);
            int mid = r.y + r.h/2;
            SDL_Rect stripe{ r.x + 2, mid - 2, r.w - 4, 4 };
            SDL_RenderFillRect(renderer, &stripe);
        };

        auto drawLaserVIcon = [&](SDL_Rect r) {
            SDL_SetRenderDrawColor(renderer, 255,50,50,255);
            int mid = r.x + r.w/2;
            SDL_Rect stripe{ mid - 2, r.y + 2, 4, r.h - 4 };
            SDL_RenderFillRect(renderer, &stripe);
        };

        // Static tiles
        for (const auto &b : staticBlocks) {
            SDL_Rect r{ b.col * CELL, b.row * CELL, CELL, CELL };
            switch (b.type) {
                case NORMAL:
                    SDL_SetRenderDrawColor(renderer, 80,160,255,255);
                    SDL_RenderFillRect(renderer, &r);
                    break;
                case BOMB:
                    SDL_SetRenderDrawColor(renderer, 200,40,40,255);
                    SDL_RenderFillRect(renderer, &r);
                    drawBombIcon(r);
                    break;
                case FREEZE:
                    SDL_SetRenderDrawColor(renderer, 120,200,255,255);
                    SDL_RenderFillRect(renderer, &r);
                    drawFreezeIcon(r);
                    break;
                case LASER_H:
                    SDL_SetRenderDrawColor(renderer, 240,240,100,255);
                    SDL_RenderFillRect(renderer, &r);
                    drawLaserHIcon(r);
                    break;
                case LASER_V:
                    SDL_SetRenderDrawColor(renderer, 180,255,140,255);
                    SDL_RenderFillRect(renderer, &r);
                    drawLaserVIcon(r);
                    break;
            }
        }

        // Falling shapes
        for (const auto &s : fallingShapes) {
            for (size_t i = 0; i < s.cells.size(); ++i) {
                SDL_Point cell = s.cells[i];
                BlockType bt = s.types[i];
                SDL_Rect r{
                    (int)(s.x + cell.x * CELL),
                    (int)(s.y + cell.y * CELL),
                    CELL, CELL
                };
                switch (bt) {
                    case NORMAL:
                        SDL_SetRenderDrawColor(renderer, 200,80,80,255);
                        SDL_RenderFillRect(renderer, &r);
                        break;
                    case BOMB:
                        SDL_SetRenderDrawColor(renderer, 230,60,60,255);
                        SDL_RenderFillRect(renderer, &r);
                        drawBombIcon(r);
                        break;
                    case FREEZE:
                        SDL_SetRenderDrawColor(renderer, 150,220,255,255);
                        SDL_RenderFillRect(renderer, &r);
                        drawFreezeIcon(r);
                        break;
                    case LASER_H:
                        SDL_SetRenderDrawColor(renderer, 255,255,150,255);
                        SDL_RenderFillRect(renderer, &r);
                        drawLaserHIcon(r);
                        break;
                    case LASER_V:
                        SDL_SetRenderDrawColor(renderer, 200,255,160,255);
                        SDL_RenderFillRect(renderer, &r);
                        drawLaserVIcon(r);
                        break;
                }
            }
        }

        // Player
        SDL_SetRenderDrawColor(renderer, 0,255,180,255);
        SDL_RenderFillRect(renderer, &player);

        // In-game timer
        if (!gameOver && font) {
            SDL_Color white{255,255,255,255};
            char buf[32];
            std::snprintf(buf, sizeof(buf), "Time: %.1f", elapsedTime);
            SDL_Surface *ts = TTF_RenderText_Blended(font, buf, white);
            if (ts) {
                SDL_Texture *tt = SDL_CreateTextureFromSurface(renderer, ts);
                SDL_Rect tr{10, 10, ts->w, ts->h};
                SDL_RenderCopy(renderer, tt, nullptr, &tr);
                SDL_DestroyTexture(tt);
                SDL_FreeSurface(ts);
            }
        }

        // Game Over overlay
        if (gameOver) {
            SDL_SetRenderDrawColor(renderer, 0,0,0,180);
            SDL_Rect overlay{0,0,SCREEN_WIDTH,SCREEN_HEIGHT};
            SDL_RenderFillRect(renderer, &overlay);

            SDL_SetRenderDrawColor(renderer, 60,0,0,230);
            SDL_Rect panel{
                SCREEN_WIDTH/2 - 210,
                SCREEN_HEIGHT/2 - 130,
                420,
                260
            };
            SDL_RenderFillRect(renderer, &panel);

            if (gameOverText)
                SDL_RenderCopy(renderer, gameOverText, nullptr, &gameOverRect);
            if (finalTimeText)
                SDL_RenderCopy(renderer, finalTimeText, nullptr, &finalTimeRect);
            if (scoreListText)
                SDL_RenderCopy(renderer, scoreListText, nullptr, &scoreListRect);
            if (restartText)
                SDL_RenderCopy(renderer, restartText, nullptr, &restartRect);
        }

        SDL_RenderPresent(renderer);

        // Frame pacing
        Uint64 end = SDL_GetPerformanceCounter();
        double frameTime = (end - now) / freq;
        if (frameTime < targetFrame) {
            Uint32 delayMs = (Uint32)((targetFrame - frameTime) * 1000.0);
            if (delayMs > 0) SDL_Delay(delayMs);
        }
    }

    destroyOverlayTexts();
    if (gameOverText) SDL_DestroyTexture(gameOverText);
    if (restartText)  SDL_DestroyTexture(restartText);
    if (font)         TTF_CloseFont(font);
    TTF_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
