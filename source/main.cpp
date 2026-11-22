// ============================================================
// MINECRAFT VERSION ULTIME - Nintendo Switch V2
// C++ / SDL2 / libnx - Fichier unique corrigé
// ============================================================

#include <switch.h>
#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

// ------------------------------------------------------------
// CONFIGURATION GENERALE
// ------------------------------------------------------------

static constexpr int SCREEN_W = 1280;
static constexpr int SCREEN_H = 720;

static constexpr float PI = 3.14159265358979323846f;
static constexpr float TWO_PI = PI * 2.0f;

static constexpr float MAP_LIMIT = 98.0f;          // corrigé: collait au sol (100)
static constexpr float CAMERA_HEIGHT = 1.65f;
static constexpr float CAMERA_FOCAL = 650.0f;
static constexpr float NEAR_PLANE = 0.12f;

static constexpr int MAX_PARTICLES = 260;
static constexpr int MAX_PROJECTILES = 48;

static constexpr float PLAYER_SPEED = 7.0f;
static constexpr float ZOMBIE_SPEED = 2.25f;

// ------------------------------------------------------------
// STRUCTURES MATHEMATIQUES
// ------------------------------------------------------------

struct V2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct V3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Color {
    Uint8 r = 255;
    Uint8 g = 255;
    Uint8 b = 255;
    Uint8 a = 255;
};

struct Tri {
    SDL_Vertex v[3];
    float depth = 0.0f;
};

static V3 v3(float x, float y, float z) {
    return {x, y, z};
}

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

static float length(V3 a) {
    return std::sqrt(dot(a, a));
}

static V3 normalize(V3 a) {
    float l = length(a);

    if (l < 0.0001f)
        return {0.0f, 0.0f, 0.0f};

    return mul(a, 1.0f / l);
}

static V3 rotateY(V3 p, float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);

    return {
        p.x * c - p.z * s,
        p.y,
        p.x * s + p.z * c
    };
}

static float clampf(float value, float minimum, float maximum) {
    return std::max(minimum, std::min(value, maximum));
}

static float distanceXZ(V3 a, V3 b) {
    float dx = a.x - b.x;
    float dz = a.z - b.z;

    return std::sqrt(dx * dx + dz * dz);
}

static Color mixColor(Color a, Color b, float t) {
    t = clampf(t, 0.0f, 1.0f);

    return {
        (Uint8)(a.r + (b.r - a.r) * t),
        (Uint8)(a.g + (b.g - a.g) * t),
        (Uint8)(a.b + (b.b - a.b) * t),
        (Uint8)(a.a + (b.a - a.a) * t)
    };
}

// ------------------------------------------------------------
// GENERATEUR ALEATOIRE SIMPLE (LCG)
// ------------------------------------------------------------

struct RandomGenerator {
    uint32_t seed = 0x4D494F5A;

    uint32_t next() {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    }

    float value() {
        return (float)(next() & 0x00FFFFFFu) / 16777216.0f;
    }

    float range(float minimum, float maximum) {
        return minimum + value() * (maximum - minimum);
    }

    int integer(int minimum, int maximum) {
        return minimum + (int)(value() * (maximum - minimum + 1));
    }
};

// ------------------------------------------------------------
// MESHES 3D
// ------------------------------------------------------------

struct Mesh {
    std::vector<V3> vertices;
    std::vector<uint32_t> indices;
};

static Mesh makeBox() {
    Mesh mesh;

    mesh.vertices = {
        {-0.5f, -0.5f, -0.5f},
        { 0.5f, -0.5f, -0.5f},
        { 0.5f,  0.5f, -0.5f},
        {-0.5f,  0.5f, -0.5f},
        {-0.5f, -0.5f,  0.5f},
        { 0.5f, -0.5f,  0.5f},
        { 0.5f,  0.5f,  0.5f},
        {-0.5f,  0.5f,  0.5f}
    };

    uint32_t indices[] = {
        0, 2, 1, 0, 3, 2,
        4, 5, 6, 4, 6, 7,
        0, 1, 5, 0, 5, 4,
        2, 3, 7, 2, 7, 6,
        1, 2, 6, 1, 6, 5,
        3, 0, 4, 3, 4, 7
    };

    mesh.indices.assign(indices, indices + 36);
    return mesh;
}

static Mesh makeSphere(
    float radius = 1.0f,
    int rings = 8,
    int segments = 12
) {
    Mesh mesh;

    for (int y = 0; y <= rings; y++) {
        float v = (float)y / rings;
        float phi = v * PI;

        for (int x = 0; x <= segments; x++) {
            float u = (float)x / segments;
            float angle = u * TWO_PI;

            float px = radius * std::sin(phi) * std::cos(angle);
            float py = radius * std::cos(phi);
            float pz = radius * std::sin(phi) * std::sin(angle);

            mesh.vertices.push_back({px, py, pz});
        }
    }

    for (int y = 0; y < rings; y++) {
        for (int x = 0; x < segments; x++) {
            uint32_t a = y * (segments + 1) + x;
            uint32_t b = a + segments + 1;

            mesh.indices.push_back(a);
            mesh.indices.push_back(b);
            mesh.indices.push_back(a + 1);

            mesh.indices.push_back(a + 1);
            mesh.indices.push_back(b);
            mesh.indices.push_back(b + 1);
        }
    }

    return mesh;
}

static Mesh makeCylinder(
    float radius = 1.0f,
    float height = 1.0f,
    int segments = 12
) {
    Mesh mesh;

    for (int y = 0; y < 2; y++) {
        float py = y ? height * 0.5f : -height * 0.5f;

        for (int i = 0; i < segments; i++) {
            float angle = TWO_PI * (float)i / segments;

            mesh.vertices.push_back({
                radius * std::cos(angle),
                py,
                radius * std::sin(angle)
            });
        }
    }

    for (int i = 0; i < segments; i++) {
        uint32_t next = (i + 1) % segments;

        mesh.indices.insert(
            mesh.indices.end(),
            {
                (uint32_t)i,
                (uint32_t)(i + segments),
                next,

                next,
                (uint32_t)(i + segments),
                (uint32_t)(next + segments)
            }
        );
    }

    return mesh;
}

// ------------------------------------------------------------
// OBJETS DU MONDE
// ------------------------------------------------------------

struct Object {
    const Mesh* mesh = nullptr;

    V3 position;
    V3 scale{1.0f, 1.0f, 1.0f};

    float yaw = 0.0f;

    // Pivot de rotation (offset autour duquel tourner).
    // Par défaut (0,0,0) => rotation autour du centre.
    V3 pivotOffset{0.0f, 0.0f, 0.0f};

    Color color;
};

// ------------------------------------------------------------
// ZOMBIES / PROJECTILES / PARTICULES / CAISSES / NUAGES
// ------------------------------------------------------------

struct Zombie {
    V3 position;

    float attackCooldown = 0.0f;
    float animationTime = 0.0f;
    float health = 100.0f;

    bool alive = true;
    bool attacking = false;
};

struct Projectile {
    V3 position;
    V3 velocity;

    float life = 2.0f;
    float damage = 25.0f;

    bool active = true;
};

struct Particle {
    V3 position;
    V3 velocity;

    Color color;

    float life = 0.5f;
    float size = 0.08f;
};

struct AmmoBox {
    V3 position;
    int amount = 20;
    bool active = true;
};

struct Cloud {
    V3 position;
    float scale = 1.0f;
    float speed = 0.5f;
};

// ------------------------------------------------------------
// MODELES PARTAGES
// ------------------------------------------------------------

static Mesh boxMesh = makeBox();
static Mesh sphereMesh = makeSphere();
static Mesh cylinderMesh = makeCylinder();
// ------------------------------------------------------------
// CLASSE PRINCIPALE DU JEU
// ------------------------------------------------------------

class Game {
private:

    // Fenetre et rendu
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;

    // Joueur
    V3 playerPosition{-12.0f, 0.0f, -1.0f};

    float cameraYaw = 0.0f;
    float cameraPitch = 0.0f;

    float playerHealth = 100.0f;
    float hurtTimer = 0.0f;

    bool playerDead = false;
    float deathTimer = 0.0f;

    bool titleScreen = true;
    bool paused = false;

    // Maison et point de réapparition
    V3 homeSpawn{-12.0f, 0.0f, -1.0f};

    bool homeDoorOpen = false;
    size_t homeDoorIndex = 0;

    // Armes
    int selectedWeapon = 0;

    int pistolAmmo = 60;
    int rifleAmmo = 180;

    float weaponCooldown = 0.0f;
    float muzzleFlash = 0.0f;
    float weaponRecoil = 0.0f;

    // Monde
    RandomGenerator random;

    std::vector<Object> objects;
    std::vector<Zombie> zombies;
    std::vector<Projectile> projectiles;
    std::vector<Particle> particles;
    std::vector<AmmoBox> ammoBoxes;
    std::vector<Cloud> clouds;

    // Rendu
    std::vector<Tri> triangles;

    // Statistiques
    int score = 0;
    int zombiesKilled = 0;

    // ---- Fonctions internes (corrigées) ----

    float randomRange(float minimum, float maximum);

    void addObject(
        const Mesh* mesh,
        V3 position,
        V3 scale,
        Color color,
        float yaw = 0.0f,
        V3 pivotOffset = v3(0.0f, 0.0f, 0.0f)
    );

    void buildWorld();
    void buildGround();
    void buildTrees();
    void buildPlayerHouse();
    void buildBossHouse();

    void addMeshTriangles(const Object& object, std::vector<Tri>& output);

    void drawWorld();
    void drawZombie(const Zombie& zombie);
    void drawAllZombies();          // ajoutée
    void drawWeapon();
    void drawHud();
    void drawTitleScreen();         // ajoutée
    void drawPauseScreen();         // ajoutée
    void drawDeathScreen();         // ajoutée

    void update(float dt);
    void updatePlayer(float dt);
    void updateZombies(float dt);
    void updateProjectiles(float dt);
    void updateParticles(float dt);

    void fireWeapon();
    void spawnImpact(V3 position, Color color = Color{255, 220, 120, 255});
    void spawnMuzzleFlash();

    void respawn();
    void interact();
    void collectAmmo();

    bool collidesWithWorld(V3 p);   // signature corrigée
    bool collidesWithHome(V3 p);    // signature corrigée

public:

    Game();
    bool init();
    void run();
    ~Game();
};
// ============================================================
// COULEURS DU MONDE
// ============================================================

static const Color COLOR_GRASS       = {92, 155, 68, 255};
static const Color COLOR_GRASS_DARK  = {75, 130, 55, 255};
static const Color COLOR_DIRT        = {125, 85, 55, 255};
static const Color COLOR_WOOD        = {125, 82, 45, 255};
static const Color COLOR_WOOD_LIGHT  = {165, 115, 67, 255};
static const Color COLOR_STONE       = {125, 125, 130, 255};
static const Color COLOR_STONE_DARK  = {85, 85, 90, 255};
static const Color COLOR_LEAVES      = {48, 115, 45, 255};
static const Color COLOR_LEAVES_DARK = {35, 88, 34, 255};
static const Color COLOR_ROOF        = {125, 45, 38, 255};
static const Color COLOR_WATER       = {45, 125, 190, 210};
static const Color COLOR_BED         = {180, 45, 45, 255};
static const Color COLOR_WHITE       = {225, 225, 220, 255};

// ============================================================
// FONCTIONS DE CONSTRUCTION DU MONDE
// ============================================================

float Game::randomRange(float minimum, float maximum) {
    return random.range(minimum, maximum);
}

void Game::addObject(
    const Mesh* mesh,
    V3 position,
    V3 scale,
    Color color,
    float yaw,
    V3 pivotOffset
) {
    Object object;

    object.mesh = mesh;
    object.position = position;
    object.scale = scale;
    object.color = color;
    object.yaw = yaw;
    object.pivotOffset = pivotOffset;

    objects.push_back(object);
}

void Game::buildWorld() {
    objects.clear();
    zombies.clear();
    projectiles.clear();
    particles.clear();
    ammoBoxes.clear();
    clouds.clear();

    objects.reserve(2500);           // perf Switch

    homeDoorOpen = false;
    homeDoorIndex = 0;

    buildGround();
    buildTrees();
    buildPlayerHouse();
    buildBossHouse();

    // Riviere
    for (int i = 0; i < 16; i++) {
        float z = -55.0f + i * 7.0f;
        float x = 23.0f + std::sin(i * 0.55f) * 5.0f;

        addObject(
            &boxMesh,
            {x, -0.12f, z},
            {7.0f, 0.12f, 8.0f},
            COLOR_WATER
        );

        if (i % 2 == 0) {
            addObject(&boxMesh, {x - 4.2f, 0.05f, z + 1.5f}, {1.2f, 0.25f, 1.0f}, COLOR_STONE);
            addObject(&boxMesh, {x + 4.0f, 0.05f, z - 1.5f}, {1.0f, 0.25f, 1.2f}, COLOR_STONE_DARK);
        }
    }

    // Nuages
    for (int i = 0; i < 18; i++) {
        Cloud cloud;
        cloud.position = {
            randomRange(-85.0f, 85.0f),
            randomRange(23.0f, 32.0f),
            randomRange(-85.0f, 85.0f)
        };
        cloud.scale = randomRange(1.5f, 3.5f);
        cloud.speed = randomRange(0.15f, 0.45f);
        clouds.push_back(cloud);

        addObject(&boxMesh, cloud.position, {cloud.scale * 2.0f, 0.8f, cloud.scale}, {235, 239, 245, 255});
        addObject(&boxMesh,
            {cloud.position.x - cloud.scale * 0.7f, cloud.position.y - 0.1f, cloud.position.z},
            {cloud.scale, 0.7f, cloud.scale * 0.9f}, {245, 247, 250, 255});
        addObject(&boxMesh,
            {cloud.position.x + cloud.scale * 0.7f, cloud.position.y - 0.1f, cloud.position.z},
            {cloud.scale, 0.7f, cloud.scale * 0.9f}, {225, 231, 239, 255});
    }

    // Caisses de munitions
    for (int i = 0; i < 12; i++) {
        AmmoBox box;
        box.position = {randomRange(-65.0f, 65.0f), 0.5f, randomRange(-65.0f, 65.0f)};
        box.amount = random.integer(12, 35);
        box.active = true;
        ammoBoxes.push_back(box);
    }

    // Zombies initiaux
    for (int i = 0; i < 18; i++) {
        Zombie zombie;
        zombie.position = {randomRange(-65.0f, 65.0f), 0.0f, randomRange(-65.0f, 65.0f)};

        if (distanceXZ(zombie.position, homeSpawn) < 18.0f) {
            zombie.position.x += 25.0f;
            zombie.position.z += 25.0f;
        }

        zombie.health = 100.0f;
        zombie.alive = true;
        zombies.push_back(zombie);
    }
}

void Game::buildGround() {
    const float tileSize = 10.0f;

    for (int x = -10; x < 10; x++) {
        for (int z = -10; z < 10; z++) {
            Color grass = COLOR_GRASS;

            if (((x + z) % 3 + 3) % 3 == 0)   // modulo positif sûr
                grass = COLOR_GRASS_DARK;

            float px = x * tileSize + tileSize * 0.5f;
            float pz = z * tileSize + tileSize * 0.5f;

            addObject(&boxMesh, {px, -0.25f, pz}, {tileSize, 0.5f, tileSize}, grass);
            addObject(&boxMesh, {px, -0.65f, pz}, {tileSize, 0.35f, tileSize}, COLOR_DIRT);
        }
    }

    // Zones de pierre
    for (int i = 0; i < 28; i++) {
        float x = randomRange(-85.0f, 85.0f);
        float z = randomRange(-85.0f, 85.0f);

        if (distanceXZ({x, 0, z}, homeSpawn) < 12.0f)
            continue;

        addObject(&boxMesh, {x, 0.04f, z},
            {randomRange(0.6f, 1.4f), 0.12f, randomRange(0.6f, 1.4f)},
            COLOR_STONE);
    }
}

void Game::buildTrees() {
    for (int i = 0; i < 52; i++) {
        float x = randomRange(-90.0f, 90.0f);
        float z = randomRange(-90.0f, 90.0f);

        if (distanceXZ({x, 0, z}, homeSpawn) < 11.0f) continue;
        if (x > 14.0f && x < 33.0f) continue;

        float height = randomRange(3.5f, 5.0f);
        float width = randomRange(1.0f, 1.6f);

        addObject(&boxMesh, {x, height * 0.5f, z}, {0.65f, height, 0.65f}, COLOR_WOOD);
        addObject(&boxMesh, {x, height + 0.9f, z}, {width * 2.3f, width * 1.8f, width * 2.3f}, COLOR_LEAVES);
        addObject(&boxMesh, {x, height + 2.0f, z}, {width * 1.5f, width * 1.5f, width * 1.5f}, COLOR_LEAVES_DARK);
        addObject(&boxMesh, {x - width * 0.8f, height + 0.7f, z}, {width, 0.7f, width}, COLOR_LEAVES);
        addObject(&boxMesh, {x + width * 0.8f, height + 0.7f, z}, {width, 0.7f, width}, COLOR_LEAVES);
    }
}

// ------------------------------------------------------------
// MAISON DU JOUEUR (porte avec pivot sur gond)
// ------------------------------------------------------------

void Game::buildPlayerHouse() {
    const float cx = -12.0f;
    const float cz = 0.0f;

    // Plancher
    addObject(&boxMesh, {cx, 0.0f, cz}, {8.0f, 0.35f, 8.0f}, COLOR_WOOD);

    // Murs latéraux
    addObject(&boxMesh, {cx - 3.85f, 1.65f, cz}, {0.3f, 3.3f, 8.0f}, COLOR_WOOD_LIGHT);
    addObject(&boxMesh, {cx + 3.85f, 1.65f, cz}, {0.3f, 3.3f, 8.0f}, COLOR_WOOD_LIGHT);

    // Mur arrière
    addObject(&boxMesh, {cx, 1.65f, cz - 3.85f}, {8.0f, 3.3f, 0.3f}, COLOR_WOOD);

    // Façade avant
    addObject(&boxMesh, {cx - 2.6f, 1.65f, cz + 3.85f}, {2.8f, 3.3f, 0.3f}, COLOR_WOOD_LIGHT);
    addObject(&boxMesh, {cx + 2.6f, 1.65f, cz + 3.85f}, {2.8f, 3.3f, 0.3f}, COLOR_WOOD_LIGHT);
    addObject(&boxMesh, {cx, 2.75f, cz + 3.85f}, {2.4f, 1.1f, 0.3f}, COLOR_WOOD);

    // PORTE : pivot sur le gond gauche (x = -0.95 par rapport au centre)
    homeDoorIndex = objects.size();
    addObject(
        &boxMesh,
        {cx, 1.1f, cz + 3.85f},
        {1.9f, 2.2f, 0.18f},
        {110, 70, 38, 255},
        0.0f,
        v3(-0.95f, 0.0f, 0.0f)     // pivot à gauche
    );

    // Poignée (solidaire de la porte)
    addObject(
        &sphereMesh,
        {cx + 0.68f, 1.15f, cz + 3.70f},
        {0.15f, 0.15f, 0.15f},
        {210, 175, 65, 255}
    );

    // Toit
    addObject(&boxMesh, {cx, 3.45f, cz}, {8.7f, 0.45f, 8.7f}, COLOR_ROOF);
    addObject(&boxMesh, {cx - 4.15f, 3.45f, cz}, {0.35f, 0.65f, 8.9f}, {105, 40, 35, 255});
    addObject(&boxMesh, {cx + 4.15f, 3.45f, cz}, {0.35f, 0.65f, 8.9f}, {105, 40, 35, 255});

    // Lit
    addObject(&boxMesh, {-14.0f, 0.35f, -1.5f}, {2.6f, 0.35f, 1.4f}, COLOR_WOOD);
    addObject(&boxMesh, {-14.0f, 0.62f, -1.5f}, {2.5f, 0.35f, 1.3f}, COLOR_BED);
    addObject(&boxMesh, {-14.8f, 0.86f, -1.5f}, {0.65f, 0.18f, 1.05f}, COLOR_WHITE);

    // Table
    addObject(&boxMesh, {-9.7f, 0.75f, -2.3f}, {1.2f, 0.2f, 1.0f}, COLOR_WOOD_LIGHT);
    addObject(&boxMesh, {-10.15f, 0.35f, -2.65f}, {0.15f, 0.75f, 0.15f}, COLOR_WOOD);
    addObject(&boxMesh, {-9.25f, 0.35f, -2.65f}, {0.15f, 0.75f, 0.15f}, COLOR_WOOD);

    // Fenêtres
    addObject(&boxMesh, {-12.0f, 2.0f, -3.68f}, {1.5f, 1.0f, 0.08f}, {90, 170, 210, 255});
    addObject(&boxMesh, {-15.68f, 2.0f, 0.0f}, {0.08f, 1.0f, 1.5f}, {90, 170, 210, 255});
}

// ------------------------------------------------------------
// MAISON DU BOSS
// ------------------------------------------------------------

void Game::buildBossHouse() {
    const float cx = 80.0f;
    const float cz = 80.0f;

    addObject(&boxMesh, {cx, 0.0f, cz}, {12.0f, 0.45f, 12.0f}, COLOR_STONE_DARK);
    addObject(&boxMesh, {cx - 5.8f, 2.1f, cz}, {0.4f, 4.2f, 12.0f}, COLOR_STONE);
    addObject(&boxMesh, {cx + 5.8f, 2.1f, cz}, {0.4f, 4.2f, 12.0f}, COLOR_STONE);
    addObject(&boxMesh, {cx, 2.1f, cz - 5.8f}, {12.0f, 4.2f, 0.4f}, COLOR_STONE);
    addObject(&boxMesh, {cx - 4.0f, 2.1f, cz + 5.8f}, {4.0f, 4.2f, 0.4f}, COLOR_STONE);
    addObject(&boxMesh, {cx + 4.0f, 2.1f, cz + 5.8f}, {4.0f, 4.2f, 0.4f}, COLOR_STONE);
    addObject(&boxMesh, {cx, 1.6f, cz + 5.8f}, {3.5f, 3.2f, 0.35f}, {55, 40, 35, 255});
    addObject(&boxMesh, {cx, 4.4f, cz}, {12.8f, 0.5f, 12.8f}, COLOR_ROOF);

    for (int x = -1; x <= 1; x += 2) {
        for (int z = -1; z <= 1; z += 2) {
            addObject(&boxMesh, {cx + x * 5.3f, 4.0f, cz + z * 5.3f}, {1.8f, 8.0f, 1.8f}, COLOR_STONE_DARK);
            addObject(&boxMesh, {cx + x * 5.3f, 8.2f, cz + z * 5.3f}, {2.2f, 0.5f, 2.2f}, COLOR_ROOF);
        }
    }

    addObject(&boxMesh, {cx, 7.0f, cz}, {0.18f, 5.0f, 0.18f}, COLOR_WOOD);
    addObject(&boxMesh, {cx + 1.1f, 8.5f, cz}, {2.2f, 1.2f, 0.12f}, {180, 30, 30, 255});
}
// ============================================================
// GAMEPLAY : JOUEUR, COLLISIONS, INTERACTIONS
// ============================================================

Game::Game() {
    playerPosition = homeSpawn;
    cameraYaw = 0.0f;
    cameraPitch = 0.0f;
    playerHealth = 100.0f;
    hurtTimer = 0.0f;
    playerDead = false;
    deathTimer = 0.0f;

    titleScreen = true;
    paused = false;

    selectedWeapon = 0;
    pistolAmmo = 60;
    rifleAmmo = 180;
    weaponCooldown = 0.0f;
    muzzleFlash = 0.0f;
    weaponRecoil = 0.0f;

    score = 0;
    zombiesKilled = 0;
}

// ------------------------------------------------------------
// COLLISIONS
// ------------------------------------------------------------

bool Game::collidesWithHome(V3 p) {
    const float minX = -16.2f;
    const float maxX = -7.8f;
    const float minZ = -4.2f;
    const float maxZ = 4.2f;

    const bool insideHouse =
        p.x > -15.45f && p.x < -8.55f &&
        p.z > -3.45f && p.z < 3.45f;

    if (insideHouse)
        return false;

    const bool insideHouseBounds =
        p.x > minX && p.x < maxX &&
        p.z > minZ && p.z < maxZ;

    if (!insideHouseBounds)
        return false;

    // Passage de la porte (resserré à la vraie largeur de la porte)
    const bool insideDoorway =
        p.x > -12.95f && p.x < -11.05f &&
        p.z > 3.15f && p.z < 5.1f;

    if (homeDoorOpen && insideDoorway)
        return false;

    return true;
}

bool Game::collidesWithWorld(V3 p) {
    if (p.x < -MAP_LIMIT + 1.0f || p.x > MAP_LIMIT - 1.0f ||
        p.z < -MAP_LIMIT + 1.0f || p.z > MAP_LIMIT - 1.0f)
        return true;

    if (collidesWithHome(p))
        return true;

    return false;
}

// ------------------------------------------------------------
// INTERACTION PORTE
// ------------------------------------------------------------

void Game::interact() {
    const V3 doorPosition = v3(-12.0f, 0.0f, 4.0f);

    if (distanceXZ(playerPosition, doorPosition) > 3.2f)
        return;

    homeDoorOpen = !homeDoorOpen;

    if (homeDoorIndex < objects.size())
        objects[homeDoorIndex].yaw = homeDoorOpen ? -1.35f : 0.0f;
}

// ------------------------------------------------------------
// COLLECTE MUNITIONS
// ------------------------------------------------------------

void Game::collectAmmo() {
    for (size_t i = 0; i < ammoBoxes.size(); ++i) {
        AmmoBox& box = ammoBoxes[i];

        if (!box.active)
            continue;

        if (distanceXZ(playerPosition, box.position) < 1.35f) {
            if (selectedWeapon == 0) pistolAmmo += box.amount;
            else                     rifleAmmo += box.amount;

            box.active = false;

            spawnImpact(
                v3(box.position.x, 0.6f, box.position.z),
                Color{80, 220, 120, 255}
            );
        }
    }
}

// ------------------------------------------------------------
// RESPAWN
// ------------------------------------------------------------

void Game::respawn() {
    playerPosition = homeSpawn;

    playerHealth = 100.0f;
    playerDead = false;
    deathTimer = 0.0f;
    hurtTimer = 0.0f;

    cameraYaw = 0.0f;
    cameraPitch = 0.0f;

    weaponCooldown = 0.0f;
    muzzleFlash = 0.0f;
    weaponRecoil = 0.0f;

    if (homeDoorOpen) {
        homeDoorOpen = false;
        if (homeDoorIndex < objects.size())
            objects[homeDoorIndex].yaw = 0.0f;
    }
}

// ------------------------------------------------------------
// UPDATE JOUEUR
// ------------------------------------------------------------

void Game::updatePlayer(float dt) {
    hidScanInput();

    const u64 buttonsDown = hidKeysDown(CONTROLLER_P1_AUTO);
    const u64 buttonsHeld = hidKeysHeld(CONTROLLER_P1_AUTO);

    if (titleScreen) {
        if (buttonsDown & KEY_A) {
            titleScreen = false;
            respawn();
        }
        return;
    }

    if (playerDead) {
        deathTimer += dt;
        if ((buttonsDown & KEY_A) || deathTimer > 5.0f)
            respawn();
        return;
    }

    if (buttonsDown & KEY_MINUS)
        paused = !paused;

    if (paused) {
        if (buttonsDown & KEY_A)
            paused = false;
        return;
    }

    if (buttonsDown & KEY_X)   interact();
    if (buttonsDown & KEY_Y)   selectedWeapon = 1 - selectedWeapon;
    if (buttonsDown & KEY_DUP)   selectedWeapon = 0;
    if (buttonsDown & KEY_DDOWN) selectedWeapon = 1;

    HidAnalogStickState leftStick  = hidJoystickRead(CONTROLLER_P1_AUTO, JOYSTICK_LEFT);
    HidAnalogStickState rightStick = hidJoystickRead(CONTROLLER_P1_AUTO, JOYSTICK_RIGHT);

    const float leftX  = clampf((float)leftStick.x  / 32767.0f, -1.0f, 1.0f);
    const float leftY  = clampf((float)leftStick.y  / 32767.0f, -1.0f, 1.0f);
    const float rightX = clampf((float)rightStick.x / 32767.0f, -1.0f, 1.0f);
    const float rightY = clampf((float)rightStick.y / 32767.0f, -1.0f, 1.0f);

    cameraYaw   += rightX * 2.4f * dt;
    cameraPitch -= rightY * 1.8f * dt;
    cameraPitch  = clampf(cameraPitch, -0.85f, 0.85f);

    const float forwardX = std::sin(cameraYaw);
    const float forwardZ = std::cos(cameraYaw);
    const float rightMoveX =  std::cos(cameraYaw);
    const float rightMoveZ = -std::sin(cameraYaw);

    float moveX = forwardX * leftY + rightMoveX * leftX;
    float moveZ = forwardZ * leftY + rightMoveZ * leftX;

    const float moveLength = std::sqrt(moveX * moveX + moveZ * moveZ);

    if (moveLength > 0.12f) {
        if (moveLength > 1.0f) {
            moveX /= moveLength;
            moveZ /= moveLength;
        }

        const float stepX = moveX * PLAYER_SPEED * dt;
        const float stepZ = moveZ * PLAYER_SPEED * dt;

        V3 candidate = playerPosition;
        candidate.x += stepX;
        if (!collidesWithWorld(candidate))
            playerPosition.x = candidate.x;

        candidate = playerPosition;
        candidate.z += stepZ;
        if (!collidesWithWorld(candidate))
            playerPosition.z = candidate.z;
    }

    if (buttonsHeld & KEY_ZR)
        fireWeapon();

    collectAmmo();
}

// ------------------------------------------------------------
// UPDATE GLOBAL
// ------------------------------------------------------------

void Game::update(float dt) {
    dt = clampf(dt, 0.0f, 0.05f);

    updatePlayer(dt);

    if (titleScreen || paused || playerDead)
        return;

    if (weaponCooldown > 0.0f) weaponCooldown -= dt;
    if (muzzleFlash   > 0.0f) muzzleFlash   -= dt;

    if (weaponRecoil > 0.0f) {
        weaponRecoil -= dt * 5.0f;
        if (weaponRecoil < 0.0f) weaponRecoil = 0.0f;
    }

    if (hurtTimer > 0.0f) hurtTimer -= dt;

    updateZombies(dt);
    updateProjectiles(dt);
    updateParticles(dt);

    if (playerHealth <= 0.0f) {
        playerHealth = 0.0f;
        playerDead = true;
        deathTimer = 0.0f;
    }
}
// ============================================================
// MOTEUR 3D : PROJECTION ET RENDU
// ============================================================

void Game::addMeshTriangles(const Object& object, std::vector<Tri>& output) {
    if (object.mesh == nullptr) return;

    const Mesh& mesh = *object.mesh;

    const float cosYaw   = std::cos(cameraYaw);
    const float sinYaw   = std::sin(cameraYaw);
    const float cosPitch = std::cos(cameraPitch);
    const float sinPitch = std::sin(cameraPitch);

    const float objectCos = std::cos(object.yaw);
    const float objectSin = std::sin(object.yaw);

    const float cameraX = playerPosition.x;
    const float cameraY = playerPosition.y + CAMERA_HEIGHT;
    const float cameraZ = playerPosition.z;

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        Tri triangle{};
        bool valid = true;
        float depthSum = 0.0f;

        for (int j = 0; j < 3; ++j) {
            const uint32_t index = mesh.indices[i + j];
            if (index >= mesh.vertices.size()) { valid = false; break; }

            V3 vertex = mesh.vertices[index];

            // Pivot (rotation autour du gond si défini)
            vertex = sub(vertex, object.pivotOffset);

            vertex.x *= object.scale.x;
            vertex.y *= object.scale.y;
            vertex.z *= object.scale.z;

            const float rotatedX = vertex.x * objectCos - vertex.z * objectSin;
            const float rotatedZ = vertex.x * objectSin + vertex.z * objectCos;
            vertex.x = rotatedX;
            vertex.z = rotatedZ;

            // Reposition du pivot
            vertex = add(vertex, object.pivotOffset);
            vertex = add(vertex, object.position);

            const float dx = vertex.x - cameraX;
            const float dy = vertex.y - cameraY;
            const float dz = vertex.z - cameraZ;

            const float viewX = dx * cosYaw - dz * sinYaw;
            const float viewZ = dx * sinYaw + dz * cosYaw;
            const float viewY = dy * cosPitch - viewZ * sinPitch;
            const float finalZ = dy * sinPitch + viewZ * cosPitch;

            if (finalZ < NEAR_PLANE) { valid = false; break; }

            const float screenX = (SCREEN_W * 0.5f) + (viewX * CAMERA_FOCAL / finalZ);
            const float screenY = (SCREEN_H * 0.5f) - (viewY * CAMERA_FOCAL / finalZ);

            SDL_Vertex& out = triangle.v[j];
            out.position.x = screenX;
            out.position.y = screenY;
            out.color.r = object.color.r;
            out.color.g = object.color.g;
            out.color.b = object.color.b;
            out.color.a = object.color.a;
            out.tex_coord.x = 0.0f;
            out.tex_coord.y = 0.0f;
            depthSum += finalZ;
        }

        if (!valid) continue;
        triangle.depth = depthSum / 3.0f;

        bool visible = false;
        for (int j = 0; j < 3; ++j) {
            const float x = triangle.v[j].position.x;
            const float y = triangle.v[j].position.y;
            if (x >= -100.0f && x <= SCREEN_W + 100.0f &&
                y >= -100.0f && y <= SCREEN_H + 100.0f) {
                visible = true; break;
            }
        }

        if (visible) output.push_back(triangle);
    }
}

void Game::drawWorld() {
    triangles.clear();
    triangles.reserve(8000);   // perf Switch

    for (const Object& object : objects)
        addMeshTriangles(object, triangles);

    for (const AmmoBox& ammo : ammoBoxes) {
        if (!ammo.active) continue;

        Object boxObject;
        boxObject.mesh = &boxMesh;
        boxObject.position = ammo.position;
        boxObject.scale = v3(0.65f, 0.45f, 0.65f);
        boxObject.color = Color{210, 170, 65, 255};
        addMeshTriangles(boxObject, triangles);
    }

    for (const Projectile& projectile : projectiles) {
        if (!projectile.active) continue;

        Object p;
        p.mesh = &sphereMesh;
        p.position = projectile.position;
        p.scale = v3(0.09f, 0.09f, 0.09f);
        p.color = Color{255, 210, 90, 255};
        addMeshTriangles(p, triangles);
    }

    for (const Particle& particle : particles) {
        if (particle.life <= 0.0f) continue;

        Object p;
        p.mesh = &sphereMesh;
        p.position = particle.position;
        p.scale = v3(particle.size, particle.size, particle.size);
        p.color = particle.color;
        addMeshTriangles(p, triangles);
    }

    std::sort(triangles.begin(), triangles.end(),
        [](const Tri& a, const Tri& b) { return a.depth > b.depth; });

    for (const Tri& triangle : triangles) {
        SDL_RenderGeometry(renderer, nullptr, triangle.v, 3, nullptr, 0);
    }
}

// ------------------------------------------------------------
// ZOMBIES
// ------------------------------------------------------------

void Game::drawZombie(const Zombie& zombie) {
    if (!zombie.alive) return;

    const float distance = distanceXZ(playerPosition, zombie.position);
    if (distance > 115.0f) return;

    const float animation = std::sin(zombie.animationTime * 5.0f) * 0.08f;
    const float walk      = std::sin(zombie.animationTime * 7.0f) * 0.22f;

    const Color zombieGreen   = Color{55, 135, 70, 255};
    const Color zombieDark    = Color{35, 90, 48, 255};
    const Color zombieClothes = Color{50, 70, 135, 255};

    Object body;
    body.mesh = &boxMesh;
    body.position = v3(zombie.position.x, zombie.position.y + 0.95f + animation, zombie.position.z);
    body.scale = v3(0.62f, 0.9f, 0.34f);
    body.color = zombieClothes;
    addMeshTriangles(body, triangles);

    Object head;
    head.mesh = &boxMesh;
    head.position = v3(zombie.position.x, zombie.position.y + 1.65f + animation, zombie.position.z);
    head.scale = v3(0.55f, 0.55f, 0.55f);
    head.color = zombieGreen;
    addMeshTriangles(head, triangles);

    Object leftArm;
    leftArm.mesh = &boxMesh;
    leftArm.position = v3(zombie.position.x - 0.47f, zombie.position.y + 1.02f + walk, zombie.position.z);
    leftArm.scale = v3(0.20f, 0.75f, 0.22f);
    leftArm.color = zombieGreen;
    addMeshTriangles(leftArm, triangles);

    Object rightArm;
    rightArm.mesh = &boxMesh;
    rightArm.position = v3(zombie.position.x + 0.47f, zombie.position.y + 1.02f - walk, zombie.position.z);
    rightArm.scale = v3(0.20f, 0.75f, 0.22f);
    rightArm.color = zombieGreen;
    addMeshTriangles(rightArm, triangles);

    Object leftLeg;
    leftLeg.mesh = &boxMesh;
    leftLeg.position = v3(zombie.position.x - 0.17f, zombie.position.y + 0.30f - walk * 0.5f, zombie.position.z);
    leftLeg.scale = v3(0.22f, 0.60f, 0.25f);
    leftLeg.color = zombieDark;
    addMeshTriangles(leftLeg, triangles);

    Object rightLeg;
    rightLeg.mesh = &boxMesh;
    rightLeg.position = v3(zombie.position.x + 0.17f, zombie.position.y + 0.30f + walk * 0.5f, zombie.position.z);
    rightLeg.scale = v3(0.22f, 0.60f, 0.25f);
    rightLeg.color = zombieDark;
    addMeshTriangles(rightLeg, triangles);
}

void Game::drawAllZombies() {
    triangles.clear();
    triangles.reserve(4000);

    for (const Zombie& zombie : zombies)
        if (zombie.alive) drawZombie(zombie);

    std::sort(triangles.begin(), triangles.end(),
        [](const Tri& a, const Tri& b) { return a.depth > b.depth; });

    for (const Tri& triangle : triangles)
        SDL_RenderGeometry(renderer, nullptr, triangle.v, 3, nullptr, 0);
}

// ------------------------------------------------------------
// ARME
// ------------------------------------------------------------

void Game::drawWeapon() {
    const int centerX = SCREEN_W / 2;
    const int centerY = SCREEN_H / 2;

    // Viseur (rouge si en train de tirer)
    if (muzzleFlash > 0.0f)
        SDL_SetRenderDrawColor(renderer, 255, 90, 90, 235);
    else
        SDL_SetRenderDrawColor(renderer, 245, 245, 245, 230);

    SDL_RenderDrawLine(renderer, centerX - 9, centerY, centerX - 3, centerY);
    SDL_RenderDrawLine(renderer, centerX + 3, centerY, centerX + 9, centerY);
    SDL_RenderDrawLine(renderer, centerX, centerY - 9, centerX, centerY - 3);
    SDL_RenderDrawLine(renderer, centerX, centerY + 3, centerX, centerY + 9);

    const int recoilOffset = (int)(weaponRecoil * 22.0f);

    SDL_Rect weaponBody;
    if (selectedWeapon == 0)
        weaponBody = {centerX + 95, SCREEN_H - 145 + recoilOffset, 145, 55};
    else
        weaponBody = {centerX + 55, SCREEN_H - 165 + recoilOffset, 245, 70};

    SDL_SetRenderDrawColor(renderer, 48, 48, 53, 255);
    SDL_RenderFillRect(renderer, &weaponBody);

    SDL_Rect grip = {weaponBody.x + 25, weaponBody.y + weaponBody.h - 5, 45, 60};
    SDL_SetRenderDrawColor(renderer, 95, 72, 45, 255);
    SDL_RenderFillRect(renderer, &grip);

    if (muzzleFlash > 0.0f) {
        SDL_Rect flash = {weaponBody.x - 30, weaponBody.y + 8, 35, 35};
        SDL_SetRenderDrawColor(renderer, 255, 210, 75, 255);
        SDL_RenderFillRect(renderer, &flash);
    }
}

// ------------------------------------------------------------
// HUD (barres + indicateurs, sans police)
// ------------------------------------------------------------

static void drawFilledBar(
    SDL_Renderer* r, int x, int y, int w, int h,
    float ratio, Color bg, Color fg
) {
    SDL_Rect bgRect = {x, y, w, h};
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(r, &bgRect);

    int ww = (int)(w * clampf(ratio, 0.0f, 1.0f));
    if (ww > 0) {
        SDL_Rect fgRect = {x, y, ww, h};
        SDL_SetRenderDrawColor(r, fg.r, fg.g, fg.b, fg.a);
        SDL_RenderFillRect(r, &fgRect);
    }

    SDL_SetRenderDrawColor(r, 235, 235, 235, 255);
    SDL_RenderDrawRect(r, &bgRect);
}

void Game::drawHud() {
    // Panneau vie
    SDL_Rect panel = {18, SCREEN_H - 108, 310, 88};
    SDL_SetRenderDrawColor(renderer, 15, 20, 18, 210);
    SDL_RenderFillRect(renderer, &panel);

    drawFilledBar(renderer, 34, SCREEN_H - 82, 230, 18,
        playerHealth / 100.0f,
        Color{70, 35, 35, 255},
        Color{65, 205, 80, 255});

    // Munitions (barre visuelle)
    const int ammo = (selectedWeapon == 0) ? pistolAmmo : rifleAmmo;
    SDL_Rect ammoPanel = {SCREEN_W - 225, SCREEN_H - 100, 205, 75};
    SDL_SetRenderDrawColor(renderer, 15, 20, 18, 210);
    SDL_RenderFillRect(renderer, &ammoPanel);

    drawFilledBar(renderer, SCREEN_W - 210, SCREEN_H - 80, 175, 16,
        clampf((float)ammo / 200.0f, 0.0f, 1.0f),
        Color{40, 40, 30, 255},
        Color{220, 190, 70, 255});

    // Score (barre d'icônes)
    SDL_Rect scorePanel = {18, 18, 190, 42};
    SDL_SetRenderDrawColor(renderer, 15, 20, 18, 210);
    SDL_RenderFillRect(renderer, &scorePanel);

    int scoreBlocks = std::min(score / 100, 15);
    for (int i = 0; i < scoreBlocks; ++i) {
        SDL_Rect s = {26 + i * 10, 28, 8, 22};
        SDL_SetRenderDrawColor(renderer, 220, 200, 80, 255);
        SDL_RenderFillRect(renderer, &s);
    }

    // Compteur zombies
    SDL_Rect zPanel = {SCREEN_W / 2 - 115, 18, 230, 38};
    SDL_SetRenderDrawColor(renderer, 15, 20, 18, 210);
    SDL_RenderFillRect(renderer, &zPanel);

    int alive = 0;
    for (const Zombie& z : zombies) if (z.alive) ++alive;
    int zombieBlocks = std::min(alive, 20);
    for (int i = 0; i < zombieBlocks; ++i) {
        SDL_Rect s = {SCREEN_W / 2 - 100 + i * 10, 26, 8, 22};
        SDL_SetRenderDrawColor(renderer, 90, 200, 100, 255);
        SDL_RenderFillRect(renderer, &s);
    }

    // Arme sélectionnée
    SDL_Rect wPanel = {SCREEN_W - 225, 18, 205, 38};
    SDL_SetRenderDrawColor(renderer, 15, 20, 18, 210);
    SDL_RenderFillRect(renderer, &wPanel);

    if (selectedWeapon == 0) {
        SDL_Rect p = {SCREEN_W - 210, 28, 60, 18};
        SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
        SDL_RenderFillRect(renderer, &p);
    } else {
        SDL_Rect r1 = {SCREEN_W - 210, 28, 120, 18};
        SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
        SDL_RenderFillRect(renderer, &r1);
    }

    // Overlay de dégâts
    if (hurtTimer > 0.0f) {
        SDL_SetRenderDrawColor(renderer, 180, 20, 20, (Uint8)(hurtTimer * 400));
        SDL_Rect dmg = {0, 0, SCREEN_W, SCREEN_H};
        SDL_RenderFillRect(renderer, &dmg);
    }
}
// ============================================================
// COMBAT
// ============================================================

void Game::fireWeapon() {
    if (playerDead || titleScreen || paused) return;
    if (weaponCooldown > 0.0f) return;

    int& ammunition = (selectedWeapon == 0) ? pistolAmmo : rifleAmmo;

    if (ammunition <= 0) {
        weaponCooldown = 0.25f;
        return;
    }

    --ammunition;

    const float directionX = std::sin(cameraYaw) * std::cos(cameraPitch);
    const float directionY = std::sin(cameraPitch);
    const float directionZ = std::cos(cameraYaw) * std::cos(cameraPitch);

    Projectile projectile;
    projectile.position = v3(playerPosition.x, playerPosition.y + CAMERA_HEIGHT, playerPosition.z);
    projectile.velocity = v3(directionX * 34.0f, directionY * 34.0f, directionZ * 34.0f);
    projectile.life = 2.0f;
    projectile.damage = (selectedWeapon == 0) ? 35.0f : 24.0f;
    projectile.active = true;
    projectiles.push_back(projectile);

    weaponCooldown = (selectedWeapon == 0) ? 0.32f : 0.12f;
    muzzleFlash = 0.075f;
    weaponRecoil = 1.0f;

    spawnMuzzleFlash();
}

void Game::spawnImpact(V3 position, Color color) {
    for (int i = 0; i < 8; ++i) {
        Particle particle;
        particle.position = position;
        particle.velocity = v3(
            randomRange(-2.5f, 2.5f),
            randomRange(0.5f, 3.0f),
            randomRange(-2.5f, 2.5f)
        );
        particle.color = color;
        particle.life = randomRange(0.2f, 0.65f);
        particle.size = randomRange(0.035f, 0.09f);
        particles.push_back(particle);
    }

    if (particles.size() > MAX_PARTICLES) {
        particles.erase(particles.begin(),
            particles.begin() + (particles.size() - MAX_PARTICLES));
    }
}

void Game::spawnMuzzleFlash() {
    const float directionX = std::sin(cameraYaw);
    const float directionZ = std::cos(cameraYaw);

    V3 flashPosition = v3(
        playerPosition.x + directionX * 0.9f,
        playerPosition.y + CAMERA_HEIGHT - 0.2f,
        playerPosition.z + directionZ * 0.9f
    );

    spawnImpact(flashPosition, Color{255, 185, 55, 255});
}

// ------------------------------------------------------------
// IA ZOMBIES
// ------------------------------------------------------------

void Game::updateZombies(float dt) {
    for (Zombie& zombie : zombies) {
        if (!zombie.alive) continue;

        zombie.animationTime += dt;

        if (zombie.attackCooldown > 0.0f)
            zombie.attackCooldown -= dt;

        const float distance = distanceXZ(playerPosition, zombie.position);

        if (distance < 1.65f) {
            zombie.attacking = true;

            if (zombie.attackCooldown <= 0.0f) {
                playerHealth -= 9.0f;
                hurtTimer = 0.25f;
                zombie.attackCooldown = 1.0f;

                if (playerHealth < 0.0f) playerHealth = 0.0f;
            }
            continue;
        }

        zombie.attacking = false;

        if (distance > 0.01f && distance < 100.0f) {
            const float dx = playerPosition.x - zombie.position.x;
            const float dz = playerPosition.z - zombie.position.z;
            const float invDistance = 1.0f / distance;

            const float moveX = dx * invDistance * ZOMBIE_SPEED * dt;
            const float moveZ = dz * invDistance * ZOMBIE_SPEED * dt;

            V3 candidate = zombie.position;
            candidate.x += moveX;
            if (!collidesWithWorld(candidate)) zombie.position.x = candidate.x;

            candidate = zombie.position;
            candidate.z += moveZ;
            if (!collidesWithWorld(candidate)) zombie.position.z = candidate.z;
        }
    }
}

// ------------------------------------------------------------
// PROJECTILES
// ------------------------------------------------------------

void Game::updateProjectiles(float dt) {
    for (Projectile& projectile : projectiles) {
        if (!projectile.active) continue;

        projectile.position = add(projectile.position, mul(projectile.velocity, dt));
        projectile.life -= dt;

        if (projectile.life <= 0.0f) { projectile.active = false; continue; }

        for (Zombie& zombie : zombies) {
            if (!zombie.alive) continue;

            const V3 zombieCenter = v3(zombie.position.x, zombie.position.y + 1.0f, zombie.position.z);
            const float dx = projectile.position.x - zombieCenter.x;
            const float dy = projectile.position.y - zombieCenter.y;
            const float dz = projectile.position.z - zombieCenter.z;
            const float hitDistance = std::sqrt(dx * dx + dy * dy + dz * dz);

            if (hitDistance < 0.85f) {
                zombie.health -= projectile.damage;
                projectile.active = false;

                spawnImpact(projectile.position, Color{80, 220, 100, 255});

                if (zombie.health <= 0.0f) {
                    zombie.health = 0.0f;
                    zombie.alive = false;

                    ++zombiesKilled;
                    score += 100;

                    spawnImpact(zombieCenter, Color{65, 180, 85, 255});

                    if (random.value() < 0.25f) {
                        AmmoBox reward;
                        reward.position = zombie.position;
                        reward.amount = 15;
                        reward.active = true;
                        ammoBoxes.push_back(reward);
                    }
                }
                break;
            }
        }

        if (std::fabs(projectile.position.x) > MAP_LIMIT ||
            std::fabs(projectile.position.z) > MAP_LIMIT)
            projectile.active = false;
    }

    projectiles.erase(
        std::remove_if(projectiles.begin(), projectiles.end(),
            [](const Projectile& p) { return !p.active; }),
        projectiles.end());

    if (projectiles.size() > MAX_PROJECTILES) {
        projectiles.erase(projectiles.begin(),
            projectiles.begin() + (projectiles.size() - MAX_PROJECTILES));
    }
}

// ------------------------------------------------------------
// PARTICULES
// ------------------------------------------------------------

void Game::updateParticles(float dt) {
    for (Particle& particle : particles) {
        if (particle.life <= 0.0f) continue;

        particle.position = add(particle.position, mul(particle.velocity, dt));
        particle.velocity.y -= 5.5f * dt;
        particle.velocity.x *= 0.985f;
        particle.velocity.z *= 0.985f;

        if (particle.position.y < 0.05f) {
            particle.position.y = 0.05f;
            particle.velocity.y *= -0.25f;
        }

        particle.life -= dt;
    }

    particles.erase(
        std::remove_if(particles.begin(), particles.end(),
            [](const Particle& p) { return p.life <= 0.0f; }),
        particles.end());
}
// ============================================================
// INITIALISATION
// ============================================================

bool Game::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::printf("Erreur SDL : %s\n", SDL_GetError());
        return false;
    }

    window = SDL_CreateWindow(
        "Minecraft version ultime",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H, 0
    );

    if (window == nullptr) {
        std::printf("Erreur fenetre : %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == nullptr)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);

    if (renderer == nullptr) {
        std::printf("Erreur renderer : %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        window = nullptr;
        SDL_Quit();
        return false;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    buildWorld();
    playerPosition = homeSpawn;

    return true;
}

// ------------------------------------------------------------
// ÉCRAN TITRE
// ------------------------------------------------------------

void Game::drawTitleScreen() {
    SDL_SetRenderDrawColor(renderer, 10, 22, 16, 255);
    SDL_Rect bg = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer, &bg);

    SDL_SetRenderDrawColor(renderer, 35, 65, 42, 245);
    SDL_Rect titlePanel = {SCREEN_W / 2 - 330, SCREEN_H / 2 - 150, 660, 300};
    SDL_RenderFillRect(renderer, &titlePanel);

    SDL_SetRenderDrawColor(renderer, 100, 180, 85, 255);
    SDL_Rect titleLine = {SCREEN_W / 2 - 270, SCREEN_H / 2 - 95, 540, 12};
    SDL_RenderFillRect(renderer, &titleLine);

    // "MINECRAFT" stylisé en blocs
    const char* title = "MINECRAFT";
    int titleLen = 9;
    int blockW = 38, blockH = 38, gap = 8;
    int totalW = titleLen * blockW + (titleLen - 1) * gap;
    int startX = SCREEN_W / 2 - totalW / 2;

    for (int i = 0; i < titleLen; ++i) {
        SDL_Rect b = {startX + i * (blockW + gap), SCREEN_H / 2 - 60, blockW, blockH};
        SDL_SetRenderDrawColor(renderer, 80 + i * 10, 170, 90, 255);
        SDL_RenderFillRect(renderer, &b);
    }

    // "ULTIMATE" en dessous
    const char* sub = "ULTIMATE";
    int subLen = 8;
    int sBlockW = 22, sBlockH = 18, sGap = 4;
    int sTotalW = subLen * sBlockW + (subLen - 1) * sGap;
    int sStartX = SCREEN_W / 2 - sTotalW / 2;

    for (int i = 0; i < subLen; ++i) {
        SDL_Rect b = {sStartX + i * (sBlockW + sGap), SCREEN_H / 2 - 12, sBlockW, sBlockH};
        SDL_SetRenderDrawColor(renderer, 210, 200, 100, 255);
        SDL_RenderFillRect(renderer, &b);
    }

    // Bouton start
    SDL_SetRenderDrawColor(renderer, 210, 190, 95, 255);
    SDL_Rect startButton = {SCREEN_W / 2 - 150, SCREEN_H / 2 + 45, 300, 55};
    SDL_RenderFillRect(renderer, &startButton);

    SDL_SetRenderDrawColor(renderer, 35, 80, 45, 255);
    SDL_Rect buttonA = {SCREEN_W / 2 - 22, SCREEN_H / 2 + 57, 44, 30};
    SDL_RenderFillRect(renderer, &buttonA);
}

// ------------------------------------------------------------
// ÉCRAN PAUSE
// ------------------------------------------------------------

void Game::drawPauseScreen() {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 175);
    SDL_Rect overlay = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer, &overlay);

    SDL_SetRenderDrawColor(renderer, 55, 75, 60, 245);
    SDL_Rect panel = {SCREEN_W / 2 - 220, SCREEN_H / 2 - 100, 440, 200};
    SDL_RenderFillRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer, 200, 190, 100, 255);
    SDL_Rect indicator = {SCREEN_W / 2 - 110, SCREEN_H / 2 + 35, 220, 18};
    SDL_RenderFillRect(renderer, &indicator);

    // Bloc "PAUSE"
    int blockW = 26, gap = 6;
    int totalW = 5 * blockW + 4 * gap;
    int startX = SCREEN_W / 2 - totalW / 2;

    for (int i = 0; i < 5; ++i) {
        SDL_Rect b = {startX + i * (blockW + gap), SCREEN_H / 2 - 55, blockW, 34};
        SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
        SDL_RenderFillRect(renderer, &b);
    }
}

// ------------------------------------------------------------
// ÉCRAN MORT
// ------------------------------------------------------------

void Game::drawDeathScreen() {
    SDL_SetRenderDrawColor(renderer, 35, 0, 0, 190);
    SDL_Rect overlay = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer, &overlay);

    SDL_SetRenderDrawColor(renderer, 85, 25, 25, 245);
    SDL_Rect panel = {SCREEN_W / 2 - 230, SCREEN_H / 2 - 100, 460, 200};
    SDL_RenderFillRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer, 220, 65, 65, 255);
    SDL_Rect deathLine = {SCREEN_W / 2 - 130, SCREEN_H / 2 + 40, 260, 12};
    SDL_RenderFillRect(renderer, &deathLine);

    int blockW = 26, gap = 6;
    int totalW = 4 * blockW + 3 * gap;
    int startX = SCREEN_W / 2 - totalW / 2;

    for (int i = 0; i < 4; ++i) {
        SDL_Rect b = {startX + i * (blockW + gap), SCREEN_H / 2 - 55, blockW, 34};
        SDL_SetRenderDrawColor(renderer, 240, 80, 80, 255);
        SDL_RenderFillRect(renderer, &b);
    }
}

// ------------------------------------------------------------
// BOUCLE PRINCIPALE
// ------------------------------------------------------------

void Game::run() {
    bool quit = false;
    Uint64 lastCounter = SDL_GetPerformanceCounter();

    while (appletMainLoop() && !quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) quit = true;
        }

        const Uint64 currentCounter = SDL_GetPerformanceCounter();
        const Uint64 frequency      = SDL_GetPerformanceFrequency();

        float dt = 0.0f;
        if (frequency > 0)
            dt = (float)((double)(currentCounter - lastCounter) / (double)frequency);

        lastCounter = currentCounter;
        dt = clampf(dt, 0.0f, 0.05f);

        update(dt);

        SDL_SetRenderDrawColor(renderer, 105, 170, 220, 255);
        SDL_RenderClear(renderer);

        if (titleScreen) {
            drawTitleScreen();
        } else {
            drawWorld();
            drawAllZombies();

            if (!playerDead) {
                drawWeapon();
                drawHud();
            }

            if (paused)     drawPauseScreen();
            if (playerDead) drawDeathScreen();
        }

        SDL_RenderPresent(renderer);
    }
}

// ------------------------------------------------------------
// DESTRUCTEUR
// ------------------------------------------------------------

Game::~Game() {
    if (renderer != nullptr) { SDL_DestroyRenderer(renderer); renderer = nullptr; }
    if (window   != nullptr) { SDL_DestroyWindow(window);     window   = nullptr; }
    SDL_Quit();
}

// ------------------------------------------------------------
// MAIN
// ------------------------------------------------------------

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    Game game;

    if (!game.init()) {
        std::printf("Impossible de demarrer le jeu.\n");
        return 1;
    }

    game.run();
    return 0;
}

