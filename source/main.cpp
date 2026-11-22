// ============================================================
// MIOZ ARENA SWITCH - C++ / SDL2 / libnx
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
static constexpr float NEAR_PLANE = 0.12f;
static constexpr int MAX_PARTICLES = 260;
static constexpr int MAX_PROJECTILES = 48;
static constexpr float PLAYER_SPEED = 7.0f;
static constexpr float ZOMBIE_SPEED = 2.25f;

struct V3 { float x = 0.0f; float y = 0.0f; float z = 0.0f; };
struct Color { Uint8 r = 255; Uint8 g = 255; Uint8 b = 255; Uint8 a = 255; };
struct Tri { SDL_Vertex v[3]; float depth = 0.0f; };

static V3 v3(float x, float y, float z) { return {x, y, z}; }
static V3 add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static V3 mul(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float length(V3 a) { return std::sqrt(dot(a, a)); }
static V3 normalize(V3 a) { float l = length(a); if (l < 0.0001f) return {0.0f, 0.0f, 0.0f}; return mul(a, 1.0f / l); }

static float clampf(float value, float minimum, float maximum) { return std::max(minimum, std::min(value, maximum)); }
static float distanceXZ(V3 a, V3 b) { float dx = a.x - b.x; float dz = a.z - b.z; return std::sqrt(dx * dx + dz * dz); }

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

struct Object {
    const Mesh* mesh = nullptr;
    V3 position; V3 scale{1.0f, 1.0f, 1.0f}; float yaw = 0.0f; V3 pivotOffset{0.0f, 0.0f, 0.0f}; Color color;
};

struct Zombie { V3 position; float attackCooldown = 0.0f; float animationTime = 0.0f; float health = 100.0f; bool alive = true; };
struct Projectile { V3 position; V3 velocity; float life = 2.0f; float damage = 25.0f; bool active = true; };
struct Particle { V3 position; V3 velocity; Color color; float life = 0.5f; float size = 0.08f; };
struct AmmoBox { V3 position; int amount = 20; bool active = true; };

static Mesh boxMesh = makeBox();
static Mesh sphereMesh = makeSphere();

// ============================================================
// CLASSE PRINCIPALE
// ============================================================
class Game {
private:
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    PadState pad;

    // AUDIO
    SDL_AudioDeviceID audioDevice = 0;
    Uint32 wavLengthShot = 0; Uint8* wavBufferShot = nullptr;
    Uint32 wavLengthZombie = 0; Uint8* wavBufferZombie = nullptr;
    Uint32 wavLengthReload = 0; Uint8* wavBufferReload = nullptr;

    V3 playerPosition{-12.0f, 0.0f, -1.0f};
    float cameraYaw = 0.0f; float cameraPitch = 0.0f;
    float playerHealth = 100.0f; float hurtTimer = 0.0f;
    bool playerDead = false; float deathTimer = 0.0f;
    bool paused = false;

    V3 homeSpawn{-12.0f, 0.0f, -1.0f};
    bool homeDoorOpen = false; size_t homeDoorIndex = 0;

    int selectedWeapon = 0; int pistolAmmo = 60; int rifleAmmo = 180;
    float weaponCooldown = 0.0f; float muzzleFlash = 0.0f; float weaponRecoil = 0.0f;

    RandomGenerator random;
    std::vector<Object> objects; std::vector<Zombie> zombies;
    std::vector<Projectile> projectiles; std::vector<Particle> particles;
    std::vector<AmmoBox> ammoBoxes;
    std::vector<Tri> triangles;

    int score = 0; int zombiesKilled = 0;

    float randomRange(float minimum, float maximum) { return random.range(minimum, maximum); }
    void addObject(const Mesh* mesh, V3 position, V3 scale, Color color, float yaw = 0.0f, V3 pivotOffset = v3(0.0f, 0.0f, 0.0f)) {
        Object object; object.mesh = mesh; object.position = position; object.scale = scale; object.color = color; object.yaw = yaw; object.pivotOffset = pivotOffset; objects.push_back(object);
    }
    
    void buildWorld();
    void buildGround();
    void buildTrees();
    void buildPlayerHouse();
    void addMeshTriangles(const Object& object, std::vector<Tri>& output);
    void drawWorld(); void drawZombie(const Zombie& zombie); void drawAllZombies();
    void drawWeapon(); void drawHud(); void drawPauseScreen(); void drawDeathScreen();
    void update(float dt); void updatePlayer(float dt); void updateZombies(float dt);
    void updateProjectiles(float dt); void updateParticles(float dt);
    void fireWeapon(); void spawnImpact(V3 position, Color color = Color{255, 220, 120, 255});
    void respawn(); void interact(); void collectAmmo();
    bool collidesWithWorld(V3 p); bool collidesWithHome(V3 p);

    void initAudio();
    void playSound(Uint8* wavBuffer, Uint32 wavLength);

public:
    Game();
    bool init();
    void run();
    ~Game();
};

// ============================================================
// CONSTRUCTION DU MONDE
// ============================================================
static const Color COLOR_GRASS = {92, 155, 68, 255}; static const Color COLOR_GRASS_DARK = {75, 130, 55, 255};
static const Color COLOR_DIRT = {125, 85, 55, 255}; static const Color COLOR_WOOD = {125, 82, 45, 255};
static const Color COLOR_WOOD_LIGHT = {165, 115, 67, 255}; static const Color COLOR_STONE = {125, 125, 130, 255};
static const Color COLOR_STONE_DARK = {85, 85, 90, 255}; static const Color COLOR_LEAVES = {48, 115, 45, 255};
static const Color COLOR_LEAVES_DARK = {35, 88, 34, 255}; static const Color COLOR_ROOF = {125, 45, 38, 255};
static const Color COLOR_WATER = {45, 125, 190, 210}; static const Color COLOR_BED = {180, 45, 45, 255};
static const Color COLOR_WHITE = {225, 225, 220, 255};

void Game::buildWorld() {
    objects.clear(); zombies.clear(); projectiles.clear(); particles.clear(); ammoBoxes.clear();
    homeDoorOpen = false; homeDoorIndex = 0;

    buildGround(); buildTrees(); buildPlayerHouse();

    for (int i = 0; i < 16; i++) {
        float z = -55.0f + i * 7.0f; float x = 23.0f + std::sin(i * 0.55f) * 5.0f;
        addObject(&boxMesh, {x, -0.12f, z}, {7.0f, 0.12f, 8.0f}, COLOR_WATER);
    }

    for (int i = 0; i < 12; i++) {
        AmmoBox box; box.position = {randomRange(-65.0f, 65.0f), 0.5f, randomRange(-65.0f, 65.0f)}; box.active = true; ammoBoxes.push_back(box);
    }

    for (int i = 0; i < 18; i++) {
        Zombie z; z.position = {randomRange(-65.0f, 65.0f), 0.0f, randomRange(-65.0f, 65.0f)};
        if (distanceXZ(z.position, homeSpawn) < 18.0f) { z.position.x += 25.0f; z.position.z += 25.0f; }
        z.health = 100.0f; z.alive = true; zombies.push_back(z);
    }
}

void Game::buildGround() {
    const float tileSize = 10.0f;
    for (int x = -10; x < 10; x++) {
        for (int z = -10; z < 10; z++) {
            Color grass = COLOR_GRASS; if (((x + z) % 3 + 3) % 3 == 0) grass = COLOR_GRASS_DARK;
            float px = x * tileSize + tileSize * 0.5f; float pz = z * tileSize + tileSize * 0.5f;
            addObject(&boxMesh, {px, -0.25f, pz}, {tileSize, 0.5f, tileSize}, grass);
        }
    }
}

void Game::buildTrees() {
    for (int i = 0; i < 52; i++) {
        float x = randomRange(-90.0f, 90.0f); float z = randomRange(-90.0f, 90.0f);
        if (distanceXZ({x, 0, z}, homeSpawn) < 11.0f) continue;
        float height = randomRange(3.5f, 5.0f); float width = randomRange(1.0f, 1.6f);
        addObject(&boxMesh, {x, height * 0.5f, z}, {0.65f, height, 0.65f}, COLOR_WOOD);
        addObject(&boxMesh, {x, height + 0.9f, z}, {width * 2.3f, width * 1.8f, width * 2.3f}, COLOR_LEAVES);
    }
}

void Game::buildPlayerHouse() {
    const float cx = -12.0f; const float cz = 0.0f;
    addObject(&boxMesh, {cx, 0.0f, cz}, {8.0f, 0.35f, 8.0f}, COLOR_WOOD);
    addObject(&boxMesh, {cx - 3.85f, 1.65f, cz}, {0.3f, 3.3f, 8.0f}, COLOR_WOOD_LIGHT);
    addObject(&boxMesh, {cx + 3.85f, 1.65f, cz}, {0.3f, 3.3f, 8.0f}, COLOR_WOOD_LIGHT);
    addObject(&boxMesh, {cx, 1.65f, cz - 3.85f}, {8.0f, 3.3f, 0.3f}, COLOR_WOOD);
    addObject(&boxMesh, {cx - 2.6f, 1.65f, cz + 3.85f}, {2.8f, 3.3f, 0.3f}, COLOR_WOOD_LIGHT);
    addObject(&boxMesh, {cx + 2.6f, 1.65f, cz + 3.85f}, {2.8f, 3.3f, 0.3f}, COLOR_WOOD_LIGHT);
    addObject(&boxMesh, {cx, 3.45f, cz}, {8.7f, 0.45f, 8.7f}, COLOR_ROOF);
    homeDoorIndex = objects.size();
    addObject(&boxMesh, {cx, 1.1f, cz + 3.85f}, {1.9f, 2.2f, 0.18f}, {110, 70, 38, 255}, 0.0f, v3(-0.95f, 0.0f, 0.0f));
}

// ============================================================
// LOGIQUE DU JOUEUR & COLLISIONS
// ============================================================
Game::Game() { playerPosition = homeSpawn; }

bool Game::collidesWithHome(V3 p) {
    const bool insideHouse = p.x > -15.45f && p.x < -8.55f && p.z > -3.45f && p.z < 3.45f;
    if (insideHouse) return false;
    const bool insideHouseBounds = p.x > -16.2f && p.x < -7.8f && p.z > -4.2f && p.z < 4.2f;
    if (!insideHouseBounds) return false;
    const bool insideDoorway = p.x > -12.95f && p.x < -11.05f && p.z > 3.15f && p.z < 5.1f;
    if (homeDoorOpen && insideDoorway) return false;
    return true;
}

bool Game::collidesWithWorld(V3 p) {
    if (p.x < -MAP_LIMIT + 1.0f || p.x > MAP_LIMIT - 1.0f || p.z < -MAP_LIMIT + 1.0f || p.z > MAP_LIMIT - 1.0f) return true;
    if (collidesWithHome(p)) return true;
    return false;
}

void Game::interact() {
    const V3 doorPosition = v3(-12.0f, 0.0f, 4.0f);
    if (distanceXZ(playerPosition, doorPosition) > 3.2f) return;
    homeDoorOpen = !homeDoorOpen;
    if (homeDoorIndex < objects.size()) objects[homeDoorIndex].yaw = homeDoorOpen ? -1.35f : 0.0f;
}

void Game::collectAmmo() {
    for (size_t i = 0; i < ammoBoxes.size(); ++i) {
        AmmoBox& box = ammoBoxes[i]; if (!box.active) continue;
        if (distanceXZ(playerPosition, box.position) < 1.35f) {
            if (selectedWeapon == 0) pistolAmmo += box.amount; else rifleAmmo += box.amount;
            box.active = false;
            playSound(wavBufferReload, wavLengthReload); // SON DE RECHARGEMENT ICI
        }
    }
}

void Game::respawn() {
    playerPosition = homeSpawn; playerHealth = 100.0f; playerDead = false; deathTimer = 0.0f;
}

void Game::updatePlayer(float dt) {
    padUpdate(&pad);
    const u64 buttonsDown = padGetButtonsDown(&pad);
    const u64 buttonsHeld = padGetButtons(&pad);

    // LANCEMENT DIRECT SANS MENU TITRE
    if (playerDead) {
        deathTimer += dt; if ((buttonsDown & HidNpadButton_A) || deathTimer > 5.0f) respawn(); return;
    }
    if (buttonsDown & HidNpadButton_Minus) paused = !paused;
    if (paused) { if (buttonsDown & HidNpadButton_A) paused = false; return; }

    if (buttonsDown & HidNpadButton_X) interact();
    if (buttonsDown & HidNpadButton_Y) selectedWeapon = 1 - selectedWeapon;

    const HidAnalogStickState leftStick = padGetStickPos(&pad, 0);
    const HidAnalogStickState rightStick = padGetStickPos(&pad, 1);

    const float leftX = clampf((float)leftStick.x / 32767.0f, -1.0f, 1.0f);
    const float leftY = clampf((float)leftStick.y / 32767.0f, -1.0f, 1.0f);
    const float rightX = clampf((float)rightStick.x / 32767.0f, -1.0f, 1.0f);
    const float rightY = clampf((float)rightStick.y / 32767.0f, -1.0f, 1.0f);

    cameraYaw += rightX * 2.4f * dt; cameraPitch -= rightY * 1.8f * dt;
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
    if (buttonsHeld & HidNpadButton_ZR) fireWeapon();
    collectAmmo();
}

void Game::update(float dt) {
    dt = clampf(dt, 0.0f, 0.05f); updatePlayer(dt); if (paused || playerDead) return;
    if (weaponCooldown > 0.0f) weaponCooldown -= dt; if (muzzleFlash > 0.0f) muzzleFlash -= dt;
    if (weaponRecoil > 0.0f) { weaponRecoil -= dt * 5.0f; if (weaponRecoil < 0.0f) weaponRecoil = 0.0f; }
    if (hurtTimer > 0.0f) hurtTimer -= dt;
    updateZombies(dt); updateProjectiles(dt); updateParticles(dt);
    if (playerHealth <= 0.0f) { playerHealth = 0.0f; playerDead = true; deathTimer = 0.0f; }
}

// ============================================================
// COMBAT & IA
// ============================================================
void Game::fireWeapon() {
    if (playerDead || paused || weaponCooldown > 0.0f) return;
    int& ammunition = (selectedWeapon == 0) ? pistolAmmo : rifleAmmo;
    if (ammunition <= 0) { weaponCooldown = 0.25f; return; }
    --ammunition;
    playSound(wavBufferShot, wavLengthShot); // SON DU PISTOLET ICI

    const float directionX = std::sin(cameraYaw) * std::cos(cameraPitch); const float directionY = std::sin(cameraPitch); const float directionZ = std::cos(cameraYaw) * std::cos(cameraPitch);
    Projectile p; p.position = v3(playerPosition.x, playerPosition.y + CAMERA_HEIGHT, playerPosition.z);
    p.velocity = v3(directionX * 34.0f, directionY * 34.0f, directionZ * 34.0f);
    p.life = 2.0f; p.damage = 35.0f; p.active = true; projectiles.push_back(p);
    weaponCooldown = (selectedWeapon == 0) ? 0.32f : 0.12f; muzzleFlash = 0.075f; weaponRecoil = 1.0f;
}

void Game::spawnImpact(V3 position, Color color) {
    for (int i = 0; i < 8; ++i) {
        Particle p; p.position = position; p.velocity = v3(randomRange(-2.5f, 2.5f), randomRange(0.5f, 3.0f), randomRange(-2.5f, 2.5f)); p.color = color;
        particles.push_back(p);
    }
}

void Game::updateZombies(float dt) {
    for (Zombie& zombie : zombies) {
        if (!zombie.alive) continue;
        zombie.animationTime += dt; if (zombie.attackCooldown > 0.0f) zombie.attackCooldown -= dt;
        const float distance = distanceXZ(playerPosition, zombie.position);
        if (distance < 1.65f) {
            if (zombie.attackCooldown <= 0.0f) {
                if (random.value() < 0.3f) playSound(wavBufferZombie, wavLengthZombie); // SON DU ZOMBIE ICI
                playerHealth -= 9.0f; hurtTimer = 0.25f; zombie.attackCooldown = 1.0f;
            }
            continue;
        }
        if (distance > 0.01f && distance < 100.0f) {
            const float dx = playerPosition.x - zombie.position.x; const float dz = playerPosition.z - zombie.position.z;
            const float invDistance = 1.0f / distance;
            V3 candidate = zombie.position; candidate.x += dx * invDistance * ZOMBIE_SPEED * dt;
            if (!collidesWithWorld(candidate)) zombie.position.x = candidate.x;
            candidate = zombie.position; candidate.z += dz * invDistance * ZOMBIE_SPEED * dt;
            if (!collidesWithWorld(candidate)) zombie.position.z = candidate.z;
        }
    }
}

void Game::updateProjectiles(float dt) {
    for (Projectile& p : projectiles) {
        if (!p.active) continue;
        p.position = add(p.position, mul(p.velocity, dt)); p.life -= dt;
        if (p.life <= 0.0f) { p.active = false; continue; }
        for (Zombie& zombie : zombies) {
            if (!zombie.alive) continue;
            const V3 zc = v3(zombie.position.x, zombie.position.y + 1.0f, zombie.position.z);
            if (std::sqrt(std::pow(p.position.x - zc.x, 2) + std::pow(p.position.y - zc.y, 2) + std::pow(p.position.z - zc.z, 2)) < 0.85f) {
                zombie.health -= p.damage; p.active = false;
                if (zombie.health <= 0.0f) { zombie.alive = false; score += 100; }
                break;
            }
        }
    }
    projectiles.erase(std::remove_if(projectiles.begin(), projectiles.end(), [](const Projectile& p) { return !p.active; }), projectiles.end());
}

void Game::updateParticles(float dt) {
    for (Particle& p : particles) { p.position = add(p.position, mul(p.velocity, dt)); p.life -= dt; }
    particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle& p) { return p.life <= 0.0f; }), particles.end());
}

// ============================================================
// RENDU 3D
// ============================================================
void Game::addMeshTriangles(const Object& object, std::vector<Tri>& output) {
    if (!object.mesh) return;
    const Mesh& mesh = *object.mesh;
    const float cosYaw = std::cos(cameraYaw), sinYaw = std::sin(cameraYaw), cosPitch = std::cos(cameraPitch), sinPitch = std::sin(cameraPitch);
    const float objectCos = std::cos(object.yaw), objectSin = std::sin(object.yaw);
    const float cx = playerPosition.x, cy = playerPosition.y + CAMERA_HEIGHT, cz = playerPosition.z;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        Tri tri{}; bool valid = true; float depthSum = 0.0f;
        for (int j = 0; j < 3; ++j) {
            V3 v = mesh.vertices[mesh.indices[i + j]];
            v = sub(v, object.pivotOffset); v.x *= object.scale.x; v.y *= object.scale.y; v.z *= object.scale.z;
            float rx = v.x * objectCos - v.z * objectSin; float rz = v.x * objectSin + v.z * objectCos;
            v.x = rx; v.z = rz; v = add(v, object.pivotOffset); v = add(v, object.position);
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
    for (const AmmoBox& a : ammoBoxes) { if (!a.active) continue; Object o; o.mesh = &boxMesh; o.position = a.position; o.scale = v3(0.65f, 0.45f, 0.65f); o.color = {210, 170, 65, 255}; addMeshTriangles(o, triangles); }
    for (const Projectile& p : projectiles) { if (!p.active) continue; Object o; o.mesh = &sphereMesh; o.position = p.position; o.scale = v3(0.09f, 0.09f, 0.09f); o.color = {255, 210, 90, 255}; addMeshTriangles(o, triangles); }
    std::sort(triangles.begin(), triangles.end(), [](const Tri& a, const Tri& b) { return a.depth > b.depth; });
    for (const Tri& t : triangles) SDL_RenderGeometry(renderer, nullptr, t.v, 3, nullptr, 0);
}

void Game::drawZombie(const Zombie& z) {
    const Color green = {55, 135, 70, 255}; const Color dark = {35, 90, 48, 255};
    Object body; body.mesh = &boxMesh; body.position = v3(z.position.x, z.position.y + 0.95f, z.position.z); body.scale = v3(0.62f, 0.9f, 0.34f); body.color = dark; addMeshTriangles(body, triangles);
    Object head; head.mesh = &boxMesh; head.position = v3(z.position.x, z.position.y + 1.65f, z.position.z); head.scale = v3(0.55f, 0.55f, 0.55f); head.color = green; addMeshTriangles(head, triangles);
}

void Game::drawAllZombies() {
    for (const Zombie& z : zombies) if (z.alive && distanceXZ(playerPosition, z.position) < 115.0f) drawZombie(z);
    std::sort(triangles.begin(), triangles.end(), [](const Tri& a, const Tri& b) { return a.depth > b.depth; });
    for (const Tri& t : triangles) SDL_RenderGeometry(renderer, nullptr, t.v, 3, nullptr, 0);
}

void Game::drawWeapon() {
    int cx = SCREEN_W / 2, cy = SCREEN_H / 2;
    SDL_SetRenderDrawColor(renderer, 245, 245, 245, 230);
    SDL_RenderDrawLine(renderer, cx - 9, cy, cx - 3, cy); SDL_RenderDrawLine(renderer, cx + 3, cy, cx + 9, cy);
    SDL_Rect wb = {cx + 95, SCREEN_H - 145 + (int)(weaponRecoil * 22.0f), 145, 55};
    SDL_SetRenderDrawColor(renderer, 48, 48, 53, 255); SDL_RenderFillRect(renderer, &wb);
}

void Game::drawHud() {
    SDL_Rect panel = {18, SCREEN_H - 108, 310, 88}; SDL_SetRenderDrawColor(renderer, 15, 20, 18, 210); SDL_RenderFillRect(renderer, &panel);
    SDL_Rect health = {34, SCREEN_H - 82, (int)(230.0f * clampf(playerHealth / 100.0f, 0, 1)), 18}; SDL_SetRenderDrawColor(renderer, 65, 205, 80, 255); SDL_RenderFillRect(renderer, &health);
    if (hurtTimer > 0.0f) { SDL_SetRenderDrawColor(renderer, 180, 20, 20, (Uint8)(hurtTimer * 400)); SDL_Rect dmg = {0, 0, SCREEN_W, SCREEN_H}; SDL_RenderFillRect(renderer, &dmg); }
}

void Game::drawPauseScreen() { SDL_SetRenderDrawColor(renderer, 0, 0, 0, 175); SDL_Rect ov = {0, 0, SCREEN_W, SCREEN_H}; SDL_RenderFillRect(renderer, &ov); }
void Game::drawDeathScreen() { SDL_SetRenderDrawColor(renderer, 35, 0, 0, 190); SDL_Rect ov = {0, 0, SCREEN_W, SCREEN_H}; SDL_RenderFillRect(renderer, &ov); }

// ============================================================
// AUDIO ET INITIALISATION
// ============================================================
static Uint8* loadWav(const char* path, Uint32* len) {
    SDL_RWops* rw = SDL_RWFromFile(path, "rb"); if (!rw) return nullptr;
    SDL_AudioSpec spec; Uint8* buf = nullptr;
    if (SDL_LoadWAV_RW(rw, 1, &spec, &buf, len) == nullptr) return nullptr;
    return buf;
}

void Game::initAudio() {
    SDL_AudioSpec want, have; SDL_zero(want); want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 4096;
    audioDevice = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (audioDevice == 0) return; SDL_PauseAudioDevice(audioDevice, 0);
    wavBufferShot = loadWav("romfs/shot.wav", &wavLengthShot);
    wavBufferZombie = loadWav("romfs/zombie.wav", &wavLengthZombie);
    wavBufferReload = loadWav("romfs/reload.wav", &wavLengthReload);
}

void Game::playSound(Uint8* buf, Uint32 len) {
    if (audioDevice != 0 && buf != nullptr && len > 0) SDL_QueueAudio(audioDevice, buf, len);
}

bool Game::init() {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO);
    window = SDL_CreateWindow("Mioz Arena", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_W, SCREEN_H, 0);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    padInitializeDefault(&pad);
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
        if (paused) drawPauseScreen(); if (playerDead) drawDeathScreen();
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
