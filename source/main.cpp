
#include <switch.h>
#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

// ============================================================
// MIOZ ZOMBIE WORLD - Nintendo Switch
// libnx moderne + SDL2
// ============================================================

static constexpr int SW = 1280;
static constexpr int SH = 720;

static constexpr float PI = 3.1415926535f;
static constexpr float MAP_LIMIT = 106.0f;

static constexpr unsigned STICK_LEFT = 0;
static constexpr unsigned STICK_RIGHT = 1;

// ------------------------------------------------------------
// Structures
// ------------------------------------------------------------

struct V3 {
    float x, y, z;
};

struct Color {
    Uint8 r, g, b, a;
};

struct Tri {
    SDL_Vertex v[3];
    float depth;
};

struct Mesh {
    std::vector<V3> p;
    std::vector<uint32_t> idx;
};

struct Object {
    const Mesh* mesh;
    V3 pos;
    V3 scale;
    float yaw;
    Color color;
};

struct Zombie {
    V3 pos;
    float cooldown = 0.0f;
    bool alive = true;
};

// ------------------------------------------------------------
// Math helpers
// ------------------------------------------------------------

static V3 add(V3 a, V3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

static V3 sub(V3 a, V3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

static V3 mul(V3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}

static float dot(V3 a, V3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static V3 rotateY(V3 p, float angle) {
    float c = cosf(angle);
    float s = sinf(angle);

    return {
        p.x * c - p.z * s,
        p.y,
        p.x * s + p.z * c
    };
}

// ------------------------------------------------------------
// Procedural meshes
// Aucun fichier de texture externe nécessaire.
// ------------------------------------------------------------

static Mesh makeBox(float x, float y, float z) {
    Mesh m;

    m.p = {
        {-x/2, -y/2, -z/2},
        { x/2, -y/2, -z/2},
        { x/2,  y/2, -z/2},
        {-x/2,  y/2, -z/2},

        {-x/2, -y/2,  z/2},
        { x/2, -y/2,  z/2},
        { x/2,  y/2,  z/2},
        {-x/2,  y/2,  z/2}
    };

    uint32_t faces[] = {
        0,2,1, 0,3,2,
        4,5,6, 4,6,7,
        0,1,5, 0,5,4,
        2,3,7, 2,7,6,
        1,2,6, 1,6,5,
        3,0,4, 3,4,7
    };

    m.idx.assign(faces, faces + 36);
    return m;
}

static Mesh makeSphere(float r, int rings = 7, int seg = 10) {
    Mesh m;

    for (int y = 0; y <= rings; y++) {
        float v = float(y) / rings;
        float phi = v * PI;

        for (int x = 0; x <= seg; x++) {
            float u = float(x) / seg;
            float a = u * 2.0f * PI;

            m.p.push_back({
                r * sinf(phi) * cosf(a),
                r * cosf(phi),
                r * sinf(phi) * sinf(a)
            });
        }
    }

    for (int y = 0; y < rings; y++) {
        for (int x = 0; x < seg; x++) {
            uint32_t a = y * (seg + 1) + x;
            uint32_t b = a + seg + 1;

            m.idx.insert(m.idx.end(), {
                a, b, a + 1,
                a + 1, b, b + 1
            });
        }
    }

    return m;
}

static Mesh makeCylinder(float radius, float height, int seg = 10) {
    Mesh m;

    for (int y = 0; y < 2; y++) {
        for (int i = 0; i < seg; i++) {
            float a = 2.0f * PI * i / seg;

            m.p.push_back({
                radius * cosf(a),
                y ? height / 2.0f : -height / 2.0f,
                radius * sinf(a)
            });
        }
    }

    for (int i = 0; i < seg; i++) {
        uint32_t n = (i + 1) % seg;

        m.idx.insert(m.idx.end(), {
            (uint32_t)i,
            (uint32_t)(i + seg),
            n,

            n,
            (uint32_t)(i + seg),
            (uint32_t)(n + seg)
        });
    }

    return m;
}

static Mesh boxMesh = makeBox(1, 1, 1);
static Mesh sphereMesh = makeSphere(1);
static Mesh cylMesh = makeCylinder(1, 1);

// ============================================================
// GAME
// ============================================================

class Game {
private:

    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;

    // Nouvelle API manette libnx
    PadState pad{};

    V3 player{0, 0, 0};

    float yaw = 0.0f;
    float pitch = 0.0f;

    int hits = 0;

    float hurt = 0.0f;
    float deadTimer = 0.0f;

    bool dead = false;
    bool title = true;
    bool paused = false;

    uint32_t seed = 0x4D494F5A;

    std::vector<Zombie> zombies;
    std::vector<Object> objects;

    // --------------------------------------------------------
    // Random
    // --------------------------------------------------------

    float rand01() {
        seed = 1664525u * seed + 1013904223u;

        return float(seed & 0x00ffffff) /
               float(0x01000000);
    }

    // --------------------------------------------------------
    // Add object
    // --------------------------------------------------------

    void addObj(
        const Mesh* mesh,
        V3 pos,
        V3 scale,
        Color color,
        float angle = 0.0f
    ) {
        objects.push_back({
            mesh,
            pos,
            scale,
            angle,
            color
        });
    }

    // --------------------------------------------------------
    // Build world
    // --------------------------------------------------------

    void buildWorld() {
        objects.clear();
        zombies.clear();

        // Grand terrain vert
        addObj(
            &boxMesh,
            {0, -0.30f, 0},
            {220, 0.5f, 220},
            {75, 142, 65, 255}
        );

        // Arbres procéduraux
        for (int i = 0; i < 34; i++) {
            float a = rand01() * 2.0f * PI;
            float r = 22.0f + rand01() * 63.0f;

            float x = cosf(a) * r;
            float z = sinf(a) * r;

            // Tronc
            addObj(
                &cylMesh,
                {x, 1.45f, z},
                {0.65f, 2.9f, 0.65f},
                {105, 68, 39, 255}
            );

            // Feuillage principal
            addObj(
                &sphereMesh,
                {x, 3.55f, z},
                {3.4f, 3.5f, 3.4f},
                {40, 112, 48, 255}
            );

            // Feuillage secondaire
            addObj(
                &sphereMesh,
                {x + 0.8f, 3.9f, z - 0.3f},
                {2.2f, 2.4f, 2.2f},
                {48, 130, 53, 255}
            );
        }

        // Sept zombies répartis autour du joueur
        for (int i = 0; i < 7; i++) {
            float a = rand01() * 2.0f * PI;
            float r = 12.0f + rand01() * 11.0f;

            zombies.push_back({
                {cosf(a) * r, 0, sinf(a) * r},
                0.0f,
                true
            });
        }
    }

    // --------------------------------------------------------
    // Respawn
    // --------------------------------------------------------

    void respawn() {
        player = {0, 0, 0};

        yaw = 0.0f;
        pitch = 0.0f;

        hits = 0;
        hurt = 0.0f;

        dead = false;
        deadTimer = 0.0f;

        for (auto& z : zombies) {
            z.alive = true;
            z.cooldown = 0.0f;
        }
    }

    // --------------------------------------------------------
    // 3D projection
    // --------------------------------------------------------

    void addMeshTriangles(
        const Object& o,
        std::vector<Tri>& out
    ) {
        for (
            size_t i = 0;
            i + 2 < o.mesh->idx.size();
            i += 3
        ) {
            V3 world[3];
            SDL_Vertex verts[3];

            float depth = 0.0f;
            bool visible = true;

            for (int k = 0; k < 3; k++) {
                V3 p = o.mesh->p[o.mesh->idx[i + k]];

                p = {
                    p.x * o.scale.x,
                    p.y * o.scale.y,
                    p.z * o.scale.z
                };

                p = rotateY(p, o.yaw);
                world[k] = add(o.pos, p);

                V3 rel = sub(
                    world[k],
                    {player.x, 1.55f, player.z}
                );

                V3 cam = rotateY(rel, -yaw);

                float cy = cosf(pitch);
                float sy = sinf(pitch);

                float yy = cam.y * cy - cam.z * sy;
                float zz = cam.y * sy + cam.z * cy;
                float xx = cam.x;

                if (zz < 0.18f) {
                    visible = false;
                    break;
                }

                float focal = 650.0f;

                float sx = SW * 0.5f + xx * focal / zz;
                float sy2 = SH * 0.5f - yy * focal / zz;

                verts[k].position = {sx, sy2};
                verts[k].tex_coord = {0, 0};

                verts[k].color = {
                    o.color.r,
                    o.color.g,
                    o.color.b,
                    o.color.a
                };

                depth += zz;
            }

            if (visible) {
                out.push_back({
                    {verts[0], verts[1], verts[2]},
                    depth / 3.0f
                });
            }
        }
    }

    // --------------------------------------------------------
    // Draw world
    // --------------------------------------------------------

    void drawWorld() {
        std::vector<Tri> tris;
        tris.reserve(7000);

        for (const auto& o : objects) {
            addMeshTriangles(o, tris);
        }

        // Zombies
        for (const auto& z : zombies) {
            if (!z.alive)
                continue;

            Object body {
                &cylMesh,
                {z.pos.x, 0.9f, z.pos.z},
                {0.8f, 1.5f, 0.8f},
                0,
                {73, 145, 69, 255}
            };

            Object head {
                &sphereMesh,
                {z.pos.x, 1.95f, z.pos.z},
                {0.72f, 0.72f, 0.72f},
                0,
                {91, 166, 80, 255}
            };

            Object eye1 {
                &sphereMesh,
                {z.pos.x - 0.20f, 2.0f, z.pos.z - 0.31f},
                {0.12f, 0.12f, 0.08f},
                0,
                {255, 30, 25, 255}
            };

            Object eye2 {
                &sphereMesh,
                {z.pos.x + 0.20f, 2.0f, z.pos.z - 0.31f},
                {0.12f, 0.12f, 0.08f},
                0,
                {255, 30, 25, 255}
            };

            addMeshTriangles(body, tris);
            addMeshTriangles(head, tris);
            addMeshTriangles(eye1, tris);
            addMeshTriangles(eye2, tris);
        }

        // Triangles éloignés dessinés en premier
        std::sort(
            tris.begin(),
            tris.end(),
            [](const Tri& a, const Tri& b) {
                return a.depth > b.depth;
            }
        );

        SDL_SetRenderDrawColor(
            ren, 111, 177, 225, 255
        );

        SDL_RenderClear(ren);

        std::vector<SDL_Vertex> verts;
        std::vector<int> indices;

        verts.reserve(tris.size() * 3);
        indices.reserve(tris.size() * 3);

        for (const auto& t : tris) {
            int base = (int)verts.size();

            for (auto v : t.v) {
                verts.push_back(v);
            }

            indices.push_back(base);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
        }

        if (!verts.empty()) {
            SDL_RenderGeometry(
                ren,
                nullptr,
                verts.data(),
                (int)verts.size(),
                indices.data(),
                (int)indices.size()
            );
        }
    }

    // --------------------------------------------------------
    // HUD
    // --------------------------------------------------------

    void drawHud() {
        SDL_SetRenderDrawBlendMode(
            ren,
            SDL_BLENDMODE_BLEND
        );

        // Vie
        for (int i = 0; i < 3; i++) {
            SDL_Rect r {
                28 + i * 42,
                26,
                30,
                24
            };

            if (i < hits) {
                SDL_SetRenderDrawColor(
                    ren, 65, 65, 65, 255
                );
            } else {
                SDL_SetRenderDrawColor(
                    ren, 220, 35, 45, 255
                );
            }

            SDL_RenderFillRect(ren, &r);
        }

        // Crosshair
        SDL_SetRenderDrawColor(
            ren, 255, 255, 255, 230
        );

        SDL_RenderDrawLine(
            ren,
            SW / 2 - 10,
            SH / 2,
            SW / 2 + 10,
            SH / 2
        );

        SDL_RenderDrawLine(
            ren,
            SW / 2,
            SH / 2 - 10,
            SW / 2,
            SH / 2 + 10
        );

        // Flash rouge quand le joueur reçoit un coup
        if (hurt > 0.0f) {
            SDL_SetRenderDrawColor(
                ren,
                180,
                0,
                0,
                (Uint8)(hurt * 115.0f)
            );

            SDL_Rect r {0, 0, SW, SH};
            SDL_RenderFillRect(ren, &r);
        }

        // Menu / pause / mort
        if (title || dead || paused) {
            SDL_SetRenderDrawColor(
                ren, 0, 0, 0, 175
            );

            SDL_Rect panel {
                SW / 2 - 260,
                SH / 2 - 75,
                520,
                150
            };

            SDL_RenderFillRect(ren, &panel);

            SDL_SetRenderDrawColor(
                ren, 255, 255, 255, 255
            );

            SDL_Rect bar {
                SW / 2 - 170,
                SH / 2 - 12,
                340,
                24
            };

            SDL_RenderFillRect(ren, &bar);

            SDL_SetRenderDrawColor(
                ren, 40, 130, 50, 255
            );

            SDL_Rect inner {
                SW / 2 - 165,
                SH / 2 - 7,
                330,
                14
            };

            SDL_RenderFillRect(ren, &inner);
        }
    }

    // --------------------------------------------------------
    // Update game
    // --------------------------------------------------------

    void update(float dt, u64 kDown, u64 kHeld) {
        if (hurt > 0.0f) {
            hurt = std::max(0.0f, hurt - dt);
        }

        // Écran de départ
        if (title) {
            if (kDown & HidNpadButton_A) {
                title = false;
            }

            return;
        }

        // Mort
        if (dead) {
            deadTimer -= dt;

            if (deadTimer <= 0.0f) {
                respawn();
                title = true;
            }

            return;
        }

        // Pause avec +
        if (kDown & HidNpadButton_Plus) {
            paused = !paused;
        }

        if (paused) {
            return;
        }

        // Retour au menu avec B
        if (kDown & HidNpadButton_B) {
            title = true;
            return;
        }

        // Réinitialisation avec -
        if (kDown & HidNpadButton_Minus) {
            respawn();
            title = true;
            return;
        }

        // ----------------------------------------------------
        // Joysticks modernes libnx
        // ----------------------------------------------------

        HidAnalogStickState left =
            padGetStickPos(&pad, STICK_LEFT);

        HidAnalogStickState right =
            padGetStickPos(&pad, STICK_RIGHT);

        float mx = left.x / 32767.0f;
        float mz = -left.y / 32767.0f;

        // D-pad : déplacements complémentaires
        if (kHeld & HidNpadButton_Left)
            mx -= 1.0f;

        if (kHeld & HidNpadButton_Right)
            mx += 1.0f;

        if (kHeld & HidNpadButton_Up)
            mz -= 1.0f;

        if (kHeld & HidNpadButton_Down)
            mz += 1.0f;

        float len = sqrtf(mx * mx + mz * mz);

        if (len > 1.0f) {
            mx /= len;
            mz /= len;
        }

        // Déplacement du joueur
        V3 move = rotateY(
            {
                mx * 7.0f * dt,
                0,
                mz * 7.0f * dt
            },
            yaw
        );

        // Limites invisibles de la carte
        player.x = std::clamp(
            player.x + move.x,
            -MAP_LIMIT,
            MAP_LIMIT
        );

        player.z = std::clamp(
            player.z + move.z,
            -MAP_LIMIT,
            MAP_LIMIT
        );

        // Caméra avec joystick droit
        yaw -= right.x / 32767.0f * 2.4f * dt;

        pitch = std::clamp(
            pitch + right.y / 32767.0f * 2.0f * dt,
            -1.48f,
            1.48f
        );

        // ----------------------------------------------------
        // IA des zombies
        // ----------------------------------------------------

        for (auto& z : zombies) {
            if (!z.alive)
                continue;

            V3 d = sub(player, z.pos);

            float dist = sqrtf(
                d.x * d.x +
                d.z * d.z
            );

            if (dist > 1.55f && dist > 0.001f) {
                z.pos.x +=
                    d.x / dist * 2.25f * dt;

                z.pos.z +=
                    d.z / dist * 2.25f * dt;
            } else {
                z.cooldown -= dt;

                if (z.cooldown <= 0.0f) {
                    hits++;
                    hurt = 0.65f;
                    z.cooldown = 1.25f;

                    if (hits >= 3) {
                        dead = true;
                        deadTimer = 2.0f;
                    }
                }
            }
        }
    }

public:

    // --------------------------------------------------------
    // Initialize
    // --------------------------------------------------------

    bool init() {

        // Configuration manette moderne
        padConfigureInput(
            1,
            HidNpadStyleSet_NpadStandard
        );

        // Initialisation de la manette par défaut
        padInitializeDefault(&pad);

        // SDL2
        if (SDL_Init(
            SDL_INIT_VIDEO |
            SDL_INIT_GAMECONTROLLER
        ) != 0) {
            printf("SDL_Init error: %s\n", SDL_GetError());
            return false;
        }

        win = SDL_CreateWindow(
            "MIOZ ZOMBIE WORLD",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            SW,
            SH,
            SDL_WINDOW_FULLSCREEN
        );

        if (!win) {
            printf("Window error: %s\n", SDL_GetError());
            return false;
        }

        ren = SDL_CreateRenderer(
            win,
            -1,
            SDL_RENDERER_ACCELERATED
        );

        if (!ren) {
            ren = SDL_CreateRenderer(
                win,
                -1,
                SDL_RENDERER_SOFTWARE
            );
        }

        if (!ren) {
            printf("Renderer error: %s\n", SDL_GetError());
            return false;
        }

        buildWorld();

        printf("MIOZ ZOMBIE WORLD\n");
        printf("Left stick: move\n");
        printf("Right stick: camera\n");
        printf("A: start\n");
        printf("B: menu\n");
        printf("PLUS: pause\n");
        printf("MINUS: restart\n");

        return true;
    }

    // --------------------------------------------------------
    // Main loop
    // --------------------------------------------------------

    void run() {
        bool quit = false;

        Uint64 last = SDL_GetPerformanceCounter();

        while (appletMainLoop() && !quit) {

            // Mise à jour de la manette
            padUpdate(&pad);

            // Boutons pressés pendant cette frame
            u64 kDown = padGetButtonsDown(&pad);

        
