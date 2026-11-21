#include <switch.h>
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

static constexpr int SW = 1280;
static constexpr int SH = 720;
static constexpr float PI = 3.1415926535f;
static constexpr float MAP_LIMIT = 106.0f;
static constexpr float FOV_FOCAL = 650.0f;
static constexpr float NEAR_Z = 0.05f;
static constexpr float FOG_START = 55.0f, FOG_END = 150.0f;

// Brouillard vert-bleu naturel (assorti au ciel ET a l'herbe)
static constexpr float FOG_R = 118.0f, FOG_G = 172.0f, FOG_B = 195.0f;

static constexpr int MAX_HEARTS = 10;
static constexpr unsigned STICK_LEFT = 0, STICK_RIGHT = 1;

// ------------------------------------------------------------
// REGLAGE DES STICKS : si une direction va a l'envers chez toi,
// change juste 1.0f en -1.0f sur la ligne concernee.
// ------------------------------------------------------------
static constexpr float MOVE_X_SIGN = 1.0f;  // stick gauche X : + = droite
static constexpr float MOVE_Z_SIGN = 1.0f;  // stick gauche Y : + = avancer
static constexpr float CAM_X_SIGN  = 1.0f;  // stick droit  X : + = droite
static constexpr float CAM_Y_SIGN  = 1.0f;  // stick droit  Y : + = haut

struct V3 { float x, y, z; };
struct Color { Uint8 r, g, b, a; };
struct Tri { SDL_Vertex v[3]; float depth; };

// Mesh avec couleurs par sommet (pour les blocs type Minecraft)
struct Mesh {
    std::vector<V3> p;
    std::vector<uint32_t> idx;
    std::vector<Color> vc;
};

struct Object { const Mesh* mesh; V3 pos; V3 scale; float yaw; Color color; };

enum ZombieType { ZT_WALKER = 0, ZT_RUNNER = 1 };
struct Zombie {
    V3 pos; Color skin {105,160,75,255};
    float cooldown = 0.0f, wobble = 0.0f, deathTimer = 0.0f, deathTilt = 0.0f, speed = 2.2f;
    ZombieType type = ZT_WALKER; bool alive = true, dying = false; int hp = 3;
};
struct AmmoBox { V3 pos; bool taken = false; };
struct Particle { V3 pos, vel; float life, maxLife; Color color; };
struct Cloud { V3 pos; V3 scale; float speed; };

static inline V3 add(V3 a, V3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
static inline V3 sub(V3 a, V3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
static inline V3 mul(V3 a, float s) { return {a.x*s, a.y*s, a.z*s}; }
static inline float dot(V3 a, V3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static inline V3 cross(V3 a, V3 b) { return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x}; }
static inline V3 norm(V3 a) { float l = sqrtf(dot(a,a)); return l > 1e-6f ? mul(a, 1.0f/l) : V3{0,1,0}; }
static inline V3 rotateY(V3 p, float c, float s) { return { p.x*c - p.z*s, p.y, p.x*s + p.z*c }; }
static inline V3 rotateY(V3 p, float angle) { return rotateY(p, cosf(angle), sinf(angle)); }
static inline V3 rotateYInv(V3 p, float c, float s) { return { p.x*c + p.z*s, p.y, -p.x*s + p.z*c }; }

// ------------------------------------------------------------
// Meshes
// ------------------------------------------------------------

// Cube uni (feuilles, eau, decor...)
static Mesh makeBox() {
    Mesh m;
    m.p = { {-0.5f,-0.5f,-0.5f},{0.5f,-0.5f,-0.5f},{0.5f,0.5f,-0.5f},{-0.5f,0.5f,-0.5f},
            {-0.5f,-0.5f, 0.5f},{0.5f,-0.5f, 0.5f},{0.5f,0.5f, 0.5f},{-0.5f,0.5f, 0.5f} };
    uint32_t f[] = { 0,2,1, 0,3,2, 4,5,6, 4,6,7, 0,1,5, 0,5,4, 2,3,7, 2,7,6, 1,2,6, 1,6,5, 3,0,4, 3,4,7 };
    m.idx.assign(f, f+36); return m;
}

// Cube Minecraft : chaque face a sa couleur.
// sideTop = frange verte en haut des faces laterales (bloc d'herbe).
// Si sideTop.a == 0, les faces laterales sont unies (side).
static Mesh makeBlock(Color top, Color side, Color bottom, Color sideTop = {0,0,0,0}) {
    Mesh m;
    const float h = 0.5f;
    // 24 sommets, 4 par face, dans le meme ordre que makeBox
    V3 base[8] = {
        {-h,-h,-h},{ h,-h,-h},{ h, h,-h},{-h, h,-h},
        {-h,-h, h},{ h,-h, h},{ h, h, h},{-h, h, h}
    };
    // faces : {indices (comme makeBox), type de couleur}
    // type 0=side, 1=top, 2=bottom
    uint32_t faces[12][4] = {
        {0,2,1, 0},{0,3,2, 0},   // -z  side
        {4,5,6, 0},{4,6,7, 0},   // +z  side
        {0,1,5, 2},{0,5,4, 2},   // -y  bottom
        {2,3,7, 1},{2,7,6, 1},   // +y  top
        {1,2,6, 0},{1,6,5, 0},   // +x  side
        {3,0,4, 0},{3,4,7, 0}    // -x  side
    };
    for (int fi = 0; fi < 12; fi++) {
        int type = (int)faces[fi][3];
        Color c = type == 1 ? top : (type == 2 ? bottom : side);
        for (int k = 0; k < 3; k++) {
            m.p.push_back(base[faces[fi][k]]);
            m.vc.push_back(c);
            m.idx.push_back((uint32_t)m.p.size()-1);
        }
    }
    // Frange verte en haut des faces laterales (bloc d'herbe)
    if (sideTop.a != 0) {
        for (size_t i = 0; i < m.p.size(); i += 3) {
            // sommets du haut = ceux dont y > 0
            for (size_t k = i; k < i+3; k++)
                if (m.p[k].y > 0.0f) m.vc[k] = sideTop;
        }
    }
    return m;
}

// Blocs Minecraft
static Mesh boxMesh   = makeBox();
static Mesh grassMesh = makeBlock({95,160,70,255},  {134,96,67,255},  {110,80,55,255}, {95,160,70,255});
static Mesh logMesh   = makeBlock({168,136,84,255}, {104,78,46,255},  {104,78,46,255});

// ------------------------------------------------------------
// Chiffres et coeurs du HUD
// ------------------------------------------------------------
static const uint8_t DIGITS[10][7] = { {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C} };
static void drawDigit(SDL_Renderer* r, int digit, int x, int y, int px, Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    for (int row = 0; row < 7; row++) { uint8_t bits = DIGITS[digit][row]; for (int col = 0; col < 5; col++) if (bits & (1 << (4 - col))) { SDL_Rect rect { x + col*px, y + row*px, px, px }; SDL_RenderFillRect(r, &rect); } }
}
static void drawNumber(SDL_Renderer* r, int value, int x, int y, int px, Color c) {
    if (value == 0) { drawDigit(r, 0, x, y, px, c); return; }
    int digits[10], n = 0; while (value > 0 && n < 10) { digits[n++] = value % 10; value /= 10; }
    for (int i = 0; i < n; i++) drawDigit(r, digits[n-1-i], x + i*(px*6), y, px, c);
}
static void drawHeart(SDL_Renderer* r, int cx, int cy, int size, Color c, bool filled) {
    int half = size / 2, rr = half / 2; SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    if (filled) {
        for (int dy = -rr; dy <= rr; dy++) for (int dx = -rr; dx <= rr; dx++) { if (dx*dx + dy*dy <= rr*rr) { SDL_Rect px { cx - rr + dx, cy - rr/2 + dy, 1, 1 }; SDL_RenderFillRect(r, &px); SDL_Rect px2{ cx + rr + dx, cy - rr/2 + dy, 1, 1 }; SDL_RenderFillRect(r, &px2); } }
        for (int row = 0; row < half; row++) { int w = half - row; SDL_Rect line { cx - w, cy + row - 1, w*2, 1 }; SDL_RenderFillRect(r, &line); }
    } else { Color dark { (Uint8)(c.r/2), (Uint8)(c.g/2), (Uint8)(c.b/2), c.a }; SDL_SetRenderDrawColor(r, dark.r, dark.g, dark.b, dark.a); for (int row = 0; row < half; row++) { int w = half - row; SDL_Rect line { cx - w, cy + row - 1, w*2, 1 }; SDL_RenderFillRect(r, &line); } }
}
class Game {
private:
    SDL_Window* win = nullptr; SDL_Renderer* ren = nullptr; PadState pad{};
    V3 player{0,0,0}; float yaw = 0.0f, pitch = 0.0f;
    int hearts = MAX_HEARTS, score = 0, level = 1, kills = 0, ammo = 500;
    float shootCooldown = 0.0f; const float SHOOT_INTERVAL = 0.07f;
    float hurt = 0.0f, muzzleFlash = 0.0f, deadTimer = 0.0f, timeAlive = 0.0f;
    bool dead = false, title = true, paused = false; uint32_t seed = 0x4D494F5A;
    float weaponBob = 0.0f, recoil = 0.0f;

    std::vector<Zombie> zombies; std::vector<Object> objects; std::vector<AmmoBox> ammoBoxes;
    std::vector<Particle> particles; std::vector<Cloud> clouds; std::vector<V3> treePositions;
    std::vector<Tri> tris; std::vector<SDL_Vertex> verts; std::vector<int> indices;
    float camCosYaw = 1.0f, camSinYaw = 0.0f, camCosPitch = 1.0f, camSinPitch = 0.0f;

    float rand01() { seed = 1664525u * seed + 1013904223u; return float(seed & 0x00ffffff) / float(0x01000000); }
    void addObj(const Mesh* mesh, V3 pos, V3 scale, Color color, float angle = 0.0f) { objects.push_back({ mesh, pos, scale, angle, color }); }

    float riverXAt(float z) { return 18.0f + sinf(z * 0.08f) * 6.0f; }

    bool tooCloseToTree(float x, float z) {
        for (const auto& t : treePositions) {
            float dx = t.x - x, dz = t.z - z;
            if (dx*dx + dz*dz < 3.0f*3.0f) return true;
        }
        return false;
    }

    void spawnOneZombieBehindPlayer() {
        V3 p{0,0,0};
        for (int tries = 0; tries < 40; tries++) {
            float behind = PI + (rand01() - 0.5f) * 2.6f, worldA = yaw + behind, r = 18.0f + rand01() * 22.0f;
            p.x = player.x + sinf(worldA) * r; p.z = player.z + cosf(worldA) * r;
            if (fabsf(p.x) > MAP_LIMIT - 2 || fabsf(p.z) > MAP_LIMIT - 2) continue;
            if (tooCloseToTree(p.x, p.z)) continue; break;
        }
        Zombie z; z.pos = p;
        Uint8 gv = (Uint8)(130 + (int)(rand01()*40));
        z.skin = { (Uint8)(gv-45), gv, (Uint8)(gv-70), 255 };
        z.wobble = rand01()*6.28f; z.hp = 3;
        if (rand01() < 0.30f) { z.type = ZT_RUNNER; z.speed = 4.0f + rand01() * 0.8f; }
        else { z.type = ZT_WALKER; z.speed = 1.6f + rand01() * 0.8f; }
        zombies.push_back(z);
    }

    void buildWorld() {
        objects.clear(); zombies.clear(); ammoBoxes.clear(); particles.clear(); clouds.clear(); treePositions.clear();

        // SOL : dalles de bloc d'herbe Minecraft (dessus vert, cotes terre)
        for (float gx = -120.0f; gx < 120.0f; gx += 20.0f) {
            for (float gz = -120.0f; gz < 120.0f; gz += 20.0f) {
                Uint8 g = (Uint8)(225 + (int)(rand01()*30));   // leger jitter de teinte
                addObj(&grassMesh, {gx, -0.05f, gz}, {20.0f, 0.1f, 20.0f}, {g, g, g, 255});
            }
        }

        // RIVIERE : lit de sable + eau bleue translucide
        for (float z = -MAP_LIMIT; z < MAP_LIMIT; z += 10.0f) {
            float rx = riverXAt(z);
            addObj(&boxMesh, {rx, 0.005f, z}, {8.5f, 0.05f, 10.0f}, {219,207,160,255});
            addObj(&boxMesh, {rx, 0.04f, z}, {6.0f, 0.06f, 10.0f}, {70,130,220,190});
        }

        // HERBES HAUTES ET FLEURS
        for (int i = 0; i < 90; i++) {
            float x = (rand01()*2.0f-1.0f)*100.0f, z = (rand01()*2.0f-1.0f)*100.0f;
            if (fabsf(x - riverXAt(z)) < 7.5f) continue;
            if (tooCloseToTree(x, z)) continue;
            if (rand01() < 0.6f) {
                Uint8 g = (Uint8)(130 + (int)(rand01()*50));
                addObj(&boxMesh, {x, 0.18f, z}, {0.16f, 0.36f, 0.16f}, {50, g, 45, 255});
            } else {
                static const Color petals[4] = { {220,60,60,255},{240,220,80,255},{240,240,240,255},{230,140,200,255} };
                Color pc = petals[(int)(rand01()*4.0f)];
                addObj(&boxMesh, {x, 0.14f, z}, {0.06f, 0.28f, 0.06f}, {60,120,50,255});
                addObj(&boxMesh, {x, 0.34f, z}, {0.18f, 0.16f, 0.18f}, pc);
            }
        }

        // ARBRES MINECRAFT : buches + feuilles en cubes
        for (int i = 0; i < 18; i++) {
            float a = rand01()*2.0f*PI, r = 22.0f + rand01()*63.0f, x = cosf(a)*r, z = sinf(a)*r;
            if (fabsf(x - riverXAt(z)) < 7.5f) continue;
            if (tooCloseToTree(x, z)) continue;
            treePositions.push_back({x, 0, z});

            float s = 0.8f;
            int trunkN = 2 + (int)(rand01()*2.0f);   // 2 a 3 buches
            for (int t = 0; t < trunkN; t++)
                addObj(&logMesh, {x, s*0.5f + t*s, z}, {s, s, s}, {255,255,255,255});
            float topY = trunkN * s;

            auto leaf = [&](float lx, float ly, float lz) {
                Uint8 g = (Uint8)(105 + (int)(rand01()*35));
                addObj(&boxMesh,
                    {x + lx + (rand01()-0.5f)*0.08f, ly, z + lz + (rand01()-0.5f)*0.08f},
                    {1.0f, 1.0f, 1.0f},
                    { (Uint8)(g-55), g, (Uint8)(g-65), 255 });
            };
            // Etage bas : anneau 3x3 autour du tronc
            for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
                if (dx == 0 && dz == 0) continue;
                leaf((float)dx, topY - 0.1f, (float)dz);
            }
            // Etage milieu : 3x3 sans les coins
            for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
                if (abs(dx) + abs(dz) == 2) continue;
                leaf((float)dx, topY + 0.9f, (float)dz);
            }
            // Etage haut : croix
            leaf(0, topY + 1.9f, 0); leaf(1, topY + 1.9f, 0); leaf(-1, topY + 1.9f, 0);
            leaf(0, topY + 1.9f, 1); leaf(0, topY + 1.9f, -1);

            // Ombre portee
            addObj(&boxMesh, {x, 0.015f, z}, {4.5f, 0.02f, 4.5f}, {0,0,0,110});

            if (rand01() < 0.3f) ammoBoxes.push_back({ {x + (rand01()*2.0f-1.0f)*2.2f, 0.25f, z + (rand01()*2.0f-1.0f)*2.2f}, false });
        }

        // Coffres pres du spawn
        ammoBoxes.push_back({ {3.0f, 0.3f, 4.0f}, false });
        ammoBoxes.push_back({ {-4.0f, 0.3f, -3.0f}, false });

        for (int i = 0; i < 20; i++) clouds.push_back({ { (rand01()*2.0f-1.0f)*300.0f, 35.0f + rand01()*25.0f, (rand01()*2.0f-1.0f)*300.0f }, { 15.0f + rand01()*20.0f, 4.0f + rand01()*4.0f, 15.0f + rand01()*20.0f }, 1.0f + rand01()*2.0f });
        for (int i = 0; i < 10; i++) spawnOneZombieBehindPlayer();
}
    void addMeshTriangles(const Object& o) {
        const V3 camPos = { player.x, 1.55f, player.z };

        // Frustum culling strict avant la boucle
        V3 cam = rotateY(sub(o.pos, camPos), camCosYaw, camSinYaw);
        if (cam.z < -4.0f || cam.z > 85.0f) return;
        if (fabsf(cam.x) > cam.z * 1.4f + 6.0f) return;

        const bool useVc = !o.mesh->vc.empty();

        for (size_t i = 0; i + 2 < o.mesh->idx.size(); i += 3) {
            V3 wp[3]; bool visible = true; float depth = 0.0f;
            for (int k = 0; k < 3; k++) {
                V3 p = o.mesh->p[o.mesh->idx[i+k]];
                p = { p.x*o.scale.x, p.y*o.scale.y, p.z*o.scale.z }; p = rotateY(p, o.yaw); wp[k] = add(o.pos, p);
            }
            V3 nrm = cross(sub(wp[1], wp[0]), sub(wp[2], wp[0]));
            if (dot(nrm, sub(camPos, wp[0])) <= 0.0f) continue;

            // Lumiere "soleil" plus contrastee : haut clair, bas sombre
            float lum = 0.45f + 0.55f * std::max(0.0f, dot(norm(nrm), norm({0.45f, 1.0f, 0.35f})));

            SDL_Vertex verts3[3];
            for (int k = 0; k < 3; k++) {
                V3 camV = rotateY(sub(wp[k], camPos), camCosYaw, camSinYaw);
                float yy = camV.y*camCosPitch - camV.z*camSinPitch, zz = camV.y*camSinPitch + camV.z*camCosPitch;

                if (zz < NEAR_Z) zz = NEAR_Z;
                float sx = SW*0.5f + camV.x * FOV_FOCAL / zz, sy = SH*0.5f - yy * FOV_FOCAL / zz;

                if (sx < -4000 || sx > SW + 4000 || sy < -4000 || sy > SH + 4000) { visible = false; break; }

                // Couleur : couleur du bloc (par face) x teinte de l'objet
                Color base = o.color;
                if (useVc) {
                    const Color& v = o.mesh->vc[o.mesh->idx[i+k]];
                    base = { (Uint8)(v.r * o.color.r / 255), (Uint8)(v.g * o.color.g / 255), (Uint8)(v.b * o.color.b / 255), o.color.a };
                }

                float fog = std::clamp((zz - FOG_START) / (FOG_END - FOG_START), 0.0f, 1.0f);
                verts3[k].position = { sx, sy }; verts3[k].tex_coord = {0,0};
                verts3[k].color = { (Uint8)(base.r*lum*(1-fog) + FOG_R*fog), (Uint8)(base.g*lum*(1-fog) + FOG_G*fog), (Uint8)(base.b*lum*(1-fog) + FOG_B*fog), base.a };
                depth += zz;
            }
            if (visible) tris.push_back({ {verts3[0], verts3[1], verts3[2]}, depth / 3.0f });
        }
    }

    // ------------------------------------------------------------
    // ZOMBIE 100% CUBES style Minecraft :
    // tete cube + cheveux, torse turquoise, pantalon bleu,
    // bras nus tendus. Le coureur a un torse plus fonce.
    // ------------------------------------------------------------
    void drawZombie(const Zombie& z) {
        float bob = sinf(z.wobble) * 0.05f, sway = sinf(z.wobble) * 0.09f;
        float fallDropY = z.dying ? std::min(1.5f, z.deathTilt * 1.0f) : 0.0f;
        V3 toP = sub(player, z.pos); float lyaw = atan2f(toP.x, toP.z);
        V3 fwd = { sinf(lyaw), 0, cosf(lyaw) }, rgt = { cosf(lyaw), 0, -sinf(lyaw) };
        const Color& skin = z.skin;
        Color shirt = (z.type == ZT_RUNNER) ? Color{40,95,115,255} : Color{58,125,145,255};
        Color pants {60,68,170,255};
        Color hairC {45,32,22,255};

        // Ombre au sol
        Object shadow { &boxMesh, {z.pos.x, 0.02f, z.pos.z}, {1.35f, 0.02f, 1.35f}, 0, {0,0,0,110} };

        // Jambes (pantalon bleu Minecraft)
        Object legL { &boxMesh, add(add(z.pos, mul(rgt, -0.16f)), {0, 0.40f + bob*0.5f - fallDropY, 0}), {0.26f, 0.80f, 0.30f}, lyaw, pants };
        Object legR { &boxMesh, add(add(z.pos, mul(rgt,  0.16f)), {0, 0.40f + bob*0.5f - fallDropY, 0}), {0.26f, 0.80f, 0.30f}, lyaw, pants };
        // Torse (chemise turquoise)
        Object torso{ &boxMesh, {z.pos.x, 1.06f + bob - fallDropY, z.pos.z}, {0.64f, 0.78f, 0.36f}, lyaw, shirt };
        // Bras nus tendus vers le joueur
        float armRaise = (z.type == ZT_RUNNER) ? 1.42f : 1.30f;
        Object armL { &boxMesh, add(add(z.pos, mul(rgt, -0.42f)), add(mul(fwd, 0.45f), {0, armRaise + bob - fallDropY, 0})), {0.22f, 0.22f, 0.90f}, lyaw + sway, skin };
        Object armR { &boxMesh, add(add(z.pos, mul(rgt,  0.42f)), add(mul(fwd, 0.45f), {0, armRaise + bob - fallDropY, 0})), {0.22f, 0.22f, 0.90f}, lyaw - sway, skin };
        // Tete CUBE + cheveux
        Object head { &boxMesh, {z.pos.x, 1.68f + bob - fallDropY, z.pos.z}, {0.56f, 0.56f, 0.56f}, lyaw, skin };
        Object hair { &boxMesh, {z.pos.x, 1.90f + bob - fallDropY, z.pos.z}, {0.58f, 0.14f, 0.58f}, lyaw, hairC };
        // Visage pixel
        V3 faceBase = add(z.pos, mul(fwd, 0.29f)); faceBase.y = 1.72f + bob - fallDropY;
        Object eyeL { &boxMesh, add(faceBase, mul(rgt, -0.13f)), {0.11f, 0.12f, 0.06f}, lyaw, {238,238,225,255} };
        Object eyeR { &boxMesh, add(faceBase, mul(rgt,  0.13f)), {0.11f, 0.12f, 0.06f}, lyaw, {238,238,225,255} };
        Object pupL { &boxMesh, add(add(faceBase, mul(rgt, -0.13f)), mul(fwd, 0.04f)), {0.05f, 0.06f, 0.04f}, lyaw, {20,20,25,255} };
        Object pupR { &boxMesh, add(add(faceBase, mul(rgt,  0.13f)), mul(fwd, 0.04f)), {0.05f, 0.06f, 0.04f}, lyaw, {20,20,25,255} };
        Object mouth{ &boxMesh, add(add(z.pos, mul(fwd, 0.29f)), {0, 1.56f + bob - fallDropY, 0}), {0.20f, 0.07f, 0.05f}, lyaw, {45,28,28,255} };

        if (z.dying) {
            float pivotY = 0.9f - fallDropY, c = cosf(z.deathTilt), s = sinf(z.deathTilt);
            auto tilt = [&](V3 p) { V3 r = p; r.y = pivotY + (p.y - pivotY) * c; r.z += (p.y - pivotY) * s * 0.7f; return r; };
            legL.pos=tilt(legL.pos); legR.pos=tilt(legR.pos); torso.pos=tilt(torso.pos);
            armL.pos=tilt(armL.pos); armR.pos=tilt(armR.pos); head.pos=tilt(head.pos);
            hair.pos=tilt(hair.pos);
            eyeL.pos=tilt(eyeL.pos); eyeR.pos=tilt(eyeR.pos);
            pupL.pos=tilt(pupL.pos); pupR.pos=tilt(pupR.pos); mouth.pos=tilt(mouth.pos);
        }
        addMeshTriangles(shadow);
        addMeshTriangles(legL); addMeshTriangles(legR); addMeshTriangles(torso);
        addMeshTriangles(armL); addMeshTriangles(armR);
        addMeshTriangles(head); addMeshTriangles(hair);
        addMeshTriangles(eyeL); addMeshTriangles(eyeR);
        addMeshTriangles(pupL); addMeshTriangles(pupR); addMeshTriangles(mouth);
    }

    void drawSky() {
        float horizonY = SH * 0.5f;
        V3 forwardInf = { player.x + sinf(yaw) * 5000.0f, 0.0f, player.z + cosf(yaw) * 5000.0f };
        V3 cam = rotateY(sub(forwardInf, {player.x, 1.55f, player.z}), camCosYaw, camSinYaw);
        float yy = cam.y*camCosPitch - cam.z*camSinPitch, zz = cam.y*camSinPitch + cam.z*camCosPitch;
        if (zz > 0.5f) horizonY = SH*0.5f - yy * FOV_FOCAL / zz;
        horizonY = std::clamp(horizonY, -200.0f, (float)SH + 200.0f);
        if (horizonY <= 0.0f) return;

        for (int i = 0; i < 24; i++) {
            float t = (float)i / 24;
            Uint8 r = (Uint8)(50 + 150 * t), g = (Uint8)(100 + 110 * t), b = (Uint8)(200 + 55 * t);
            // Proche de l'horizon, on fond vers la couleur du brouillard
            float m = std::clamp((t - 0.70f) / 0.30f, 0.0f, 1.0f) * 0.5f;
            r = (Uint8)(r*(1-m) + FOG_R*m); g = (Uint8)(g*(1-m) + FOG_G*m); b = (Uint8)(b*(1-m) + FOG_B*m);
            SDL_SetRenderDrawColor(ren, r, g, b, 255);
            SDL_Rect band { 0, (int)(t*horizonY), SW, (int)(horizonY/24.0f)+1 }; SDL_RenderFillRect(ren, &band);
        }

        // SOLEIL CARRE style Minecraft
        float sunScreenX = SW * 0.5f - sinf(yaw - 0.5f) * 700.0f;
        float sunScreenY = SH * 0.35f + pitch * 450.0f;
        if (sunScreenY < horizonY + 100) {
            SDL_SetRenderDrawColor(ren, 255, 255, 210, 40);
            SDL_Rect glow { (int)sunScreenX - 80, (int)sunScreenY - 80, 160, 160 }; SDL_RenderFillRect(ren, &glow);
            SDL_SetRenderDrawColor(ren, 255, 255, 230, 90);
            SDL_Rect mid { (int)sunScreenX - 52, (int)sunScreenY - 52, 104, 104 }; SDL_RenderFillRect(ren, &mid);
            SDL_SetRenderDrawColor(ren, 255, 255, 250, 255);
            SDL_Rect core { (int)sunScreenX - 28, (int)sunScreenY - 28, 56, 56 }; SDL_RenderFillRect(ren, &core);
        }

        // NUAGES : grappes de blocs
        for (const auto& c : clouds) {
            V3 rel = sub(c.pos, {player.x, 1.55f, player.z});
            V3 camRel = rotateY(rel, camCosYaw, camSinYaw);
            float cy = camRel.y*camCosPitch - camRel.z*camSinPitch, cz = camRel.y*camSinPitch + camRel.z*camCosPitch;
            if (cz > 5.0f) {
                float sx = SW*0.5f + camRel.x * FOV_FOCAL / cz, sy = SH*0.5f - cy * FOV_FOCAL / cz;
                float szX = c.scale.x * FOV_FOCAL / cz, szY = c.scale.y * FOV_FOCAL / cz;
                if (sx > -300 && sx < SW + 300 && sy > -300 && sy < horizonY + 50) {
                    SDL_SetRenderDrawColor(ren, 255, 255, 255, 230);
                    SDL_Rect a { (int)(sx - szX/2), (int)(sy - szY/2), (int)szX, (int)szY }; SDL_RenderFillRect(ren, &a);
                    SDL_Rect b { (int)(sx - szX*0.95f), (int)(sy - szY*0.10f), (int)(szX*0.5f), (int)(szY*0.6f) }; SDL_RenderFillRect(ren, &b);
                    SDL_Rect c2 { (int)(sx + szX*0.45f), (int)(sy - szY*0.10f), (int)(szX*0.5f), (int)(szY*0.6f) }; SDL_RenderFillRect(ren, &c2);
                    SDL_SetRenderDrawColor(ren, 205, 205, 215, 170);
                    SDL_Rect d { (int)(sx - szX/2), (int)(sy + szY*0.35f), (int)szX, (int)(szY*0.3f) }; SDL_RenderFillRect(ren, &d);
                }
            }
        }
    }

    void spawnBlood(V3 pos, int count) {
        for (int i = 0; i < count; i++) {
            Particle p; p.pos = pos; p.vel = { (rand01()-0.5f)*6.0f, rand01()*4.0f + 1.0f, (rand01()-0.5f)*6.0f };
            p.life = 0.6f + rand01() * 0.4f; p.maxLife = p.life; p.color = { (Uint8)(200+rand01()*55), (Uint8)(20+rand01()*20), (Uint8)(20+rand01()*20), 255 };
            particles.push_back(p);
        }
    }

    void fireShot() {
        if (ammo <= 0 || shootCooldown > 0.0f) return;
        ammo--; muzzleFlash = 0.1f; shootCooldown = SHOOT_INTERVAL; recoil = 1.0f;
        V3 dir = { sinf(yaw)*cosf(pitch), -sinf(pitch), cosf(yaw)*cosf(pitch) };
        V3 camPos = { player.x, 1.55f, player.z };
        Zombie* best = nullptr; bool isHeadshot = false; float bestDot = 0.985f;
        for (auto& z : zombies) {
            if (!z.alive || z.dying) continue;
            V3 toB = sub({z.pos.x, z.pos.y+1.05f, z.pos.z}, camPos);
            if (dot(norm(toB), dir) > bestDot) { bestDot = dot(norm(toB), dir); best = &z; isHeadshot = false; }
            V3 toH = sub({z.pos.x, z.pos.y+1.68f, z.pos.z}, camPos);
            if (dot(norm(toH), dir) > bestDot) { bestDot = dot(norm(toH), dir); best = &z; isHeadshot = true; }
        }
        if (best) {
            spawnBlood({best->pos.x, best->pos.y + (isHeadshot?1.68f:1.05f), best->pos.z}, isHeadshot?15:4);
            if (isHeadshot) { best->dying = true; best->deathTimer = 1.2f; kills++; score += 200; }
            else {
                best->hp--;
                if (best->hp <= 0) { best->dying = true; best->deathTimer = 1.2f; kills++; score += 100; }
            }
            if (kills > 0 && kills % 5 == 0) level++;
        }
}
    void update(float dt, u64 kDown, u64 kHeld) {
        if (hurt > 0.0f) hurt = std::max(0.0f, hurt - dt);
        if (muzzleFlash > 0.0f) muzzleFlash = std::max(0.0f, muzzleFlash - dt);
        if (shootCooldown > 0.0f) shootCooldown = std::max(0.0f, shootCooldown - dt);
        if (recoil > 0.0f) recoil = std::max(0.0f, recoil - dt * 10.0f);

        for (auto& c : clouds) c.pos.x += c.speed * dt;
        if (!clouds.empty() && clouds[0].pos.x > 350.0f) { for(auto& c : clouds) c.pos.x -= 700.0f; }

        for (auto& p : particles) { p.pos = add(p.pos, mul(p.vel, dt)); p.vel.y -= 12.0f * dt; p.life -= dt; }
        particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle& p){ return p.life <= 0.0f; }), particles.end());

        if (title) { if (kDown & HidNpadButton_A) title = false; return; }
        if (dead) { deadTimer -= dt; if (deadTimer <= 0.0f) { respawn(); title = true; } return; }
        if (kDown & HidNpadButton_Plus) paused = !paused; if (paused) return;
        if (kDown & HidNpadButton_B) { title = true; return; }
        if (kDown & HidNpadButton_Minus) { respawn(); title = true; return; }

        timeAlive += dt;
        HidAnalogStickState left = padGetStickPos(&pad, STICK_LEFT), right = padGetStickPos(&pad, STICK_RIGHT);

        float mx = left.x / 32767.0f * MOVE_X_SIGN;
        float mz = left.y / 32767.0f * MOVE_Z_SIGN;
        if (mx != 0.0f || mz != 0.0f) weaponBob += dt * 10.0f; else weaponBob += dt * 2.0f;

        if (kHeld & HidNpadButton_Left) mx -= 1.0f; if (kHeld & HidNpadButton_Right) mx += 1.0f;
        if (kHeld & HidNpadButton_Up) mz += 1.0f; if (kHeld & HidNpadButton_Down) mz -= 1.0f;
        float len = sqrtf(mx*mx + mz*mz); if (len > 1.0f) { mx /= len; mz /= len; }

        // rotateYInv = CORRECT (meme axe que la camera)
        V3 move = rotateYInv({ mx*7.0f*dt, 0, mz*7.0f*dt }, cosf(yaw), sinf(yaw));
        player.x = std::clamp(player.x + move.x, -MAP_LIMIT, MAP_LIMIT);
        player.z = std::clamp(player.z + move.z, -MAP_LIMIT, MAP_LIMIT);

        yaw += right.x / 32767.0f * CAM_X_SIGN * 2.4f * dt;
        pitch += right.y / 32767.0f * CAM_Y_SIGN * 2.0f * dt;
        pitch = std::clamp(pitch, -1.48f, 1.48f);

        if (kHeld & HidNpadButton_ZR) fireShot();

        for (auto& b : ammoBoxes) {
            if (b.taken) continue;
            float dx = b.pos.x - player.x, dz = b.pos.z - player.z;
            if (dx*dx + dz*dz < 1.4f*1.4f) { b.taken = true; ammo = std::min(999, ammo + 100); }
        }

        static float spawnCooldown = 0.0f; spawnCooldown -= dt;
        int targetCount = std::min(15, 8 + (int)(timeAlive / 10.0f));
        int aliveCount = 0; for (auto& z : zombies) if (z.alive) aliveCount++;
        if (aliveCount < targetCount && spawnCooldown <= 0.0f) { spawnOneZombieBehindPlayer(); spawnCooldown = 1.0f; }

        if (zombies.size() > 30) zombies.erase(std::remove_if(zombies.begin(), zombies.end(), [](const Zombie& z){ return !z.alive && !z.dying; }), zombies.end());

        for (auto& z : zombies) {
            if (z.dying) {
                z.deathTimer -= dt; z.deathTilt = std::min(1.5708f, z.deathTilt + dt * 5.0f);
                if (z.deathTimer <= 0.0f) { z.dying = false; z.alive = false; } continue;
            }
            if (!z.alive) continue;
            z.wobble += dt * (z.type == ZT_RUNNER ? 14.0f : 7.0f);

            V3 d = sub(player, z.pos); float dist = sqrtf(d.x*d.x + d.z*d.z);
            if (dist > 1.55f && dist > 0.001f) {
                z.pos.x += (d.x / dist * z.speed) * dt; z.pos.z += (d.z / dist * z.speed) * dt;
                z.pos.x = std::clamp(z.pos.x, -MAP_LIMIT, MAP_LIMIT); z.pos.z = std::clamp(z.pos.z, -MAP_LIMIT, MAP_LIMIT);
            } else {
                z.cooldown -= dt;
                if (z.cooldown <= 0.0f) { hearts--; hurt = 0.65f; z.cooldown = 1.25f; if (hearts <= 0) { dead = true; deadTimer = 2.0f; } }
            }
        }
    }

public:
    void respawn() {
        player = {0,0,0}; yaw = 0.0f; pitch = 0.0f;
        hearts = MAX_HEARTS; score = 0; kills = 0; ammo = 500; level = 1;
        hurt = 0.0f; timeAlive = 0.0f; dead = false; deadTimer = 0.0f;
        zombies.clear(); particles.clear();
        for (auto& b : ammoBoxes) b.taken = false;
        for (int i = 0; i < 8; i++) spawnOneZombieBehindPlayer();
    }

    void drawWorld() {
        camCosYaw = cosf(yaw); camSinYaw = sinf(yaw);
        camCosPitch = cosf(pitch); camSinPitch = sinf(pitch);

        SDL_SetRenderDrawColor(ren, 111, 177, 225, 255);
        SDL_RenderClear(ren);

        drawSky();

        tris.clear(); tris.reserve(4000);
        for (const auto& o : objects) addMeshTriangles(o);
        for (const auto& z : zombies) if (z.alive || z.dying) drawZombie(z);
        // Coffres Minecraft (coffre en bois + fermoir)
        for (const auto& b : ammoBoxes) if (!b.taken) {
            addMeshTriangles({ &boxMesh, b.pos, {0.9f, 0.6f, 0.9f}, 0, {146,96,50,255} });
            addMeshTriangles({ &boxMesh, {b.pos.x, b.pos.y + 0.34f, b.pos.z}, {0.96f, 0.14f, 0.96f}, 0, {112,70,34,255} });
            addMeshTriangles({ &boxMesh, {b.pos.x, b.pos.y + 0.05f, b.pos.z + 0.45f}, {0.12f, 0.20f, 0.08f}, 0, {220,200,60,255} });
            addMeshTriangles({ &boxMesh, {b.pos.x, b.pos.y + 0.05f, b.pos.z - 0.45f}, {0.12f, 0.20f, 0.08f}, 0, {220,200,60,255} });
        }

        std::sort(tris.begin(), tris.end(), [](const Tri& a, const Tri& b) { return a.depth > b.depth; });
        verts.clear(); indices.clear(); verts.reserve(tris.size()*3); indices.reserve(tris.size()*3);
        for (const auto& t : tris) {
            int base = (int)verts.size();
            for (auto& v : t.v) verts.push_back(v);
            indices.push_back(base); indices.push_back(base+1); indices.push_back(base+2);
        }
        // Blend active pour les ombres et l'eau translucide
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        if (!verts.empty()) SDL_RenderGeometry(ren, nullptr, verts.data(), (int)verts.size(), indices.data(), (int)indices.size());

        // PARTICULES
        for (const auto& p : particles) {
            V3 cam = rotateY(sub(p.pos, {player.x, 1.55f, player.z}), camCosYaw, camSinYaw);
            float yy = cam.y*camCosPitch - cam.z*camSinPitch, zz = cam.y*camSinPitch + cam.z*camCosPitch;
            if (zz < NEAR_Z) continue;
            int sx = (int)(SW*0.5f + cam.x * FOV_FOCAL / zz), sy = (int)(SH*0.5f - yy * FOV_FOCAL / zz);
            int sz = (int)std::max(2.0f, 60.0f / zz); if (sz > 8) sz = 8;
            SDL_SetRenderDrawColor(ren, p.color.r, p.color.g, p.color.b, (Uint8)((p.life/p.maxLife)*255));
            SDL_Rect rect { sx - sz/2, sy - sz/2, sz, sz }; SDL_RenderFillRect(ren, &rect);
        }

        // ARME (FPS)
        int gunX = SW - 340 + (int)(cosf(weaponBob) * 8.0f);
        int gunY = SH - 210 + (int)(fabsf(sinf(weaponBob)) * 10.0f) + (int)(recoil * 25.0f);
        SDL_SetRenderDrawColor(ren, 30, 30, 30, 255);
        SDL_Rect gunBody1 { gunX, gunY + 40, 90, 40 }; SDL_RenderFillRect(ren, &gunBody1);
        SDL_Rect gunBody2 { gunX + 60, gunY, 160, 30 }; SDL_RenderFillRect(ren, &gunBody2);
        SDL_Rect mag { gunX + 80, gunY + 70, 40, 80 }; SDL_RenderFillRect(ren, &mag);
        SDL_SetRenderDrawColor(ren, 100, 100, 100, 255);
        SDL_Rect sight { gunX + 100, gunY - 10, 10, 20 }; SDL_RenderFillRect(ren, &sight);
        SDL_Rect barrelTip { gunX + 220, gunY + 10, 20, 10 }; SDL_RenderFillRect(ren, &barrelTip);

        if (muzzleFlash > 0.0f) {
            int fx = gunX + 240, fy = gunY + 15;
            SDL_SetRenderDrawColor(ren, 255, 255, 200, 255);
            SDL_Rect f1 { fx - 15, fy - 5, 30, 10 }; SDL_RenderFillRect(ren, &f1);
            SDL_Rect f2 { fx - 5, fy - 15, 10, 30 }; SDL_RenderFillRect(ren, &f2);
            SDL_SetRenderDrawColor(ren, 255, 150, 0, 200);
            SDL_Rect f3 { fx - 25, fy - 3, 50, 6 }; SDL_RenderFillRect(ren, &f3);
            SDL_Rect f4 { fx - 3, fy - 25, 6, 50 }; SDL_RenderFillRect(ren, &f4);
            SDL_SetRenderDrawColor(ren, 255, 255, 255, 150);
            SDL_RenderDrawLine(ren, fx, fy, SW/2, SH/2);
        }
    }

    void drawHud() {
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        for (int i = 0; i < MAX_HEARTS; i++) drawHeart(ren, 40 + i*40, 40, 30, (i < hearts) ? Color{230,40,60,255} : Color{60,30,40,180}, (i < hearts));
        SDL_SetRenderDrawColor(ren, 20,20,20,160); SDL_Rect sbox{SW-240,20,220,50}; SDL_RenderFillRect(ren,&sbox);
        drawNumber(ren, score, SW-220, 28, 4, {255,240,100,255});
        SDL_SetRenderDrawColor(ren, 20,20,20,160); SDL_Rect abox{SW-240,80,220,38}; SDL_RenderFillRect(ren,&abox);
        drawNumber(ren, ammo, SW-220, 86, 3, {120,220,255,255});

        SDL_SetRenderDrawColor(ren, 255,255,255,230);
        SDL_RenderDrawLine(ren, SW/2-10, SH/2, SW/2+10, SH/2);
        SDL_RenderDrawLine(ren, SW/2, SH/2-10, SW/2, SH/2+10);

        if (hurt > 0.0f) { SDL_SetRenderDrawColor(ren, 180,0,0,(Uint8)(hurt*115.0f)); SDL_Rect r{0,0,SW,SH}; SDL_RenderFillRect(ren,&r); }
        if (title || dead || paused) { SDL_SetRenderDrawColor(ren, 0,0,0,175); SDL_Rect p{SW/2-300, SH/2-100, 600, 200}; SDL_RenderFillRect(ren,&p); }
    }

    bool init() {
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad);

        if (SDL_Init(SDL_INIT_VIDEO) != 0) { printf("SDL_Init error: %s\n", SDL_GetError()); return false; }

        win = SDL_CreateWindow("MIOZ ZOMBIE WORLD", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SW, SH, 0);
        if (!win) { printf("Window error: %s\n", SDL_GetError()); return false; }

        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
        if (!ren) { printf("Renderer error: %s\n", SDL_GetError()); return false; }
        buildWorld();
        printf("MIOZ ZOMBIE WORLD - Edition Minecraft\n");
        return true;
    }

    void run() {
        bool quit = false; Uint64 last = SDL_GetPerformanceCounter();
        while (appletMainLoop() && !quit) {
            padUpdate(&pad); u64 kDown = padGetButtonsDown(&pad), kHeld = padGetButtons(&pad);
            SDL_Event e; while (SDL_PollEvent(&e)) if (e.type == SDL_QUIT) quit = true;
            Uint64 now = SDL_GetPerformanceCounter();
            float dt = (float)((double)(now - last) / SDL_GetPerformanceFrequency()); last = now;
            dt = std::clamp(dt, 0.0f, 0.05f);
            update(dt, kDown, kHeld); drawWorld(); drawHud(); SDL_RenderPresent(ren);
        }
    }
    ~Game() { if (ren) SDL_DestroyRenderer(ren); if (win) SDL_DestroyWindow(win); SDL_Quit(); }
};

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    Game game; if (game.init()) game.run();
    return 0;
}
