// ============================================================
// MIOZ ARENA SWITCH - C++ / SDL2 / libnx
// Version complète et sécurisée
// ============================================================

#include <switch.h>
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

static constexpr int SCREEN_W = 1280;
static constexpr int SCREEN_H = 720;

static constexpr float PI = 3.14159265358979323846f;
static constexpr float TWO_PI = PI * 2.0f;

static constexpr float MAP_LIMIT = 98.0f;
static constexpr float CAMERA_HEIGHT = 1.65f;
static constexpr float CAMERA_FOCAL = 650.0f;
static constexpr float NEAR_PLANE = 0.15f;

static constexpr float PLAYER_SPEED = 7.0f;
static constexpr float ZOMBIE_SPEED = 2.25f;

// ------------------------------------------------------------
// STRUCTURES DE BASE
// ------------------------------------------------------------
struct V3 { float x = 0.0f; float y = 0.0f; float z = 0.0f; };
struct Color { Uint8 r = 255; Uint8 g = 255; Uint8 b = 255; Uint8 a = 255; };
struct Tri { SDL_Vertex v[3]; float depth = 0.0f; };

static V3 v3(float x, float y, float z) { return {x, y, z}; }
static V3 add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static V3 mul(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

static float clampf(float value, float minimum, float maximum) { 
    return std::max(minimum, std::min(value, maximum)); 
}

static float distanceXZ(V3 a, V3 b) { 
    float dx = a.x - b.x; 
    float dz = a.z - b.z; 
    return std::sqrt(dx * dx + dz * dz); 
}

struct RandomGenerator {
    uint32_t seed = 0x4D494F5A;
    uint32_t next() { seed = seed * 1664525u + 1013904223u; return seed; }
    float value() { return (float)(next() & 0x00FFFFFFu) / 16777216.0f; }
    float range(float minimum, float maximum) { return minimum + value() * (maximum - minimum); }
};

struct Mesh { 
    std::vector<V3> vertices; 
    std::vector<uint32_t> indices; 
};

static Mesh makeBox() {
    Mesh mesh;
    mesh.vertices = {
        {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f},
        {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f}
    };
    uint32_t indices[] = { 0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 2, 3, 7, 2, 7, 6, 1, 2, 6, 1, 6, 5, 3, 0, 4, 3, 4, 7 };
    mesh.indices.assign(indices, indices + 36);
    return mesh;
}

static Mesh makeSphere(float radius = 1.0f, int rings = 8, int segments = 12) {
    Mesh mesh;
    for (int y = 0; y <= rings; y++) {
        float v = (float)y / rings; float phi = v * PI;
        for (int x = 0; x <= segments; x++) {
            float u = (float)x / segments; float angle = u * TWO_PI;
            mesh.vertices.push_back({ radius * std::sin(phi) * std::cos(angle), radius * std::cos(phi), radius * std::sin(phi) * std::sin(angle) });
        }
    }
    for (int y = 0; y < rings; y++) {
        for (int x = 0; x < segments; x++) {
            uint32_t a = y * (segments + 1) + x; uint32_t b = a + segments + 1;
            mesh.indices.insert(mesh.indices.end(), { a, b, a + 1, a + 1, b, b + 1 });
        }
    }
    return mesh;
}

struct Object { const Mesh* mesh = nullptr; V3 position; V3 scale{1.0f, 1.0f, 1.0f}; Color color; };
struct Zombie { V3 position; float attackCooldown = 0.0f; float animationTime = 0.0f; float health = 100.0f; bool alive = true; };
struct Projectile { V3 position; V3 velocity; float life = 2.0f; float damage = 25.0f; bool active = true; };
struct Particle { V3 position; V3 velocity; Color color; float life = 0.5f; float size = 0.08f; };

static Mesh boxMesh = makeBox();
static Mesh sphereMesh = makeSphere();

// ------------------------------------------------------------
// CLASSE DU JEU
// ------------------------------------------------------------
class Game {
private:
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    PadState pad;

    // Audio
    SDL_AudioDeviceID audioDevice = 0;
    Uint32 wavLengthShot = 0; Uint8* wavBufferShot = nullptr;
    Uint32 wavLengthZombie = 0; Uint8* wavBufferZombie = nullptr;
    Uint32 wavLengthReload = 0; Uint8* wavBufferReload = nullptr;

    V3 playerPosition{0.0f, 0.0f, 0.0f};
    float cameraYaw = 0.0f; float cameraPitch = 0.0f;
    float playerHealth = 100.0f; float hurtTimer = 0.0f;
    bool playerDead = false; float deathTimer = 0.0f;
    bool paused = false;

    int pistolAmmo = 100;
    float weaponCooldown = 0.0f; float weaponRecoil = 0.0f; float muzzleFlash = 0.0f;

    RandomGenerator random;
    std::vector<Object> objects; std::vector<Zombie> zombies;
    std::vector<Projectile> projectiles; std::vector<Particle> particles;
    std::vector<Tri> triangles;

    void buildWorld();
    void addMeshTriangles(const Object& object, std::vector<Tri>& output);
    void drawWorld(); void drawZombie(const Zombie& zombie); void drawAllZombies();
    void drawWeapon(); void drawHud();
    void update(float dt); void updatePlayer(float dt); void updateZombies(float dt);
    void updateProjectiles(float dt); void updateParticles(float dt);
    void fireWeapon(); void spawnImpact(V3 position, Color color);
    void initAudio(); void playSound(Uint8* wavBuffer, Uint32 wavLength);
    bool collidesWithWorld(V3 p);

public:
    Game();
    bool init();
    void run();
    ~Game();
};

// ------------------------------------------------------------
// CONSTRUCTION DU MONDE
// ------------------------------------------------------------
void Game::buildWorld() {
    objects.clear(); zombies.clear(); projectiles.clear(); particles.clear();
    
    for (int x = -10; x < 10; x++) {
        for (int z = -10; z < 10; z++) {
            Object o;
            o.mesh = &boxMesh;
            o.position = {x * 10.0f + 5.0f, -0.25f, z * 10.0f + 5.0f};
            o.scale = {9.98f, 0.5f, 9.98f}; 
            o.color = ((x + z) % 2 == 0) ? Color{92, 155, 68, 255} : Color{75, 130, 55, 255};
            objects.push_back(o);
        }
    }

    for (int i = 0; i < 40; i++) {
        float x = random.range(-80.0f, 80.0f); float z = random.range(-80.0f, 80.0f);
        if (std::sqrt(x*x + z*z) < 15.0f) continue; 
        
        Object trunk; trunk.mesh = &boxMesh; trunk.position = {x, 2.0f, z}; trunk.scale = {0.6f, 4.0f, 0.6f}; trunk.color = {90, 60, 30, 255}; objects.push_back(trunk);
        Object leaves; leaves.mesh = &boxMesh; leaves.position = {x, 4.5f, z}; leaves.scale = {3.0f, 2.5f, 3.0f}; leaves.color = {35, 90, 40, 255}; objects.push_back(leaves);
    }

    for (int i = 0; i < 25; i++) {
        Zombie z;
        z.position = {random.range(-60.0f, 60.0f), 0.0f, random.range(-60.0f, 60.0f)};
        if (std::sqrt(z.position.x*z.position.x + z.position.z*z.position.z) < 15.0f) { z.position.x += 25.0f; }
        z.health = 100.0f; z.alive = true;
        zombies.push_back(z);
    }
}

bool Game::collidesWithWorld(V3 p) {
    if (p.x < -MAP_LIMIT + 1.0f || p.x > MAP_LIMIT - 1.0f || p.z < -MAP_LIMIT + 1.0f || p.z > MAP_LIMIT - 1.0f) return true;
    return false;
}

// ------------------------------------------------------------
// LOGIQUE DU JOUEUR (MANETTE)
// ------------------------------------------------------------
void Game::updatePlayer(float dt) {
    // Mise à jour de la manette
    padUpdate(&pad);
    const u64 kDown = padGetButtonsDown(&pad);
    const u64 kHeld = padGetButtons(&pad);
    
    HidAnalogStickState leftStick = padGetStickPos(&pad, 0);
    HidAnalogStickState rightStick = padGetStickPos(&pad, 1);

    if (playerDead) { 
        deathTimer += dt; 
        if ((kDown & HidNpadButton_A) || deathTimer > 3.0f) { playerPosition = {0,0,0}; playerHealth = 100.0f; playerDead = false; } 
        return; 
    }
    if (kDown & HidNpadButton_Plus) paused = !paused;
    if (paused) { if (kDown & HidNpadButton_A) paused = false; return; }

    const float leftX = clampf((float)leftStick.x / 32767.0f, -1.0f, 1.0f);
    const float leftY = clampf((float)leftStick.y / 32767.0f, -1.0f, 1.0f);
    const float rightX = clampf((float)rightStick.x / 32767.0f, -1.0f, 1.0f);
    const float rightY = clampf((float)rightStick.y / 32767.0f, -1.0f, 1.0f);

    cameraYaw += rightX * 2.4f * dt; 
    cameraPitch -= rightY * 1.8f * dt;
    cameraPitch = clampf(cameraPitch, -0.85f, 0.85f);

    const float forwardX = std::sin(cameraYaw); const float forwardZ = std::cos(cameraYaw);
    const float rightMoveX = std::cos(cameraYaw); const float rightMoveZ = -std::sin(cameraYaw);
    float moveX = forwardX * leftY + rightMoveX * leftX; float moveZ = forwardZ * leftY + rightMoveZ * leftX;
    const float moveLength = std::sqrt(moveX * moveX + moveZ * moveZ);

    if (moveLength > 0.12f) {
        if (moveLength > 1.0f) { moveX /= moveLength; moveZ /= moveLength; }
        const float stepX = moveX * PLAYER_SPEED * dt; const float stepZ = moveZ * PLAYER_SPEED * dt;
        V3 candidate = playerPosition; candidate.x += stepX; if (!collidesWithWorld(candidate)) playerPosition.x = candidate.x;
        candidate = playerPosition; candidate.z += stepZ; if (!collidesWithWorld(candidate)) playerPosition.z = candidate.z;
    }
    if (kHeld & HidNpadButton_ZR) fireWeapon();
}

void Game::update(float dt) {
    dt = clampf(dt, 0.0f, 0.05f); 
    updatePlayer(dt); 
    if (paused || playerDead) return;
    
    if (weaponCooldown > 0.0f) weaponCooldown -= dt;
    if (weaponRecoil > 0.0f) { weaponRecoil -= dt * 8.0f; if (weaponRecoil < 0.0f) weaponRecoil = 0.0f; }
    if (muzzleFlash > 0.0f) muzzleFlash -= dt;
    if (hurtTimer > 0.0f) hurtTimer -= dt;
    
    updateZombies(dt); updateProjectiles(dt); updateParticles(dt);
    if (playerHealth <= 0.0f) { playerHealth = 0.0f; playerDead = true; deathTimer = 0.0f; }
}

// ------------------------------------------------------------
// COMBAT ET IA
// ------------------------------------------------------------
void Game::fireWeapon() {
    if (playerDead || paused || weaponCooldown > 0.0f) return;
    if (pistolAmmo <= 0) { weaponCooldown = 0.3f; return; }
    
    pistolAmmo--;
    playSound(wavBufferShot, wavLengthShot);

    const float dirX = std::sin(cameraYaw) * std::cos(cameraPitch); 
    const float dirY = std::sin(cameraPitch); 
    const float dirZ = std::cos(cameraYaw) * std::cos(cameraPitch);
    
    Projectile p; p.position = v3(playerPosition.x, playerPosition.y + CAMERA_HEIGHT, playerPosition.z);
    p.velocity = v3(dirX * 45.0f, dirY * 45.0f, dirZ * 45.0f);
    p.life = 2.0f; p.damage = 35.0f; p.active = true; projectiles.push_back(p);
    
    weaponCooldown = 0.2f; weaponRecoil = 1.0f; muzzleFlash = 0.05f;
}

void Game::spawnImpact(V3 position, Color color) {
    for (int i = 0; i < 5; ++i) { Particle p; p.position = position; p.velocity = v3(random.range(-3.0f, 3.0f), random.range(1.0f, 4.0f), random.range(-3.0f, 3.0f)); p.color = color; particles.push_back(p); }
}

void Game::updateZombies(float dt) {
    for (Zombie& z : zombies) {
        if (!z.alive) continue;
        z.animationTime += dt; if (z.attackCooldown > 0.0f) z.attackCooldown -= dt;
        const float dist = distanceXZ(playerPosition, z.position);
        
        if (dist < 1.5f) {
            if (z.attackCooldown <= 0.0f) { if (random.value() < 0.3f) playSound(wavBufferZombie, wavLengthZombie); playerHealth -= 10.0f; hurtTimer = 0.3f; z.attackCooldown = 1.0f; }
            continue;
        }
        if (dist < 80.0f) { 
            const float dx = playerPosition.x - z.position.x; const float dz = playerPosition.z - z.position.z;
            const float inv = 1.0f / dist;
            V3 cand = z.position; cand.x += dx * inv * ZOMBIE_SPEED * dt; if (!collidesWithWorld(cand)) z.position.x = cand.x;
            cand = z.position; cand.z += dz * inv * ZOMBIE_SPEED * dt; if (!collidesWithWorld(cand)) z.position.z = cand.z;
        }
    }
}

void Game::updateProjectiles(float dt) {
    for (Projectile& p : projectiles) {
        if (!p.active) continue;
        p.position = add(p.position, mul(p.velocity, dt)); p.life -= dt;
        if (p.life <= 0.0f) { p.active = false; continue; }
        for (Zombie& z : zombies) {
            if (!z.alive) continue;
            const V3 zc = v3(z.position.x, z.position.y + 1.0f, z.position.z);
            if (std::sqrt(std::pow(p.position.x - zc.x, 2) + std::pow(p.position.y - zc.y, 2) + std::pow(p.position.z - zc.z, 2)) < 0.9f) {
                z.health -= p.damage; p.active = false;
                spawnImpact(p.position, {255, 0, 0, 255});
                if (z.health <= 0.0f) { z.alive = false; spawnImpact(zc, {80, 220, 100, 255}); }
                break;
            }
        }
    }
    projectiles.erase(std::remove_if(projectiles.begin(), projectiles.end(), [](const Projectile& p) { return !p.active; }), projectiles.end());
}

void Game::updateParticles(float dt) {
    for (Particle& p : particles) { p.position = add(p.position, mul(p.velocity, dt)); p.velocity.y -= 9.8f * dt; p.life -= dt; }
    particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle& p) { return p.life <= 0.0f; }), particles.end());
}

// ------------------------------------------------------------
// RENDU 3D
// ------------------------------------------------------------
void Game::addMeshTriangles(const Object& object, std::vector<Tri>& output) {
    if (!object.mesh) return;
    const Mesh& mesh = *object.mesh;
    const float cosYaw = std::cos(cameraYaw), sinYaw = std::sin(cameraYaw), cosPitch = std::cos(cameraPitch), sinPitch = std::sin(cameraPitch);
    const float cx = playerPosition.x, cy = playerPosition.y + CAMERA_HEIGHT, cz = playerPosition.z;
    
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        Tri tri{}; bool valid = true; float depthSum = 0.0f;
        for (int j = 0; j < 3; ++j) {
            V3 v = mesh.vertices[mesh.indices[i + j]];
            v.x *= object.scale.x; v.y *= object.scale.y; v.z *= object.scale.z;
            v = add(v, object.position);
            float dx = v.x - cx, dy = v.y - cy, dz = v.z - cz;
            float viewX = dx * cosYaw - dz * sinYaw, viewZ = dx * sinYaw + dz * cosYaw, viewY = dy * cosPitch - viewZ * sinPitch, finalZ = dy * sinPitch + viewZ * cosPitch;
            if (finalZ < NEAR_PLANE) { valid = false; break; }
            tri.v[j].position.x = (SCREEN_W * 0.5f) + (viewX * CAMERA_FOCAL / finalZ);
            tri.v[j].position.y = (SCREEN_H * 0.5f) - (viewY * CAMERA_FOCAL / finalZ);
            tri.v[j].color = {object.color.r, object.color.g, object.color.b, object.color.a};
            depthSum += finalZ;
        }
        if (valid) { tri.depth = depthSum / 3.0f; output.push_back(tri); }
    }
}

void Game::drawWorld() {
    triangles.clear();
    for (const Object& o : objects) addMeshTriangles(o, triangles);
    for (const Projectile& p : projectiles) { if (!p.active) continue; Object o; o.mesh = &sphereMesh; o.position = p.position; o.scale = v3(0.1f, 0.1f, 0.1f); o.color = {255, 210, 90, 255}; addMeshTriangles(o, triangles); }
    std::sort(triangles.begin(), triangles.end(), [](const Tri& a, const Tri& b) { return a.depth > b.depth; });
    for (const Tri& t : triangles) SDL_RenderGeometry(renderer, nullptr, t.v, 3, nullptr, 0);
}

void Game::drawZombie(const Zombie& z) {
    const float anim = std::sin(z.animationTime * 5.0f) * 0.1f;
    const Color green = {45, 110, 55, 255}; 
    const Color dark = {25, 70, 35, 255};
    const Color shirt = {40, 50, 100, 255};
    const Color eye = {255, 20, 20, 255};

    Object body; body.mesh = &boxMesh; body.position = {z.position.x, 1.0f + anim, z.position.z}; body.scale = {0.8f, 1.2f, 0.5f}; body.color = shirt; addMeshTriangles(body, triangles);
    Object head; head.mesh = &boxMesh; head.position = {z.position.x, 1.9f + anim, z.position.z}; head.scale = {0.6f, 0.6f, 0.6f}; head.color = green; addMeshTriangles(head, triangles);
    
    Object eyeL; eyeL.mesh = &boxMesh; eyeL.position = {z.position.x - 0.15f, 1.95f + anim, z.position.z + 0.31f}; eyeL.scale = {0.1f, 0.1f, 0.1f}; eyeL.color = eye; addMeshTriangles(eyeL, triangles);
    Object eyeR; eyeR.mesh = &boxMesh; eyeR.position = {z.position.x + 0.15f, 1.95f + anim, z.position.z + 0.31f}; eyeR.scale = {0.1f, 0.1f, 0.1f}; eyeR.color = eye; addMeshTriangles(eyeR, triangles);

    Object armL; armL.mesh = &boxMesh; armL.position = {z.position.x - 0.5f, 1.2f, z.position.z}; armL.scale = {0.25f, 0.8f, 0.25f}; armL.color = green; addMeshTriangles(armL, triangles);
    Object armR; armR.mesh = &boxMesh; armR.position = {z.position.x + 0.5f, 1.2f, z.position.z}; armR.scale = {0.25f, 0.8f, 0.25f}; armR.color = green; addMeshTriangles(armR, triangles);

    Object legL; legL.mesh = &boxMesh; legL.position = {z.position.x - 0.2f, 0.25f - anim, z.position.z}; legL.scale = {0.3f, 0.5f, 0.3f}; legL.color = dark; addMeshTriangles(legL, triangles);
    Object legR; legR.mesh = &boxMesh; legR.position = {z.position.x + 0.2f, 0.25f + anim, z.position.z}; legR.scale = {0.3f, 0.5f, 0.3f}; legR.color = dark; addMeshTriangles(legR, triangles);
}

void Game::drawAllZombies() {
    for (const Zombie& z : zombies) if (z.alive && distanceXZ(playerPosition, z.position) < 100.0f) drawZombie(z);
    std::sort(triangles.begin(), triangles.end(), [](const Tri& a, const Tri& b) { return a.depth > b.depth; });
    for (const Tri& t : triangles) SDL_RenderGeometry(renderer, nullptr, t.v, 3, nullptr, 0);
}

// ------------------------------------------------------------
// ARME ET HUD
// ------------------------------------------------------------
void Game::drawWeapon() {
    int cx = SCREEN_W / 2, cy = SCREEN_H / 2;
    int recoil = (int)(weaponRecoil * 30.0f);
    
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 200);
    SDL_RenderDrawLine(renderer, cx - 15, cy, cx - 5, cy); SDL_RenderDrawLine(renderer, cx + 5, cy, cx + 15, cy);
    SDL_RenderDrawLine(renderer, cx, cy - 15, cx, cy - 5); SDL_RenderDrawLine(renderer, cx, cy + 5, cx, cy + 15);

    SDL_Rect body = {cx + 80, SCREEN_H - 200 + recoil, 160, 70};
    SDL_SetRenderDrawColor(renderer, 45, 45, 50, 255); SDL_RenderFillRect(renderer, &body);
    
    SDL_Rect barrel = {cx + 140, SCREEN_H - 210 + recoil, 100, 40};
    SDL_SetRenderDrawColor(renderer, 30, 30, 35, 255); SDL_RenderFillRect(renderer, &barrel);
    
    SDL_Rect grip = {cx + 90, SCREEN_H - 140 + recoil, 50, 90};
    SDL_SetRenderDrawColor(renderer, 70, 40, 20, 255); SDL_RenderFillRect(renderer, &grip);

    if (muzzleFlash > 0.0f) {
        SDL_Rect flash = {cx + 230, SCREEN_H - 205 + recoil, 40, 30};
        SDL_SetRenderDrawColor(renderer, 255, 220, 50, 255); SDL_RenderFillRect(renderer, &flash);
    }
}

void Game::drawHud() {
    SDL_Rect panel = {18, SCREEN_H - 80, 320, 60}; 
    SDL_SetRenderDrawColor(renderer, 15, 20, 18, 210); SDL_RenderFillRect(renderer, &panel);
    SDL_Rect health = {34, SCREEN_H - 60, (int)(280.0f * clampf(playerHealth / 100.0f, 0, 1)), 20}; 
    SDL_SetRenderDrawColor(renderer, 65, 205, 80, 255); SDL_RenderFillRect(renderer, &health);

    SDL_Rect ammoBar = {SCREEN_W - 200, SCREEN_H - 60, (int)(170.0f * clampf((float)pistolAmmo / 100.0f, 0, 1)), 20};
    SDL_SetRenderDrawColor(renderer, 210, 170, 65, 255); SDL_RenderFillRect(renderer, &ammoBar);

    if (hurtTimer > 0.0f) { 
        SDL_SetRenderDrawColor(renderer, 180, 20, 20, (Uint8)(hurtTimer * 400)); 
        SDL_Rect dmg = {0, 0, SCREEN_W, SCREEN_H}; SDL_RenderFillRect(renderer, &dmg); 
    }
}

// ------------------------------------------------------------
// AUDIO ET INIT
// ------------------------------------------------------------
static Uint8* loadWav(const char* path, Uint32* len) {
    SDL_RWops* rw = SDL_RWFromFile(path, "rb"); 
    if (!rw) return nullptr;
    SDL_AudioSpec spec; Uint8* buf = nullptr;
    if (SDL_LoadWAV_RW(rw, 1, &spec, &buf, len) == nullptr) return nullptr;
    return buf;
}

void Game::initAudio() {
    SDL_AudioSpec want, have; SDL_zero(want); want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 4096;
    audioDevice = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (audioDevice == 0) return; 
    SDL_PauseAudioDevice(audioDevice, 0);
    wavBufferShot = loadWav("romfs/shot.wav", &wavLengthShot);
    wavBufferZombie = loadWav("romfs/zombie.wav", &wavLengthZombie);
    wavBufferReload = loadWav("romfs/reload.wav", &wavLengthReload);
}

void Game::playSound(Uint8* buf, Uint32 len) {
    if (audioDevice != 0 && buf != nullptr && len > 0) SDL_QueueAudio(audioDevice, buf, len);
}

Game::Game() { playerPosition = {0.0f, 0.0f, 0.0f}; }

bool Game::init() {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO);
    window = SDL_CreateWindow("Mioz Arena", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_W, SCREEN_H, 0);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    
    // Initialisation de la manette avec l'API moderne
    padInitializeDefault(&pad);
    
    initAudio();
    buildWorld();
    return true;
}

void Game::run() {
    bool quit = false; Uint64 last = SDL_GetPerformanceCounter();
    while (appletMainLoop() && !quit) {
        SDL_Event e; while (SDL_PollEvent(&e)) { if (e.type == SDL_QUIT) quit = true; }
        Uint64 curr = SDL_GetPerformanceCounter(); float dt = (float)((double)(curr - last) / SDL_GetPerformanceFrequency()); last = curr;
        update(dt);
        
        SDL_SetRenderDrawColor(renderer, 105, 170, 220, 255); SDL_RenderClear(renderer);
        drawWorld(); drawAllZombies(); drawWeapon(); drawHud();
        if (paused) { SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180); SDL_Rect ov = {0, 0, SCREEN_W, SCREEN_H}; SDL_RenderFillRect(renderer, &ov); }
        if (playerDead) { SDL_SetRenderDrawColor(renderer, 100, 0, 0, 200); SDL_Rect ov = {0, 0, SCREEN_W, SCREEN_H}; SDL_RenderFillRect(renderer, &ov); }
        
        SDL_RenderPresent(renderer);
    }
}

Game::~Game() {
    if (wavBufferShot) SDL_FreeWAV(wavBufferShot); if (wavBufferZombie) SDL_FreeWAV(wavBufferZombie); if (wavBufferReload) SDL_FreeWAV(wavBufferReload);
    if (audioDevice != 0) SDL_CloseAudioDevice(audioDevice);
    if (renderer) SDL_DestroyRenderer(renderer); if (window) SDL_DestroyWindow(window); SDL_Quit();
}

int main() {
    Game game; if (!game.init()) return 1; game.run(); return 0;
}
