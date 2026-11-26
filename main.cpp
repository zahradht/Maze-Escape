#include <ncursesw/ncurses.h>
#include <clocale>
#include <string>
#include <vector>
#include <algorithm>
#include <random>
#include <ctime>
#include <chrono>
#include <cstdlib>
#include <queue>
#include <fstream>
#include <cmath>
#include <cstdint>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>

using namespace std;
using namespace std::chrono;

// MAZE ESCAPE - ANCIENT MYSTERY DUNGEON
// C++17 + NCURSESW

constexpr int MAP_H = 19;
constexpr int MAP_W = 39;
constexpr int MAP_Y = 5;
constexpr int MAP_X = 2;
constexpr int TILE_W = 2;
constexpr int TOTAL_LEVELS = 3;

constexpr int SIDE_X = MAP_X + MAP_W * TILE_W + 4;

mt19937 rng(static_cast<unsigned int>(time(nullptr)));

enum ColorID {
    C_WALL = 1,
    C_PLAYER,
    C_CRYSTAL,
    C_COIN,
    C_KEY,
    C_EXIT,
    C_ENEMY,
    C_TRAP,
    C_TITLE,
    C_TEXT,
    C_DIM,
    C_HP,
    C_PANEL,
    C_SUCCESS,
    C_WARNING
};

struct Position {
    int y, x;
};

struct Pickup {
    int y, x;
    char type;
    bool taken = false;
};

struct Enemy {
    int y, x;
    bool hunter;
};

struct Trap {
    int y, x;
    bool activated = false;
};

struct Game {
    vector<string> maze;
    vector<Pickup> pickups;
    vector<Enemy> enemies;
    vector<Trap> traps;

    int playerY = 1;
    int playerX = 1;

    int hp = 5;
    int maxHP = 5;

    int score = 0;
    int coins = 0;
    int crystals = 0;
    int requiredCrystals = 3;

    int level = 1;
    int difficulty = 2;

    bool hasKey = false;

    string message = "Explore the ancient dungeon!";

    steady_clock::time_point lastEnemyMove;
    steady_clock::time_point lastDamage;
};

// RANDOM
int randomInt(int minimum, int maximum) {
    uniform_int_distribution<int> dist(minimum, maximum);
    return dist(rng);
}

void shuffleVector(vector<int>& values) {
    shuffle(values.begin(), values.end(), rng);
}

bool insideMap(int y, int x) {
    return y > 0 && y < MAP_H - 1 &&
           x > 0 && x < MAP_W - 1;
}

int distanceBetween(int y1, int x1, int y2, int x2) {
    return abs(y1 - y2) + abs(x1 - x2);
}

// AUDIO SYSTEM - REAL WAV AUDIO FOR WSL
const string AUDIO_DIR = "/tmp/maze_escape_audio/";

pid_t menuMusicPID = -1;

void writeWav(const string& filename,
              const vector<pair<double, int>>& notes,
              double volume = 0.35) {

    const int sampleRate = 22050;
    vector<int16_t> samples;

    for (const auto& note : notes) {
        double frequency = note.first;
        int duration = note.second;

        int count = sampleRate * duration / 1000;

        for (int i = 0; i < count; i++) {
            double sample = 0;

            if (frequency > 0) {
                double t = static_cast<double>(i) / sampleRate;

                double envelope = min(1.0, i / 300.0);
                envelope *= min(1.0, (count - i) / 500.0);

                sample = sin(2.0 * M_PI * frequency * t)
                         * volume * envelope;
            }

            samples.push_back(
                static_cast<int16_t>(sample * 32767)
            );
        }
    }

    ofstream file(filename, ios::binary);

    if (!file) return;

    uint32_t dataSize = samples.size() * sizeof(int16_t);
    uint32_t fileSize = 36 + dataSize;
    uint16_t audioFormat = 1;
    uint16_t channels = 1;
    uint16_t bitsPerSample = 16;
    uint32_t byteRate = sampleRate * channels * bitsPerSample / 8;
    uint16_t blockAlign = channels * bitsPerSample / 8;

    file.write("RIFF", 4);
    file.write(reinterpret_cast<char*>(&fileSize), 4);
    file.write("WAVE", 4);

    file.write("fmt ", 4);

    uint32_t subchunk1Size = 16;

    file.write(reinterpret_cast<char*>(&subchunk1Size), 4);
    file.write(reinterpret_cast<char*>(&audioFormat), 2);
    file.write(reinterpret_cast<char*>(&channels), 2);
    file.write(reinterpret_cast<char*>(const_cast<int*>(&sampleRate)), 4);
    file.write(reinterpret_cast<char*>(&byteRate), 4);
    file.write(reinterpret_cast<char*>(&blockAlign), 2);
    file.write(reinterpret_cast<char*>(&bitsPerSample), 2);

    file.write("data", 4);
    file.write(reinterpret_cast<char*>(&dataSize), 4);

    file.write(
        reinterpret_cast<char*>(samples.data()),
        dataSize
    );

    file.close();
}

void createAudioFiles() {

    mkdir(AUDIO_DIR.c_str(), 0777);

    // OPENING SOUND
    writeWav(AUDIO_DIR + "opening.wav", {
        {392, 180}, {523, 180}, {659, 250},
        {784, 350}, {0, 100}, {659, 180},
        {784, 450}
    }, 0.45);

    // MENU BACKGROUND MUSIC
    writeWav(AUDIO_DIR + "menu.wav", {
        {523, 300}, {659, 300}, {784, 450},
        {659, 300}, {523, 450}, {0, 120},
        {440, 300}, {523, 300}, {659, 450},
        {587, 300}, {523, 450}, {0, 120},
        {392, 300}, {523, 300}, {659, 450},
        {523, 300}, {440, 450}, {0, 120},
        {392, 300}, {440, 300}, {523, 600},
        {0, 300}
    }, 0.16);

    // ITEM COLLECTION
    writeWav(AUDIO_DIR + "collect.wav", {
        {880, 90}, {1175, 120}, {1568, 180}
    }, 0.45);

    // MONSTER ATTACK
    writeWav(AUDIO_DIR + "monster.wav", {
        {180, 180}, {120, 250}, {85, 350}
    }, 0.65);

    // TRAP
    writeWav(AUDIO_DIR + "trap.wav", {
        {950, 70}, {250, 180}, {120, 350}
    }, 0.60);

    // LEVEL COMPLETE
    writeWav(AUDIO_DIR + "level.wav", {
        {523, 160}, {659, 160}, {784, 160},
        {1046, 350}
    }, 0.45);

    // VICTORY
    writeWav(AUDIO_DIR + "victory.wav", {
        {523, 180}, {659, 180}, {784, 180},
        {1046, 250}, {1175, 250}, {1318, 500}
    }, 0.50);
}

void playWav(const string& filename) {

    while (waitpid(-1, nullptr, WNOHANG) > 0) {}

    pid_t pid = fork();

    if (pid == 0) {
        execlp(
            "paplay",
            "paplay",
            filename.c_str(),
            (char*)nullptr
        );

        _exit(127);
    }
}

void playEffect(const string& filename) {
    playWav(AUDIO_DIR + filename);
}

void startMenuMusic() {

    if (menuMusicPID > 0) return;

    menuMusicPID = fork();

    if (menuMusicPID == 0) {

        setpgid(0, 0);

        execl(
            "/bin/sh",
            "sh",
            "-c",
            "while true; do paplay /tmp/maze_escape_audio/menu.wav >/dev/null 2>&1; done",
            (char*)nullptr
        );

        _exit(127);
    }

    if (menuMusicPID > 0) {
        setpgid(menuMusicPID, menuMusicPID);
    }
}

void stopMenuMusic() {

    if (menuMusicPID > 0) {
        kill(-menuMusicPID, SIGTERM);
        waitpid(menuMusicPID, nullptr, 0);
        menuMusicPID = -1;
    }
}

void playSound(int type) {

    switch (type) {

        case 1:
            playEffect("collect.wav");
            break;

        case 2:
            playEffect("monster.wav");
            break;

        case 3:
            playEffect("level.wav");
            break;

        case 4:
            playEffect("victory.wav");
            break;

        case 5:
            playEffect("trap.wav");
            break;
    }
}

// COLORS 
void initializeColors() {
    if (!has_colors()) return;

    start_color();
    use_default_colors();

    init_pair(C_WALL, COLOR_BLUE, COLOR_BLUE);
    init_pair(C_PLAYER, COLOR_YELLOW, -1);
    init_pair(C_CRYSTAL, COLOR_CYAN, -1);
    init_pair(C_COIN, COLOR_YELLOW, -1);
    init_pair(C_KEY, COLOR_MAGENTA, -1);
    init_pair(C_EXIT, COLOR_GREEN, -1);
    init_pair(C_ENEMY, COLOR_RED, -1);
    init_pair(C_TRAP, COLOR_RED, -1);
    init_pair(C_TITLE, COLOR_CYAN, -1);
    init_pair(C_TEXT, COLOR_WHITE, -1);
    init_pair(C_DIM, COLOR_BLUE, -1);
    init_pair(C_HP, COLOR_RED, -1);
    init_pair(C_PANEL, COLOR_CYAN, -1);
    init_pair(C_SUCCESS, COLOR_GREEN, -1);
    init_pair(C_WARNING, COLOR_MAGENTA, -1);
}

// DRAWING
void centerText(int y, const string& text) {
    int x = max(0, (COLS - static_cast<int>(text.size())) / 2);
    mvprintw(y, x, "%s", text.c_str());
}

void printColored(int y, int x, int color, const string& text) {
    attron(COLOR_PAIR(color) | A_BOLD);
    mvprintw(y, x, "%s", text.c_str());
    attroff(COLOR_PAIR(color) | A_BOLD);
}

void drawEmoji(int y, int x, const wchar_t* emoji, int color) {
    if (y < 0 || y >= LINES || x < 0 || x + 1 >= COLS) return;

    attron(COLOR_PAIR(color) | A_BOLD);
    mvaddwstr(y, x, emoji);
    attroff(COLOR_PAIR(color) | A_BOLD);
}

void drawFrame(int y, int x, int h, int w, int color) {
    if (h < 2 || w < 2) return;
    if (y < 0 || x < 0 || y + h > LINES || x + w > COLS) return;

    attron(COLOR_PAIR(color) | A_BOLD);

    mvaddch(y, x, ACS_ULCORNER);
    mvaddch(y, x + w - 1, ACS_URCORNER);
    mvaddch(y + h - 1, x, ACS_LLCORNER);
    mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);

    for (int i = 1; i < w - 1; i++) {
        mvaddch(y, x + i, ACS_HLINE);
        mvaddch(y + h - 1, x + i, ACS_HLINE);
    }

    for (int i = 1; i < h - 1; i++) {
        mvaddch(y + i, x, ACS_VLINE);
        mvaddch(y + i, x + w - 1, ACS_VLINE);
    }

    attroff(COLOR_PAIR(color) | A_BOLD);
}

// LOGO
void drawLogo() {
    const vector<string> logo = {
        " __  __    _    _____ _____   _____ ____   ____    _    ____  _____ ",
        "|  \\/  |  / \\  |__  /| ____| | ____/ ___| / ___|  / \\  |  _ \\| ____|",
        "| |\\/| | / _ \\   / / |  _|   |  _| \\___ \\| |     / _ \\ | |_) |  _|",
        "| |  | |/ ___ \\ / /_ | |___  | |___ ___) | |___ / ___ \\|  __/| |___",
        "|_|  |_/_/   \\_/____||_____| |_____|____/ \\____/_/   \\_|_|   |_____|"
    };

    int startY = 3;
    int logoWidth = 0;

     for (const string& line : logo) {
        logoWidth = max(logoWidth, (int)line.length());
    }

    int startX = max(0, (COLS - logoWidth) / 2);

    attron(COLOR_PAIR(C_TITLE) | A_BOLD);

    for (int i = 0; i < (int)logo.size(); i++) {
        mvprintw(startY + i, startX, "%s", logo[i].c_str());
    }

    attroff(COLOR_PAIR(C_TITLE) | A_BOLD);

    int centerX = COLS / 2;

    const string subtitle = "A N C I E N T   M Y S T E R Y   D U N G E O N";

int subtitleX = (COLS - static_cast<int>(subtitle.length())) / 2;

mvprintw(9, subtitleX, "%s", subtitle.c_str());
}

// MAZE GENERATION
void generateMaze(Game& game) {
    game.maze.assign(MAP_H, string(MAP_W, '#'));

    vector<Position> stack;
    stack.push_back({1, 1});
    game.maze[1][1] = ' ';

    const int dy[] = {-2, 2, 0, 0};
    const int dx[] = {0, 0, -2, 2};

    while (!stack.empty()) {
        Position current = stack.back();

        vector<int> dirs = {0, 1, 2, 3};
        shuffleVector(dirs);

        bool moved = false;

        for (int dir : dirs) {
            int ny = current.y + dy[dir];
            int nx = current.x + dx[dir];

            if (ny > 0 && ny < MAP_H - 1 &&
                nx > 0 && nx < MAP_W - 1 &&
                game.maze[ny][nx] == '#') {

                game.maze[current.y + dy[dir] / 2]
                          [current.x + dx[dir] / 2] = ' ';

                game.maze[ny][nx] = ' ';
                stack.push_back({ny, nx});

                moved = true;
                break;
            }
        }

        if (!moved) stack.pop_back();
    }

    // Create additional loops.
    for (int i = 0; i < 35 + game.level * 12; i++) {
        int y = randomInt(1, MAP_H - 2);
        int x = randomInt(1, MAP_W - 2);

        if (game.maze[y][x] != '#') continue;

        int neighbors = 0;

        if (game.maze[y - 1][x] == ' ') neighbors++;
        if (game.maze[y + 1][x] == ' ') neighbors++;
        if (game.maze[y][x - 1] == ' ') neighbors++;
        if (game.maze[y][x + 1] == ' ') neighbors++;

        if (neighbors >= 2) game.maze[y][x] = ' ';
    }

    // Expand connected rooms from existing corridors.
    for (int room = 0; room < 5; room++) {
        vector<Position> floors;

        for (int y = 2; y < MAP_H - 2; y++) {
            for (int x = 2; x < MAP_W - 2; x++) {
                if (game.maze[y][x] == ' ') {
                    floors.push_back({y, x});
                }
            }
        }

        if (floors.empty()) continue;

        Position center = floors[randomInt(0, static_cast<int>(floors.size()) - 1)];

        int ry = randomInt(1, 2);
        int rx = randomInt(2, 3);

        for (int y = center.y - ry; y <= center.y + ry; y++) {
            for (int x = center.x - rx; x <= center.x + rx; x++) {
                if (insideMap(y, x)) game.maze[y][x] = ' ';
            }
        }
    }

    game.maze[1][1] = ' ';
    game.maze[MAP_H - 2][MAP_W - 2] = ' ';
}

// OBJECT PLACEMENT
bool occupied(const Game& game, int y, int x) {
    if (y == game.playerY && x == game.playerX) return true;

    if (y == MAP_H - 2 && x == MAP_W - 2) return true;

    for (const auto& p : game.pickups) {
        if (!p.taken && p.y == y && p.x == x) return true;
    }

    for (const auto& e : game.enemies) {
        if (e.y == y && e.x == x) return true;
    }

    for (const auto& t : game.traps) {
        if (!t.activated && t.y == y && t.x == x) return true;
    }

    return false;
}

Position randomFloor(const Game& game) {
    for (int attempt = 0; attempt < 5000; attempt++) {
        int y = randomInt(1, MAP_H - 2);
        int x = randomInt(1, MAP_W - 2);

        if (game.maze[y][x] == ' ' && !occupied(game, y, x)) {
            return {y, x};
        }
    }

    for (int y = 1; y < MAP_H - 1; y++) {
        for (int x = 1; x < MAP_W - 1; x++) {
            if (game.maze[y][x] == ' ' && !occupied(game, y, x)) {
                return {y, x};
            }
        }
    }

    return {1, 1};
}

void addPickup(Game& game, char type) {
    Position p = randomFloor(game);
    game.pickups.push_back({p.y, p.x, type, false});
}

void placeObjects(Game& game) {
    game.pickups.clear();
    game.enemies.clear();
    game.traps.clear();

    game.hasKey = false;
    game.crystals = 0;
    game.requiredCrystals = 2 + game.level;

    addPickup(game, 'K');

    for (int i = 0; i < game.requiredCrystals; i++) {
        addPickup(game, 'G');
    }

    for (int i = 0; i < 7 + game.level * 2; i++) {
        addPickup(game, '$');
    }

    for (int i = 0; i < 3 + game.level; i++) {
        Position p = randomFloor(game);
        game.traps.push_back({p.y, p.x, false});
    }

    // Exactly 5 monsters per level.
    // 2 hunters and 3 patrol monsters.
    for (int i = 0; i < 5; i++) {
        Position p = randomFloor(game);
        game.enemies.push_back({p.y, p.x, i < 2});
    }
}

// LEVEL
string levelName(int level) {
    if (level == 1) return "ANCIENT ENTRANCE";
    if (level == 2) return "FORGOTTEN DUNGEON";
    return "LOST TEMPLE";
}

void loadLevel(Game& game, int level) {
    game.level = level;
    game.playerY = 1;
    game.playerX = 1;

    generateMaze(game);
    placeObjects(game);

    game.message = "Find the key, collect crystals and escape!";

    game.lastEnemyMove = steady_clock::now();
    game.lastDamage = steady_clock::now() - seconds(3);
}

void initializeGame(Game& game) {
    game.hp = game.maxHP;
    game.score = 0;
    game.coins = 0;
    game.crystals = 0;

    loadLevel(game, 1);
}

// ITEM COLLECTION
void collectItems(Game& game) {
    for (auto& item : game.pickups) {
        if (item.taken) continue;

        if (item.y == game.playerY &&
            item.x == game.playerX) {

            item.taken = true;

            switch (item.type) {
                case 'K':
                    game.hasKey = true;
                    game.score += 100;
                    game.message = "Ancient key acquired!";
                    playSound(1);
                    break;

                case 'G':
                    game.crystals++;
                    game.score += 150;
                    game.message = "Crystal collected! +150 points";
                    playSound(1);
                    break;

                case '$':
                    game.coins++;
                    game.score += 25;
                    game.message = "Treasure collected! +25 points";
                    playSound(1);
                    break;
            }
        }
    }
}

// DAMAGE
void damagePlayer(Game& game, const string& reason, int sound = 2) {
    auto now = steady_clock::now();

    if (duration_cast<milliseconds>(
        now - game.lastDamage).count() < 1100) {
        return;
    }

    game.hp--;
    game.lastDamage = now;
    game.message = reason;

    playSound(sound);
}

// PLAYER
bool exitUnlocked(const Game& game) {
    return game.hasKey &&
           game.crystals >= game.requiredCrystals;
}

void movePlayer(Game& game, int dy, int dx) {
    int ny = game.playerY + dy;
    int nx = game.playerX + dx;

    if (!insideMap(ny, nx)) return;
    if (game.maze[ny][nx] == '#') return;

    // Exit gate
    if (ny == MAP_H - 2 && nx == MAP_W - 2) {
        if (exitUnlocked(game)) {
            game.playerY = ny;
            game.playerX = nx;
            game.message = "You escaped this dungeon!";
        }
        else {
            game.message = "Find the key and all crystals first!";
        }
        return;
    }

    // Avoid walking directly into a monster.
    for (const auto& enemy : game.enemies) {
        if (enemy.y == ny && enemy.x == nx) {
            damagePlayer(game, "A monster attacked you!");
            return;
        }
    }

    game.playerY = ny;
    game.playerX = nx;

    collectItems(game);

    for (auto& trap : game.traps) {
        if (!trap.activated &&
            trap.y == game.playerY &&
            trap.x == game.playerX) {

            trap.activated = true;
            damagePlayer(game, "You stepped on a hidden trap!", 5);
            break;
        }
    }
}

// ENEMY AI
void updateEnemies(Game& game) {
    auto now = steady_clock::now();

    int interval = 850 - game.difficulty * 150;

    if (duration_cast<milliseconds>(
        now - game.lastEnemyMove).count() < interval) {
        return;
    }

    game.lastEnemyMove = now;

    const int dy[] = {-1, 1, 0, 0};
    const int dx[] = {0, 0, -1, 1};

    for (auto& enemy : game.enemies) {
        vector<int> dirs = {0, 1, 2, 3};

        if (enemy.hunter) {
            sort(dirs.begin(), dirs.end(),
                [&](int a, int b) {
                    int da = distanceBetween(
                        enemy.y + dy[a], enemy.x + dx[a],
                        game.playerY, game.playerX);

                    int db = distanceBetween(
                        enemy.y + dy[b], enemy.x + dx[b],
                        game.playerY, game.playerX);

                    return da < db;
                });
        }
        else {
            shuffleVector(dirs);
        }

        for (int dir : dirs) {
            int ny = enemy.y + dy[dir];
            int nx = enemy.x + dx[dir];

            if (!insideMap(ny, nx)) continue;
            if (game.maze[ny][nx] == '#') continue;

            if (ny == MAP_H - 2 && nx == MAP_W - 2) continue;

            if (ny == game.playerY && nx == game.playerX) {
                damagePlayer(game, "A monster caught you!");
                break;
            }

            bool blocked = false;

            for (const auto& other : game.enemies) {
                if (&other != &enemy &&
                    other.y == ny && other.x == nx) {
                    blocked = true;
                    break;
                }
            }

            if (blocked) continue;

            enemy.y = ny;
            enemy.x = nx;
            break;
        }
    }
}

// HUD
void drawHUD(const Game& game) {
    printColored(1, 3, C_TITLE, "MAZE ESCAPE");

    printColored(2, 3, C_COIN, levelName(game.level));
    printColored(3, 3, C_DIM, "ANCIENT MYSTERY DUNGEON");

    drawFrame(4, SIDE_X, 22, COLS - SIDE_X - 2, C_PANEL);

    printColored(6, SIDE_X + 2, C_TITLE, "PLAYER STATUS");

    mvprintw(8, SIDE_X + 2, "HEALTH");

    for (int i = 0; i < game.maxHP; i++) {
        if (i < game.hp) {
            attron(COLOR_PAIR(C_HP) | A_BOLD);
            mvaddch(9, SIDE_X + 2 + i * 2, ACS_DIAMOND);
            attroff(COLOR_PAIR(C_HP) | A_BOLD);
        }
        else {
            attron(COLOR_PAIR(C_DIM));
            mvaddch(9, SIDE_X + 2 + i * 2, '.');
            attroff(COLOR_PAIR(C_DIM));
        }
    }

    mvprintw(11, SIDE_X + 2, "SCORE    : %d", game.score);
    mvprintw(12, SIDE_X + 2, "TREASURE : %d", game.coins);
    mvprintw(13, SIDE_X + 2, "CRYSTAL  : %d/%d",
             game.crystals, game.requiredCrystals);

    mvprintw(14, SIDE_X + 2, "KEY      : ");

    if (game.hasKey) {
        printColored(14, SIDE_X + 13, C_SUCCESS, "YES");
    }
    else {
        printColored(14, SIDE_X + 13, C_WARNING, "NO");
    }

    printColored(16, SIDE_X + 2, C_TITLE, "OBJECTIVE");

    mvprintw(18, SIDE_X + 2, "1. Find the key");
    mvprintw(19, SIDE_X + 2, "2. Collect crystals");
    mvprintw(20, SIDE_X + 2, "3. Find the exit");

    printColored(22, SIDE_X + 2, C_COIN, "CONTROLS");
    mvprintw(23, SIDE_X + 2, "WASD / ARROWS");
    mvprintw(24, SIDE_X + 2, "P : PAUSE");
    mvprintw(25, SIDE_X + 2, "ESC : MENU");
}

// MAP RENDERING
void drawMap(const Game& game) {
    int mapScreenWidth = MAP_W * TILE_W;

    drawFrame(MAP_Y - 1, MAP_X - 1,
              MAP_H + 2, mapScreenWidth + 2, C_PANEL);

    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            int sy = MAP_Y + y;
            int sx = MAP_X + x * TILE_W;

            if (y == game.playerY && x == game.playerX) {
                drawEmoji(sy, sx, L"🐥", C_PLAYER);
                continue;
            }

            if (y == MAP_H - 2 && x == MAP_W - 2) {
                if (exitUnlocked(game)) {
                    drawEmoji(sy, sx, L"🚪", C_EXIT);
                }
                else {
                    drawEmoji(sy, sx, L"🔐", C_WARNING);
                }
                continue;
            }

            bool drawn = false;

            for (const auto& enemy : game.enemies) {
                if (enemy.y == y && enemy.x == x) {
                    drawEmoji(sy, sx,
                              enemy.hunter ? L"🕷️" : L"🦍",
                              C_ENEMY);
                    drawn = true;
                    break;
                }
            }

            if (drawn) continue;

            for (const auto& item : game.pickups) {
                if (!item.taken &&
                    item.y == y && item.x == x) {

                    if (item.type == 'K')
                        drawEmoji(sy, sx, L"🗝️", C_KEY);
                    else if (item.type == 'G')
                        drawEmoji(sy, sx, L"💎", C_CRYSTAL);
                    else
                        drawEmoji(sy, sx, L"💰", C_COIN);

                    drawn = true;
                    break;
                }
            }

            if (drawn) continue;

            for (const auto& trap : game.traps) {
                if (!trap.activated &&
                    trap.y == y && trap.x == x) {

                    drawEmoji(sy, sx, L"🐾", C_TRAP);
                    drawn = true;
                    break;
                }
            }

            if (drawn) continue;

            if (game.maze[y][x] == '#') {
                attron(COLOR_PAIR(C_WALL));
                mvaddch(sy, sx, ACS_CKBOARD);
                mvaddch(sy, sx + 1, ACS_CKBOARD);
                attroff(COLOR_PAIR(C_WALL));
            }
            else {
                mvaddstr(sy, sx, "  ");

                if ((x * 7 + y * 11 + game.level) % 31 == 0) {
                    attron(COLOR_PAIR(C_DIM));
                    mvaddch(sy, sx + 1, '.');
                    attroff(COLOR_PAIR(C_DIM));
                }
            }
        }
    }
}

// GAME SCREEN
void drawGame(const Game& game) {
    erase();

    drawHUD(game);
    drawMap(game);

    printColored(MAP_H + 7, 3, C_TITLE, "EVENT:");

    mvprintw(MAP_H + 7, 11, "%-65s",
             game.message.c_str());

    refresh();
}

// PAUSE
int pauseMenu() {
    int choice = 0;

    nodelay(stdscr, FALSE);

    vector<string> options = {
        "RESUME",
        "RESTART LEVEL",
        "MAIN MENU"
    };

    while (true) {
        erase();

        drawFrame(LINES / 2 - 5, COLS / 2 - 18,
                  12, 36, C_PANEL);

        printColored(LINES / 2 - 3,
                     COLS / 2 - 6,
                     C_TITLE, "PAUSED");

        for (int i = 0; i < 3; i++) {
            int color = (i == choice) ? C_SUCCESS : C_TEXT;

            attron(COLOR_PAIR(color) | A_BOLD);

            mvprintw(LINES / 2 + i - 1,
                     COLS / 2 - 9,
                     "%s %s",
                     i == choice ? ">" : " ",
                     options[i].c_str());

            attroff(COLOR_PAIR(color) | A_BOLD);
        }

        refresh();

        int key = getch();

        if (key == KEY_UP || key == 'w' || key == 'W')
            choice = (choice + 2) % 3;
        else if (key == KEY_DOWN || key == 's' || key == 'S')
            choice = (choice + 1) % 3;
        else if (key == 10 || key == KEY_ENTER) {
            nodelay(stdscr, TRUE);
            return choice;
        }
        else if (key == 27) {
            nodelay(stdscr, TRUE);
            return 0;
        }
    }
}

// INSTRUCTIONS
void showInstructions() {
    erase();

    drawFrame(3, 5, 25, COLS - 10, C_PANEL);

    printColored(5, 9, C_TITLE, "HOW TO PLAY");

    mvprintw(8, 9, "MOVEMENT");
    mvprintw(9, 12, "W / A / S / D or Arrow Keys");

    mvprintw(11, 9, "ITEMS");

    drawEmoji(12, 12, L"🗝️", C_KEY);
    mvprintw(12, 16, "Ancient key");

    drawEmoji(13, 12, L"💎", C_CRYSTAL);
    mvprintw(13, 16, "Crystal");

    drawEmoji(14, 12, L"💰", C_COIN);
    mvprintw(14, 16, "Treasure");

    mvprintw(16, 9, "DANGERS");

    drawEmoji(17, 12, L"🕷️", C_ENEMY);
    mvprintw(17, 16, "Hunter monster");

    drawEmoji(18, 12, L"🦍", C_ENEMY);
    mvprintw(18, 16, "Patrol monster");

    drawEmoji(19, 12, L"🐾", C_TRAP);
    mvprintw(19, 16, "Hidden trap");

    mvprintw(21, 9, "Find the key, collect all crystals, and reach the exit.");
    mvprintw(24, 9, "Press any key to return...");

    refresh();
    getch();
}

// SETTINGS
void settingsMenu(Game& game) {
    int choice = game.difficulty - 1;

    vector<string> levels = {"EASY", "NORMAL", "HARD"};

    while (true) {
        erase();

        drawFrame(LINES / 2 - 5, COLS / 2 - 20,
                  12, 40, C_PANEL);

        printColored(LINES / 2 - 3,
                     COLS / 2 - 7,
                     C_TITLE, "SETTINGS");

        mvprintw(LINES / 2 - 1,
                 COLS / 2 - 14,
                 "DIFFICULTY: %s",
                 levels[choice].c_str());

        printColored(LINES / 2 + 1,
                     COLS / 2 - 12,
                     C_SUCCESS, "<  CHANGE  >");

        mvprintw(LINES / 2 + 4,
                 COLS / 2 - 13,
                 "ENTER: CONFIRM");

        refresh();

        int key = getch();

        if (key == KEY_LEFT || key == KEY_UP ||
            key == 'a' || key == 'w') {
            choice = (choice + 2) % 3;
        }
        else if (key == KEY_RIGHT || key == KEY_DOWN ||
                 key == 'd' || key == 's') {
            choice = (choice + 1) % 3;
        }
        else if (key == 10 || key == KEY_ENTER) {
            game.difficulty = choice + 1;
            return;
        }
        else if (key == 27) {
            return;
        }
    }
}

// MAIN MENU
int mainMenu(Game& game) {
    int choice = 0;

    vector<string> options = {
        "START GAME",
        "HOW TO PLAY",
        "SETTINGS",
        "EXIT"
    };

    nodelay(stdscr, FALSE);

    while (true) {
        erase();

        drawLogo();

        attron(COLOR_PAIR(C_DIM));
        centerText(10, "A forgotten world lies beneath the ancient stones...");
        attroff(COLOR_PAIR(C_DIM));

        drawFrame(LINES / 2 - 2,
                  COLS / 2 - 19,
                  12, 38, C_PANEL);

        for (int i = 0; i < 4; i++) {
            int color = (i == choice) ? C_SUCCESS : C_TEXT;

            attron(COLOR_PAIR(color) | A_BOLD);

            mvprintw(LINES / 2 + i,
                     COLS / 2 - 10,
                     "%s %s",
                     i == choice ? ">" : " ",
                     options[i].c_str());

            attroff(COLOR_PAIR(color) | A_BOLD);
        }

        attron(COLOR_PAIR(C_DIM));
        centerText(LINES - 3, "C++ / NCURSESW | ANCIENT MYSTERY DUNGEON");
        centerText(LINES - 2, "Use ARROW KEYS and ENTER");
        attroff(COLOR_PAIR(C_DIM));

        refresh();

        int key = getch();

        if (key == KEY_UP || key == 'w' || key == 'W')
            choice = (choice + 3) % 4;
        else if (key == KEY_DOWN || key == 's' || key == 'S')
            choice = (choice + 1) % 4;
        else if (key == 10 || key == KEY_ENTER) {
            if (choice == 0) return 0;

            if (choice == 1)
                showInstructions();
            else if (choice == 2)
                settingsMenu(game);
            else if (choice == 3)
                return 1;
        }
    }
}

// END SCREENS
void gameOverScreen(const Game& game) {
    erase();

    drawFrame(LINES / 2 - 6,
              COLS / 2 - 22,
              14, 44, C_ENEMY);

    printColored(LINES / 2 - 4,
                 COLS / 2 - 5,
                 C_ENEMY, "GAME OVER");

    mvprintw(LINES / 2 - 1,
             COLS / 2 - 10,
             "FINAL SCORE: %d", game.score);

    mvprintw(LINES / 2 + 1,
             COLS / 2 - 10,
             "LEVEL: %d", game.level);

    printColored(LINES / 2 + 4,
                 COLS / 2 - 13,
                 C_TEXT, "Press any key to return");

    refresh();
    getch();
}

void victoryScreen(const Game& game) {
    erase();

    drawFrame(LINES / 2 - 7,
              COLS / 2 - 24,
              16, 48, C_SUCCESS);

    printColored(LINES / 2 - 5,
                 COLS / 2 - 7,
                 C_SUCCESS, "YOU ESCAPED!");

    attron(COLOR_PAIR(C_COIN) | A_BOLD);
    centerText(LINES / 2 - 2,
               "THE LOST TEMPLE HAS BEEN CONQUERED");
    attroff(COLOR_PAIR(C_COIN) | A_BOLD);

    mvprintw(LINES / 2 + 1,
             COLS / 2 - 10,
             "FINAL SCORE: %d", game.score);

    mvprintw(LINES / 2 + 3,
             COLS / 2 - 10,
             "TREASURE: %d", game.coins);

    mvprintw(LINES / 2 + 5,
             COLS / 2 - 13,
             "Press any key to return");

    refresh();
    getch();
}

// GAME LOOP
void playGame(Game& game) {
    initializeGame(game);

    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);

    while (true) {
        if (LINES < 29 || COLS < 120) {
            nodelay(stdscr, FALSE);
            erase();

            printColored(2, 2, C_ENEMY, "TERMINAL TOO SMALL!");

            mvprintw(4, 2,
                     "Please resize terminal to at least 120 columns x 29 rows.");

            mvprintw(6, 2, "Press any key to return...");
            refresh();
            getch();
            return;
        }

        drawGame(game);

        int key = getch();

        if (key == KEY_UP || key == 'w' || key == 'W')
            movePlayer(game, -1, 0);
        else if (key == KEY_DOWN || key == 's' || key == 'S')
            movePlayer(game, 1, 0);
        else if (key == KEY_LEFT || key == 'a' || key == 'A')
            movePlayer(game, 0, -1);
        else if (key == KEY_RIGHT || key == 'd' || key == 'D')
            movePlayer(game, 0, 1);
        else if (key == 'p' || key == 'P' || key == 27) {
            int result = pauseMenu();

            if (result == 1) {
                loadLevel(game, game.level);
            }
            else if (result == 2) {
                nodelay(stdscr, FALSE);
                return;
            }
        }

        updateEnemies(game);

        if (game.hp <= 0) {
            nodelay(stdscr, FALSE);
            gameOverScreen(game);
            return;
        }

        if (game.playerY == MAP_H - 2 &&
            game.playerX == MAP_W - 2 &&
            exitUnlocked(game)) {

            game.score += 300;

            if (game.level < TOTAL_LEVELS) {
                playSound(3);

                game.level++;
                game.hp = min(game.maxHP, game.hp + 1);

                loadLevel(game, game.level);
            }
            else {
                playSound(4);

                nodelay(stdscr, FALSE);
                victoryScreen(game);
                return;
            }
        }

        napms(25);
    }
}

// MAIN
int main() {

    setlocale(LC_ALL, "");

    // Buat semua file WAV otomatis
    createAudioFiles();

    initscr();

    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    initializeColors();

    Game game;

    bool exitGame = false;

    // Opening sound saat game dibuka
    playEffect("opening.wav");

    // Musik menu utama
    startMenuMusic();

    while (!exitGame) {

        int result = mainMenu(game);

        if (result == 1) {
            exitGame = true;
        }
        else {

            // Hentikan musik menu saat gameplay dimulai
            stopMenuMusic();

            playGame(game);

            // Musik menu kembali setelah gameplay selesai
            if (!exitGame) {
                startMenuMusic();
            }
        }
    }

    stopMenuMusic();

    endwin();

    return 0;
}