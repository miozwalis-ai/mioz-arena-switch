#include <switch.h>
#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

// ============================================================
// MIOZ ZOMBIE WORLD - v3 (pistolet + vagues + munitions)
// ============================================================

static constexpr int SW = 1280;
static constexpr int SH = 720;

static constexpr float PI        = 3.1415926535f;
static constexpr float MAP_LIMIT = 106.0f;
static constexpr float FOV_FOCAL = 650.0f;
static constexpr float NEAR_Z    = 0.18f;

static constexpr float FOG_START = 60.0f;
static constexpr float FOG_END   = 140.0f;

static constexpr unsigned STICK_LEFT  = 0;
static constexpr unsigned STICK_RIGHT = 1;

// ------------------------------------------------------------
// Structures
// ------------------------------------------------------------

struct V3 { float x, y, z; };
struct Color { Uint8 r, g, b, a; };

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
    float wobble   = 0.0f;
    bool  alive    = true;
};

struct AmmoBox {
    V3 pos;
    bool taken = false;
};

// ------------------------------------------------------------
// Math helpers
// ------------------------------------------------------------

static inline V3 add(V3 a, V3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
static inline V3 sub(V3 a, V3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
static inline V3 mul(V3 a, float s) { return {a.x*s, a.y*s, a.z*s}; }
static inline float dot(V3 a, V3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }

static inline V3 cross(V3 a, V3 b) {
    return { a.y*b.z - a.z*b.y,
             a.z*b.x - a.x*b.z,
             a.x*b.y - a.y*b.x };
}

static inline V3 norm(V3 a) {
    float l = sqrtf(dot(a,a));
    return l > 1e-6f ? mul(a, 1.0f/l) : V3{0,1,0};
}

static inline V3 rotateY(V3 p, float c, float s) {
    return { p.x*c - p.z*s, p.y, p.x*s + p.z*c };
}
static inline V3 rotateY(V3 p, float angle) {
    return rotateY(p, cosf(angle), sinf(angle));
}

// ------------------------------------------------------------
// Procedural meshes
// ------------------------------------------------------------

static Mesh makeBox(float x, float y, float z) {
    Mesh m;
    m.p = {
        {-x/2,-y/2,-z/2},{ x/2,-y/2,-z/2},{ x/2, y/2,-z/2},{-x/2, y/2,-z/2},
        {-x/2,-y/2, z/2},{ x/2,-y/2, z/2},{ x/2, y/2, z/2},{-x/2, y/2, z/2}
    };
    uint32_t f[] = {
        0,2,1, 0,3,2,  4,5,6, 4,6,7,
        0,1,5, 0,5,4,  2,3,7, 2,7,6,
        1,2,6, 1,6,5,  3,0,4, 3,4,7
    };
    m.idx.assign(f, f+36);
    return m;
}

static Mesh makeSphere(float r, int rings = 7, int seg = 10) {
    Mesh m;
    for (int y = 0; y <= rings; y++) {
        float v   = float(y)/rings;
        float phi = v*PI;
        for (int x = 0; x <= seg; x++) {
            float u = float(x)/seg;
            float a = u*2.0f*PI;
            m.p.push_back({
                r*sinf(phi)*cosf(a),
                r*cosf(phi),
                r*sinf(phi)*sinf(a)
            });
        }
    }
    for (int y = 0; y < rings; y++) {
        for (int x = 0; x < seg; x++) {
            uint32_t a = y*(seg+1)+x, b = a+seg+1;
            m.idx.insert(m.idx.end(), { a,b,a+1, a+1,b,b+1 });
        }
    }
    return m;
}

static Mesh makeCylinder(float radius, float height, int seg = 10) {
    Mesh m;
    for (int y = 0; y < 2; y++) {
        for (int i = 0; i < seg; i++) {
            float a = 2.0f*PI*i/seg;
            m.p.push_back({
                radius*cosf(a),
                y ? height/2.0f : -height/2.0f,
                radius*sinf(a)
            });
        }
    }
    for (int i = 0; i < seg; i++) {
        uint32_t n = (i+1)%seg;
        m.idx.insert(m.idx.end(), {
            (uint32_t)i, (uint32_t)(i+seg), n,
            n,           (uint32_t)(i+seg), (uint32_t)(n+seg)
        });
    }
    return m;
}

static Mesh boxMesh    = makeBox(1,1,1);
static Mesh sphereMesh = makeSphere(1);
static Mesh cylMesh    = makeCylinder(1,1);

// ============================================================
// GAME
// ============================================================

class Game {
private:
    SDL_Window*   win = nullptr;
    SDL_Renderer* ren = nullptr;
    PadState      pad{};

    V3    player{0,0,0};
    float yaw   = 0.0f;
    float pitch = 0.0f;

    // --- Combat / progression
    int   hits        = 0;
    int   score       = 0;
    int   level       = 1;
    int   kills       = 0;
    int   ammo        = 60;
    int   ballsPerKill = 5;      // 5 → 3 → 1
    float hurt        = 0.0f;
    float muzzleFlash = 0.0f;
    float shakeTimer  = 0.0f;
    float shakeAmount = 0.0f;

    float deadTimer = 0.0f;
    float timeAlive = 0.0f;

    bool dead   = false;
    bool title  = true;
    bool paused = false;

    uint32_t seed = 0x4D494F5A;

    std::vector<Zombie>  zombies;
    std::vector<Object>  objects;
    std::vector<AmmoBox> ammoBoxes;

    std::vector<Tri>         tris;
    std::vector<SDL_Vertex>  verts;
    std::vector<int>         indices;

    float camCosYaw = 1.0f, camSinYaw = 0.0f;
    float camCosPitch = 1.0f, camSinPitch = 0.0f;

    // --------------------------------------------------------
    float rand01() {
        seed = 1664525u * seed + 1013904223u;
        return float(seed & 0x00ffffff) / float(0x01000000);
    }

    void addObj(const Mesh* mesh, V3 pos, V3 scale, Color color,
                float angle = 0.0f) {
        objects.push_back({ mesh, pos, scale, angle, color });
    }

    bool tooCloseToTree(float x, float z) {
        for (const auto& o : objects) {
            if (o.mesh != &cylMesh) continue;
            float dx = o.pos.x - x;
            float dz = o.pos.z - z;
            if (dx*dx + dz*dz < 3.0f*3.0f) return true;
        }
        return false;
    }

    // --------------------------------------------------------
    void buildWorld() {
        objects.clear();
        zombies.clear();
        ammoBoxes.clear();

        // Sol immense
        addObj(&boxMesh, {0,-0.05f,0}, {1000,0.1f,1000}, {55,155,65,255});
        addObj(&boxMesh, {0,-0.04f,0}, {180,0.06f,180},  {70,145,60,255});

        // Arbres + caisses de munitions près de certains arbres
        for (int i = 0; i < 34; i++) {
            float a = rand01() * 2.0f * PI;
            float r = 22.0f + rand01() * 63.0f;
            float x = cosf(a)*r;
            float z = sinf(a)*r;

            Uint8 v1 = (Uint8)(100 + (int)(rand01()*30));
            Uint8 v2 = (Uint8)(110 + (int)(rand01()*30));

            addObj(&cylMesh,    {x,1.45f,z}, {0.65f,2.9f,0.65f}, {105,68,39,255});
            addObj(&sphereMesh, {x,3.55f,z}, {3.4f,3.5f,3.4f},   {40,v1,48,255});
            addObj(&sphereMesh, {x+0.8f,3.9f,z-0.3f}, {2.2f,2.4f,2.2f}, {48,v2,53,255});

            // 1 chance sur 3 d'avoir une caisse près de l'arbre
            if (rand01() < 0.33f) {
                float ox = x + (rand01()*2.0f-1.0f)*2.2f;
                float oz = z + (rand01()*2.0f-1.0f)*2.2f;
                ammoBoxes.push_back({ {ox, 0.25f, oz}, false });
            }
        }

        spawnInitialZombies(8);
    }

    // Spawn des zombies DERRIÈRE le joueur (hors champ)
    void spawnInitialZombies(int n) {
        for (int i = 0; i < n; i++) {
            spawnOneZombieBehindPlayer();
        }
    }

    void spawnOneZombieBehindPlayer() {
        V3 p{};
        for (int tries = 0; tries < 40; tries++) {
            // Angle par rapport au joueur : entre 100° et 260° (derrière)
            float behind = PI + (rand01() - 0.5f) * 2.6f;
            float worldA = yaw + behind;
            float r = 18.0f + rand01() * 22.0f;
            p.x = player.x + sinf(worldA) * r;
            p.z = player.z + cosf(worldA) * r;

            if (fabsf(p.x) > MAP_LIMIT - 2) continue;
            if (fabsf(p.z) > MAP_LIMIT - 2) continue;
            if (tooCloseToTree(p.x, p.z)) continue;
            break;
        }
        zombies.push_back({ p, 0.0f, rand01()*6.28f, true });
    }

    void respawn() {
        player = {0,0,0};
        yaw = 0.0f; pitch = 0.0f;
        hits = 0; score = 0; kills = 0; ammo = 60;
        level = 1; ballsPerKill = 5;
        hurt = 0.0f; timeAlive = 0.0f;
        dead = false; deadTimer = 0.0f;
        zombies.clear();
        for (auto& b : ammoBoxes) b.taken = false;
        spawnInitialZombies(8);
}// --------------------------------------------------------
void addMeshTriangles(const Object& o) {
    const V3 camPos = { player.x, 1.55f, player.z };
    const float fogR = 111, fogG = 177, fogB = 225;

    for (size_t i = 0; i + 2 < o.mesh->idx.size(); i += 3) {
        V3 wp[3];
        bool visible = true;
        float depth = 0.0f;

        for (int k = 0; k < 3; k++) {
            V3 p = o.mesh->p[o.mesh->idx[i+k]];
            p = { p.x*o.scale.x, p.y*o.scale.y, p.z*o.scale.z };
            p = rotateY(p, o.yaw);
            wp[k] = add(o.pos, p);
        }

        V3 e1 = sub(wp[1], wp[0]);
        V3 e2 = sub(wp[2], wp[0]);
        V3 nrm = cross(e1, e2);
        V3 viewDir = sub(camPos, wp[0]);
        if (dot(nrm, viewDir) <= 0.0f) continue;

        float ndl = dot(norm(nrm), norm({0.4f, 1.0f, 0.3f}));
        float lum = 0.55f + 0.45f * std::max(0.0f, ndl);

        SDL_Vertex verts3[3];
        for (int k = 0; k < 3; k++) {
            V3 rel = sub(wp[k], camPos);
            V3 cam = rotateY(rel, camCosYaw, camSinYaw);

            float yy =  cam.y*camCosPitch - cam.z*camSinPitch;
            float zz =  cam.y*camSinPitch + cam.z*camCosPitch;
            float xx =  cam.x;

            if (zz < NEAR_Z) { visible = false; break; }

            float sx = SW*0.5f + xx * FOV_FOCAL / zz;
            float sy = SH*0.5f - yy * FOV_FOCAL / zz;

            if (sx < -2000 || sx > SW+2000 || sy < -2000 || sy > SH+2000) {
                visible = false; break;
            }

            float fog = std::clamp((zz - FOG_START) / (FOG_END - FOG_START),
                                   0.0f, 1.0f);

            Uint8 cr = (Uint8)(o.color.r * lum * (1-fog) + fogR * fog);
            Uint8 cg = (Uint8)(o.color.g * lum * (1-fog) + fogG * fog);
            Uint8 cb = (Uint8)(o.color.b * lum * (1-fog) + fogB * fog);

            verts3[k].position  = { sx, sy };
            verts3[k].tex_coord = { 0, 0 };
            verts3[k].color     = { cr, cg, cb, o.color.a };
            depth += zz;
        }

        if (visible) {
            tris.push_back({
                { verts3[0], verts3[1], verts3[2] },
                depth / 3.0f
            });
        }
    }
}

void drawZombie(const Zombie& z) {
    float bob = sinf(z.wobble) * 0.06f;

    Object body { &cylMesh, {z.pos.x, 0.9f+bob, z.pos.z},
                  {0.8f, 1.5f, 0.8f}, 0, {73,145,69,255} };
    Object head { &sphereMesh, {z.pos.x, 1.95f+bob, z.pos.z},
                  {0.72f,0.72f,0.72f}, 0, {91,166,80,255} };

    V3 toP = sub(player, z.pos);
    float lyaw = atan2f(toP.x, toP.z);
    V3 fwd = { sinf(lyaw), 0, cosf(lyaw) };
    V3 rgt = { cosf(lyaw), 0, -sinf(lyaw) };

    V3 eyeBase = { z.pos.x + fwd.x*0.31f, 2.0f+bob, z.pos.z + fwd.z*0.31f };

    Object eyeL { &sphereMesh, add(eyeBase, mul(rgt, -0.20f)),
        {0.12f,0.12f,0.10f}, 0, {255,30,25,255} };
    Object eyeR { &sphereMesh, add(eyeBase, mul(rgt,  0.20f)),
        {0.12f,0.12f,0.10f}, 0, {255,30,25,255} };

    addMeshTriangles(body);
    addMeshTriangles(head);
    addMeshTriangles(eyeL);
    addMeshTriangles(eyeR);
}

void drawAmmoBox(const AmmoBox& b) {
    if (b.taken) return;
    Object o { &boxMesh, b.pos, {0.9f, 0.5f, 0.9f}, 0,
               {220, 200, 60, 255} };
    addMeshTriangles(o);
    // Petit couvercle plus clair
    Object lid { &boxMesh, {b.pos.x, b.pos.y+0.28f, b.pos.z},
                 {0.95f, 0.08f, 0.95f}, 0, {255, 240, 120, 255} };
    addMeshTriangles(lid);
}

void drawSky() {
    const int bands = 24;
    for (int i = 0; i < bands; i++) {
        float t = (float)i / bands;
        Uint8 r = (Uint8)( 90 + 40 * t);
        Uint8 g = (Uint8)(150 + 50 * t);
        Uint8 b = (Uint8)(220 + 25 * t);

        SDL_SetRenderDrawColor(ren, r, g, b, 255);
        SDL_Rect band { 0, (int)(t * SH), SW, SH / bands + 1 };
        SDL_RenderFillRect(ren, &band);
    }
}

void drawWorld() {
    camCosYaw   = cosf(yaw);
    camSinYaw   = sinf(yaw);
    camCosPitch = cosf(pitch);
    camSinPitch = sinf(pitch);

    tris.clear();
    tris.reserve(9000);

    for (const auto& o : objects)   addMeshTriangles(o);
    for (const auto& z : zombies)   if (z.alive) drawZombie(z);
    for (const auto& b : ammoBoxes) drawAmmoBox(b);

    std::sort(tris.begin(), tris.end(),
        [](const Tri& a, const Tri& b) { return a.depth > b.depth; });

    drawSky();

    verts.clear();
    indices.clear();
    verts.reserve(tris.size()*3);
    indices.reserve(tris.size()*3);

    for (const auto& t : tris) {
        int base = (int)verts.size();
        for (auto& v : t.v) verts.push_back(v);
        indices.push_back(base);
        indices.push_back(base+1);
        indices.push_back(base+2);
    }

    if (!verts.empty()) {
        SDL_RenderGeometry(ren, nullptr,
            verts.data(),   (int)verts.size(),
            indices.data(), (int)indices.size());
    }
}

// --------------------------------------------------------
void drawHud() {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    // Coeurs
    for (int i = 0; i < 3; i++) {
        SDL_Rect r { 28 + i*42, 26, 30, 24 };
        SDL_SetRenderDrawColor(ren,
            (i < hits) ? 65 : 220,
            (i < hits) ? 65 : 35,
            (i < hits) ? 65 : 45,
            255);
        SDL_RenderFillRect(ren, &r);
    }

    // Barre de niveau
    SDL_SetRenderDrawColor(ren, 30, 30, 30, 200);
    SDL_Rect lbBg { 28, 62, 220, 18 };
    SDL_RenderFillRect(ren, &lbBg);

    float prog = (kills % 5) / 5.0f;
    SDL_SetRenderDrawColor(ren, 80, 200, 255, 255);
    SDL_Rect lb { 30, 64, (int)(216 * prog), 14 };
    SDL_RenderFillRect(ren, &lb);

    // Balles restantes (barre jaune en haut à droite)
    int barW = std::min(400, ammo * 4);
    SDL_SetRenderDrawColor(ren, 255, 220, 60, 220);
    SDL_Rect tb { SW - 30 - barW, 26, barW, 12 };
    SDL_RenderFillRect(ren, &tb);

    // Crosshair
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 230);
    SDL_RenderDrawLine(ren, SW/2-10, SH/2, SW/2+10, SH/2);
    SDL_RenderDrawLine(ren, SW/2, SH/2-10, SW/2, SH/2+10);

    // Flash de bouche (petit rond jaune clignotant)
    if (muzzleFlash > 0.0f) {
        SDL_SetRenderDrawColor(ren, 255, 240, 150,
            (Uint8)(muzzleFlash * 200));
        SDL_Rect flash { SW/2-18, SH/2-18, 36, 36 };
        SDL_RenderFillRect(ren, &flash);
    }

    // Dégâts
    if (hurt > 0.0f) {
        SDL_SetRenderDrawColor(ren, 180, 0, 0, (Uint8)(hurt*115.0f));
        SDL_Rect r {0,0,SW,SH};
        SDL_RenderFillRect(ren, &r);
    }

    // Menus
    if (title || dead || paused) {
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 175);
        SDL_Rect panel { SW/2-300, SH/2-100, 600, 200 };
        SDL_RenderFillRect(ren, &panel);

        SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
        SDL_Rect bar { SW/2-170, SH/2+40, 340, 24 };
        SDL_RenderFillRect(ren, &bar);

        SDL_SetRenderDrawColor(ren, 40, 130, 50, 255);
        SDL_Rect inner { SW/2-165, SH/2+45, 330, 14 };
        SDL_RenderFillRect(ren, &inner);
    }
}    // --------------------------------------------------------
    // Tire : raycast vers le centre de l'écran
    // --------------------------------------------------------
    void fireShot() {
        if (ammo <= 0) return;
        ammo--;
        muzzleFlash = 0.15f;

        // Direction caméra (avant)
        V3 dir = {
            sinf(yaw) * cosf(pitch),
            -sinf(pitch),
            cosf(yaw) * cosf(pitch)
        };

        V3 camPos = { player.x, 1.55f, player.z };

        // Cherche le zombie le plus aligné (produit scalaire max)
        Zombie* best = nullptr;
        float bestDot = 0.985f; // tolérance d'angle

        for (auto& z : zombies) {
            if (!z.alive) continue;
            V3 toZ = {
                z.pos.x - camPos.x,
                (z.pos.y + 1.0f) - camPos.y,   // centre du zombie
                z.pos.z - camPos.z
            };
            float len = sqrtf(dot(toZ, toZ));
            if (len < 0.01f) continue;
            V3 toZn = mul(toZ, 1.0f/len);
            float d = dot(toZn, dir);
            if (d > bestDot) {
                bestDot = d;
                best = &z;
            }
        }

        if (best) {
            // Le zombie encaisse
            int needed = ballsPerKill;
            static int damageAccum = 0; // statique pour ce tir
            damageAccum++;
            if (damageAccum >= needed) {
                damageAccum = 0;
                best->alive = false;
                kills++;
                score += 100;

                // Progression de niveau
                if (kills % 5 == 0) {
                    level++;
                    if (level >= 3)      ballsPerKill = 1;
                    else if (level >= 2) ballsPerKill = 3;
                    else                 ballsPerKill = 5;
                }
            }
        }
    }

    // --------------------------------------------------------
    void update(float dt, u64 kDown, u64 kHeld) {
        if (hurt > 0.0f)        hurt        = std::max(0.0f, hurt - dt);
        if (muzzleFlash > 0.0f) muzzleFlash = std::max(0.0f, muzzleFlash - dt);
        if (shakeTimer > 0.0f)  shakeTimer  = std::max(0.0f, shakeTimer - dt);

        if (title) {
            if (kDown & HidNpadButton_A) title = false;
            return;
        }

        if (dead) {
            deadTimer -= dt;
            if (deadTimer <= 0.0f) { respawn(); title = true; }
            return;
        }

        if (kDown & HidNpadButton_Plus) paused = !paused;
        if (paused) return;

        if (kDown & HidNpadButton_B)     { title = true; return; }
        if (kDown & HidNpadButton_Minus) { respawn(); title = true; return; }

        timeAlive += dt;

        // ----------------------------------------------------
        // Sticks (corrigés : +y = stick poussé vers le haut)
        // ----------------------------------------------------
        HidAnalogStickState left  = padGetStickPos(&pad, STICK_LEFT);
        HidAnalogStickState right = padGetStickPos(&pad, STICK_RIGHT);

        float mx =  left.x / 32767.0f;
        float mz =  left.y / 32767.0f;   // haut = avancer

        if (kHeld & HidNpadButton_Left)  mx -= 1.0f;
        if (kHeld & HidNpadButton_Right) mx += 1.0f;
        if (kHeld & HidNpadButton_Up)    mz += 1.0f;
        if (kHeld & HidNpadButton_Down)  mz -= 1.0f;

        float len = sqrtf(mx*mx + mz*mz);
        if (len > 1.0f) { mx /= len; mz /= len; }

        V3 move = rotateY({ mx*7.0f*dt, 0, mz*7.0f*dt }, yaw);
        player.x = std::clamp(player.x + move.x, -MAP_LIMIT, MAP_LIMIT);
        player.z = std::clamp(player.z + move.z, -MAP_LIMIT, MAP_LIMIT);

        // Caméra
        yaw   += right.x / 32767.0f * 2.4f * dt;   // droite = regarder à droite
        pitch  = std::clamp(
            pitch - right.y / 32767.0f * 2.0f * dt,
            -1.48f, 1.48f);

        // ----------------------------------------------------
        // Tir (ZR ou A)
        // ----------------------------------------------------
        if ((kDown & HidNpadButton_ZR) || (kDown & HidNpadButton_A)) {
            fireShot();
        }

        // ----------------------------------------------------
        // Ramassage munitions (distance < 1.2)
        // ----------------------------------------------------
        for (auto& b : ammoBoxes) {
            if (b.taken) continue;
            float dx = b.pos.x - player.x;
            float dz = b.pos.z - player.z;
            if (dx*dx + dz*dz < 1.2f*1.2f) {
                b.taken = true;
                ammo = std::min(999, ammo + 20);
            }
        }

        // ----------------------------------------------------
        // Vagues : plus le temps passe, plus il y a de zombies
        // Cap à 40, spawn max 1 toutes les 1.5s
        // ----------------------------------------------------
        static float spawnCooldown = 0.0f;
        spawnCooldown -= dt;
        int targetCount = 8 + (int)(timeAlive / 6.0f);
        if (targetCount > 40) targetCount = 40;

        int aliveCount = 0;
        for (auto& z : zombies) if (z.alive) aliveCount++;

        if (aliveCount < targetCount && spawnCooldown <= 0.0f) {
            spawnOneZombieBehindPlayer();
            spawnCooldown = 1.5f;
        }

        // Nettoyage périodique des zombies morts
        if (zombies.size() > 60) {
            zombies.erase(
                std::remove_if(zombies.begin(), zombies.end(),
                    [](const Zombie& z){ return !z.alive; }),
                zombies.end());
        }

        // ----------------------------------------------------
        // IA zombies (vitesse augmente avec le temps)
        // ----------------------------------------------------
        float speedMul = 1.0f + std::min(1.5f, timeAlive / 60.0f);

        for (auto& z : zombies) {
            if (!z.alive) continue;

            z.wobble += dt * 8.0f;

            V3 d = sub(player, z.pos);
            float dist = sqrtf(d.x*d.x + d.z*d.z);

            V3 push{0,0,0};
            for (auto& o : zombies) {
                if (&o == &z || !o.alive) continue;
                V3 dd = sub(z.pos, o.pos);
                float d2 = dd.x*dd.x + dd.z*dd.z;
                if (d2 < 1.2f*1.2f && d2 > 1e-4f) {
                    float il = 1.0f / sqrtf(d2);
                    push = add(push, mul(dd, il * 0.8f));
                }
            }

            if (dist > 1.55f && dist > 0.001f) {
                float spd = 2.25f * speedMul;
                z.pos.x += (d.x / dist * spd + push.x) * dt;
                z.pos.z += (d.z / dist * spd + push.z) * dt;

                z.pos.x = std::clamp(z.pos.x, -MAP_LIMIT, MAP_LIMIT);
                z.pos.z = std::clamp(z.pos.z, -MAP_LIMIT, MAP_LIMIT);
            } else {
                z.cooldown -= dt;
                if (z.cooldown <= 0.0f) {
                    hits++;
                    hurt = 0.65f;
                    shakeTimer = 0.25f;
                    shakeAmount = 6.0f;
                    z.cooldown = 1.25f;
                    if (hits >= 3) { dead = true; deadTimer = 2.0f; }
                }
            }
        }
    }

public:

    bool init() {
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad);

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
            printf("SDL_Init error: %s\n", SDL_GetError());
            return false;
        }

        win = SDL_CreateWindow("MIOZ ZOMBIE WORLD",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            SW, SH, SDL_WINDOW_FULLSCREEN);
        if (!win) { printf("Window error: %s\n", SDL_GetError()); return false; }

        ren = SDL_CreateRenderer(win, -1,
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
        if (!ren) { printf("Renderer error: %s\n", SDL_GetError()); return false; }

        buildWorld();

        printf("MIOZ ZOMBIE WORLD v3\n");
        printf("L
