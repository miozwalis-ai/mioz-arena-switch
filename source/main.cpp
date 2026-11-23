
#include <switch.h>
#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static constexpr int SCREEN_W = 1280;
static constexpr int SCREEN_H = 720;

static constexpr float PI = 3.14159265358979323846f;
static constexpr float TWO_PI = PI * 2.0f;

static constexpr float CAMERA_HEIGHT = 1.65f;
static constexpr float CAMERA_FOCAL = 650.0f;
static constexpr float NEAR_PLANE = 0.12f;

static constexpr float MAP_LIMIT = 98.0f;
static constexpr float PLAYER_SPEED = 7.0f;
static constexpr float ZOMBIE_SPEED = 2.25f;

static constexpr int MAX_PARTICLES = 260;
static constexpr int MAX_PROJECTILES = 48;

static constexpr float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static constexpr float sqrf(float v) {
    return v * v;
}

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
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

static float length(V3 a) {
    return std::sqrt(dot(a, a));
}

static V3 normalize(V3 a) {
    float l = length(a);
    if (l < 0.0001f) return v3(0, 0, 0);
    return mul(a, 1.0f / l);
}

static float distanceXZ(V3 a, V3 b) {
    float dx = a.x - b.x;
    float dz = a.z - b.z;
    return std::sqrt(dx*dx + dz*dz);
}

static Color color(Uint8 r, Uint8 g, Uint8 b, Uint8 a = 255) {
    return {r, g, b, a};
}

struct Mesh {
    std::vector<V3> vertices;
    std::vector<int> indices;
};

static Mesh makeBox() {
    Mesh m;

    m.vertices = {
        {-0.5f,-0.5f,-0.5f},
        { 0.5f,-0.5f,-0.5f},
        { 0.5f, 0.5f,-0.5f},
        {-0.5f, 0.5f,-0.5f},
        {-0.5f,-0.5f, 0.5f},
        { 0.5f,-0.5f, 0.5f},
        { 0.5f, 0.5f, 0.5f},
        {-0.5f, 0.5f, 0.5f}
    };

    m.indices = {
        0,2,1, 0,3,2,
        4,5,6, 4,6,7,
        0,1,5, 0,5,4,
        3,7,6, 3,6,2,
        0,4,7, 0,7,3,
        1,2,6, 1,6,5
    };

    return m;
}

static Mesh makeSphere(int rings = 8, int segments = 10) {
    Mesh m;

    for (int y = 0; y <= rings; ++y) {
        float v = static_cast<float>(y) / rings;
        float phi = v * PI;

        for (int x = 0; x <= segments; ++x) {
            float u = static_cast<float>(x) / segments;
            float theta = u * TWO_PI;

            float px = std::sin(phi) * std::cos(theta);
            float py = std::cos(phi);
            float pz = std::sin(phi) * std::sin(theta);

            m.vertices.push_back(v3(px * 0.5f, py * 0.5f, pz * 0.5f));
        }
    }

    for (int y = 0; y < rings; ++y) {
        for (int x = 0; x < segments; ++x) {
            int a = y * (segments + 1) + x;
            int b = a + segments + 1;

            m.indices.insert(m.indices.end(), {
                a, b, a + 1,
                a + 1, b, b + 1
            });
        }
    }

    return m;
}

struct Object {
    const Mesh* mesh = nullptr;
    V3 position{};
    V3 scale{1,1,1};
    float yaw = 0.0f;
    V3 pivotOffset{};
    Color tint{255,255,255,255};
};

struct Zombie {
    V3 position{};
    float attackCooldown = 0.0f;
    float animationTime = 0.0f;
    float health = 100.0f;
    bool alive = true;
};

struct Projectile {
    V3 position{};
    V3 velocity{};
    float life = 0.0f;
    bool active = false;
    bool rifle = false;
};

struct Particle {
    V3 position{};
    V3 velocity{};
    float life = 0.0f;
    float maxLife = 0.0f;
    Color tint{};
    bool active = false;
};

struct AmmoBox {
    V3 position{};
    bool active = true;
};

static Mesh boxMesh;
static Mesh sphereMesh;

class Game {
private:
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* frameTexture = nullptr;

    PadState pad{};

    SDL_AudioDeviceID audioDevice = 0;
    SDL_AudioSpec audioSpec{};

    Uint8* shotWav = nullptr;
    Uint32 shotWavLength = 0;

    Uint8* hitWav = nullptr;
    Uint32 hitWavLength = 0;

    Uint8* zombieWav = nullptr;
    Uint32 zombieWavLength = 0;

    V3 playerPosition{-12.0f, 0.0f, -1.0f};

    float cameraYaw = 0.0f;
    float cameraPitch = 0.0f;

    float playerHealth = 100.0f;
    float hurtTimer = 0.0f;
    float deathTimer = 0.0f;

    bool playerDead = false;
    bool paused = false;

    V3 homeSpawn{-12.0f, 0.0f, -1.0f};
    bool homeDoorOpen = false;
    int homeDoorIndex = -1;

    int selectedWeapon = 0;
    int pistolAmmo = 48;
    int rifleAmmo = 120;

    float weaponCooldown = 0.0f;
    float muzzleFlash = 0.0f;
    float weaponRecoil = 0.0f;

    uint32_t randomState = 0x12345678;

    std::vector<Object> objects;
    std::vector<Zombie> zombies;
    std::vector<Projectile> projectiles;
    std::vector<Particle> particles;
    std::vector<AmmoBox> ammoBoxes;
    std::vector<Tri> triangles;

    int score = 0;
    int zombiesKilled = 0;

    Uint32* pixelBuffer = nullptr;
    float* depthBuffer = nullptr;

    float randomFloat(float minValue, float maxValue);
    int randomInt(int minValue, int maxValue);

    void addObject(
        const Mesh* mesh,
        V3 position,
        V3 scale,
        Color tint,
        float yaw = 0.0f
    );

    void buildWorld();
    void buildGround();
    void buildTrees();
    void buildPlayerHouse();

    void addMeshTriangles(const Object& object);
    void addZombieTriangles(const Zombie& zombie);

    void renderTriangles();
    void drawWorld();
    void drawWeapon();
    void drawHud();
    void drawPauseScreen();
    void drawDeathScreen();

    void update(float dt);
    void updatePlayer(float dt);
    void updateZombies(float dt);
    void updateProjectiles(float dt);
    void updateParticles(float dt);

    void fireWeapon();
    void spawnImpact(V3 position, Color tint, int amount);
    void respawn();
    void interact();
    void collectAmmo();

    bool collidesWithHome(V3 position) const;
    bool collidesWithWorld(V3 position) const;
    bool collidesWithZombie(V3 position) const;

    void initAudio();
    void playSound(const Uint8* data, Uint32 length);

public:
    Game() = default;
    ~Game();

    bool init();
    void run();
};
float Game::randomFloat(float minValue, float maxValue) {
    if (randomState == 0u) {
        randomState = 0x9E3779B9u;
    }

    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;

    float t = static_cast<float>(randomState & 0x00FFFFFFu) /
              static_cast<float>(0x01000000u);

    return minValue + (maxValue - minValue) * t;
}

int Game::randomInt(int minValue, int maxValue) {
    if (maxValue <= minValue) return minValue;

    long long span = static_cast<long long>(maxValue) -
                     static_cast<long long>(minValue) + 1LL;

    return minValue + static_cast<int>(
        randomFloat(0.0f, static_cast<float>(span))
    );
}

void Game::addObject(
    const Mesh* mesh,
    V3 position,
    V3 scale,
    Color tint,
    float yaw
) {
    Object object;
    object.mesh = mesh;
    object.position = position;
    object.scale = scale;
    object.tint = tint;
    object.yaw = yaw;

    objects.push_back(object);
}

void Game::buildGround() {
    for (int x = -95; x <= 95; x += 10) {
        for (int z = -95; z <= 95; z += 10) {
            Color groundColor = ((x / 10 + z / 10) & 1)
                ? color(83, 145, 65)
                : color(91, 153, 71);

            addObject(
                &boxMesh,
                v3(static_cast<float>(x), -0.25f, static_cast<float>(z)),
                v3(10.0f, 0.5f, 10.0f),
                groundColor
            );
        }
    }
}

void Game::buildTrees() {
    for (int i = 0; i < 52; ++i) {
        float x = randomFloat(-85.0f, 85.0f);
        float z = randomFloat(-85.0f, 85.0f);

        if (x > -22.0f && x < -2.0f &&
            z > -12.0f && z < 12.0f) {
            continue;
        }

        float height = randomFloat(2.5f, 4.0f);

        addObject(
            &boxMesh,
            v3(x, height * 0.5f, z),
            v3(0.55f, height, 0.55f),
            color(105, 70, 42)
        );

        addObject(
            &boxMesh,
            v3(x, height + 0.55f, z),
            v3(2.8f, 2.2f, 2.8f),
            color(
                static_cast<Uint8>(35 + randomInt(0, 15)),
                static_cast<Uint8>(105 + randomInt(0, 30)),
                42
            )
        );
    }
}

void Game::buildPlayerHouse() {
    // Sol intérieur
    addObject(
        &boxMesh,
        v3(-12.0f, -0.05f, 0.0f),
        v3(8.0f, 0.35f, 8.0f),
        color(135, 103, 72)
    );

    // Mur gauche
    addObject(
        &boxMesh,
        v3(-15.85f, 1.65f, 0.0f),
        v3(0.3f, 3.3f, 8.0f),
        color(145, 145, 145)
    );

    // Mur droit
    addObject(
        &boxMesh,
        v3(-8.15f, 1.65f, 0.0f),
        v3(0.3f, 3.3f, 8.0f),
        color(145, 145, 145)
    );

    // Mur arrière
    addObject(
        &boxMesh,
        v3(-12.0f, 1.65f, -3.85f),
        v3(8.0f, 3.3f, 0.3f),
        color(145, 145, 145)
    );

    // Mur avant gauche
    addObject(
        &boxMesh,
        v3(-14.7f, 1.65f, 3.85f),
        v3(2.6f, 3.3f, 0.3f),
        color(145, 145, 145)
    );

    // Mur avant droit
    addObject(
        &boxMesh,
        v3(-9.3f, 1.65f, 3.85f),
        v3(2.6f, 3.3f, 0.3f),
        color(145, 145, 145)
    );

    // Toit
    addObject(
        &boxMesh,
        v3(-12.0f, 3.45f, 0.0f),
        v3(8.4f, 0.35f, 8.4f),
        color(90, 65, 48)
    );

    // Porte
    Object door;
    door.mesh = &boxMesh;
    door.position = v3(-12.0f, 1.1f, 3.85f);
    door.scale = v3(1.9f, 2.2f, 0.18f);
    door.tint = color(105, 64, 35);
    door.pivotOffset = v3(-0.95f, 0.0f, 0.0f);

    homeDoorIndex = static_cast<int>(objects.size());
    objects.push_back(door);
}

void Game::buildWorld() {
    objects.clear();
    zombies.clear();
    projectiles.clear();
    particles.clear();
    ammoBoxes.clear();

    buildGround();
    buildTrees();
    buildPlayerHouse();

    // Zone d'eau
    addObject(
        &boxMesh,
        v3(30.0f, -0.12f, 25.0f),
        v3(22.0f, 0.12f, 16.0f),
        color(45, 125, 190, 210)
    );

    // Boîtes de munitions
    for (int i = 0; i < 12; ++i) {
        AmmoBox box;
        box.position = v3(
            randomFloat(-70.0f, 70.0f),
            0.45f,
            randomFloat(-70.0f, 70.0f)
        );
        box.active = true;
        ammoBoxes.push_back(box);
    }

    // Zombies
    for (int i = 0; i < 18; ++i) {
        Zombie zombie;
        zombie.position = v3(
            randomFloat(-65.0f, 65.0f),
            0.0f,
            randomFloat(-65.0f, 65.0f)
        );

        if (distanceXZ(zombie.position, homeSpawn) < 15.0f) {
            zombie.position.x += 25.0f;
        }

        zombies.push_back(zombie);
    }
}

void Game::addMeshTriangles(const Object& object) {
    if (!object.mesh) return;

    const Mesh& mesh = *object.mesh;

    const float cy = std::cos(object.yaw);
    const float sy = std::sin(object.yaw);

    const float cameraCosYaw = std::cos(cameraYaw);
    const float cameraSinYaw = std::sin(cameraYaw);

    const float cameraCosPitch = std::cos(cameraPitch);
    const float cameraSinPitch = std::sin(cameraPitch);

    const V3 cameraPosition = v3(
        playerPosition.x,
        playerPosition.y + CAMERA_HEIGHT,
        playerPosition.z
    );

    struct ClipVertex {
        float x, y, z;
    };

    auto transform = [&](V3 vertex) -> ClipVertex {
        vertex.x *= object.scale.x;
        vertex.y *= object.scale.y;
        vertex.z *= object.scale.z;

        vertex.x -= object.pivotOffset.x;
        vertex.y -= object.pivotOffset.y;
        vertex.z -= object.pivotOffset.z;

        float worldX = vertex.x * cy + vertex.z * sy;
        float worldZ = -vertex.x * sy + vertex.z * cy;

        worldX += object.position.x;
        worldZ += object.position.z;

        float worldY = vertex.y + object.position.y;

        float dx = worldX - cameraPosition.x;
        float dy = worldY - cameraPosition.y;
        float dz = worldZ - cameraPosition.z;

        float viewX = dx * cameraCosYaw + dz * cameraSinYaw;
        float viewZ = -dx * cameraSinYaw + dz * cameraCosYaw;

        float viewY = dy * cameraCosPitch - viewZ * cameraSinPitch;
        float finalZ = dy * cameraSinPitch + viewZ * cameraCosPitch;

        return {viewX, viewY, finalZ};
    };

    auto interpolate = [](ClipVertex a, ClipVertex b, float t) {
        return ClipVertex{
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t
        };
    };

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        int ia = mesh.indices[i];
        int ib = mesh.indices[i + 1];
        int ic = mesh.indices[i + 2];

        if (ia < 0 || ib < 0 || ic < 0 ||
            ia >= static_cast<int>(mesh.vertices.size()) ||
            ib >= static_cast<int>(mesh.vertices.size()) ||
            ic >= static_cast<int>(mesh.vertices.size())) {
            continue;
        }

        ClipVertex input[3] = {
            transform(mesh.vertices[ia]),
            transform(mesh.vertices[ib]),
            transform(mesh.vertices[ic])
        };

        ClipVertex polygon[4];
        int count = 0;

        for (int j = 0; j < 3; ++j) {
            ClipVertex a = input[j];
            ClipVertex b = input[(j + 1) % 3];

            bool aInside = a.z >= NEAR_PLANE;
            bool bInside = b.z >= NEAR_PLANE;

            float denom = b.z - a.z;

            if (aInside && bInside) {
                if (count < 4) polygon[count++] = b;
            } else if (aInside && !bInside) {
                if (std::fabs(denom) > 1e-6f && count < 4) {
                    float t = (NEAR_PLANE - a.z) / denom;
                    polygon[count++] = interpolate(a, b, t);
                }
            } else if (!aInside && bInside) {
                if (std::fabs(denom) > 1e-6f && count < 3) {
                    float t = (NEAR_PLANE - a.z) / denom;
                    polygon[count++] = interpolate(a, b, t);
                }
                if (count < 4) {
                    polygon[count++] = b;
                }
            }
        }

        if (count < 3) continue;

        for (int j = 1; j + 1 < count; ++j) {
            ClipVertex tri[3] = {
                polygon[0],
                polygon[j],
                polygon[j + 1]
            };

            Tri output{};

            bool valid = true;
            float depth = 0.0f;

            for (int k = 0; k < 3; ++k) {
                if (tri[k].z < NEAR_PLANE ||
                    !std::isfinite(tri[k].z)) {
                    valid = false;
                    break;
                }

                float screenX = SCREEN_W * 0.5f +
                    tri[k].x * CAMERA_FOCAL / tri[k].z;

                float screenY = SCREEN_H * 0.5f -
                    tri[k].y * CAMERA_FOCAL / tri[k].z;

                if (!std::isfinite(screenX) ||
                    !std::isfinite(screenY)) {
                    valid = false;
                    break;
                }

                output.v[k].position = {
                    screenX,
                    screenY
                };

                output.v[k].color = {
                    object.tint.r,
                    object.tint.g,
                    object.tint.b,
                    object.tint.a
                };

                output.v[k].tex_coord = {0.0f, 0.0f};

                depth += tri[k].z;
            }

            if (!valid) continue;

            output.depth = depth / 3.0f;
            triangles.push_back(output);
        }
    }
}

void Game::addZombieTriangles(const Zombie& zombie) {
    if (!zombie.alive) return;

    float yaw = std::atan2(
        playerPosition.x - zombie.position.x,
        playerPosition.z - zombie.position.z
    );

    float walk = std::sin(zombie.animationTime * 7.0f) * 0.12f;

    Object body;
    body.mesh = &boxMesh;
    body.position = add(zombie.position, v3(0.0f, 0.95f, 0.0f));
    body.scale = v3(0.65f, 0.9f, 0.38f);
    body.yaw = yaw;
    body.tint = color(48, 115, 55);
    addMeshTriangles(body);

    Object head;
    head.mesh = &boxMesh;
    head.position = add(zombie.position, v3(0.0f, 1.65f, 0.0f));
    head.scale = v3(0.58f, 0.58f, 0.58f);
    head.yaw = yaw;
    head.tint = color(83, 145, 75);
    addMeshTriangles(head);

    // Yeux visibles sur la face avant du zombie
    for (int side = -1; side <= 1; side += 2) {
        float localX = side * 0.14f;

        V3 eyeOffset = v3(
            localX * std::cos(yaw) + 0.30f * std::sin(yaw),
            1.70f,
            -localX * std::sin(yaw) + 0.30f * std::cos(yaw)
        );

        Object eye;
        eye.mesh = &boxMesh;
        eye.position = add(zombie.position, eyeOffset);
        eye.scale = v3(0.10f, 0.09f, 0.045f);
        eye.yaw = yaw;
        eye.tint = color(235, 235, 220);
        addMeshTriangles(eye);
    }

    // Bras
    for (int side = -1; side <= 1; side += 2) {
        float armX = side * 0.48f;

        V3 armOffset = v3(
            armX * std::cos(yaw) + walk * std::sin(yaw),
            0.95f,
            -armX * std::sin(yaw) + walk * std::cos(yaw)
        );

        Object arm;
        arm.mesh = &boxMesh;
        arm.position = add(zombie.position, armOffset);
        arm.scale = v3(0.20f, 0.72f, 0.22f);
        arm.yaw = yaw;
        arm.tint = color(70, 130, 65);
        addMeshTriangles(arm);
    }

    // Jambes
    for (int side = -1; side <= 1; side += 2) {
        float legX = side * 0.18f;

        V3 legOffset = v3(
            legX * std::cos(yaw) + walk * side * std::sin(yaw),
            0.30f,
            -legX * std::sin(yaw) + walk * side * std::cos(yaw)
        );

        Object leg;
        leg.mesh = &boxMesh;
        leg.position = add(zombie.position, legOffset);
        leg.scale = v3(0.23f, 0.55f, 0.25f);
        leg.yaw = yaw;
        leg.tint = color(45, 70, 115);
        addMeshTriangles(leg);
    }
}

void Game::renderTriangles() {
    std::sort(
        triangles.begin(),
        triangles.end(),
        [](const Tri& a, const Tri& b) {
            return a.depth > b.depth;
        }
    );

    for (const Tri& triangle : triangles) {
        SDL_RenderGeometry(
            renderer,
            nullptr,
            const_cast<SDL_Vertex*>(triangle.v),
            3,
            nullptr,
            0
        );
    }
}

void Game::drawWorld() {
    triangles.clear();

    for (const Object& object : objects) {
        addMeshTriangles(object);
    }

    // Boîtes de munitions
    for (const AmmoBox& box : ammoBoxes) {
        if (!box.active) continue;

        Object ammo;
        ammo.mesh = &boxMesh;
        ammo.position = box.position;
        ammo.scale = v3(0.75f, 0.75f, 0.75f);
        ammo.tint = color(220, 175, 35);
        addMeshTriangles(ammo);
    }

    // Projectiles
    for (const Projectile& projectile : projectiles) {
        if (!projectile.active) continue;

        Object bullet;
        bullet.mesh = &sphereMesh;
        bullet.position = projectile.position;
        bullet.scale = v3(0.13f, 0.13f, 0.13f);
        bullet.tint = color(255, 190, 65);
        addMeshTriangles(bullet);
    }

    // Particules
    for (const Particle& particle : particles) {
        if (!particle.active) continue;

        Object particleObject;
        particleObject.mesh = &sphereMesh;
        particleObject.position = particle.position;
        particleObject.scale = v3(0.09f, 0.09f, 0.09f);
        particleObject.tint = particle.tint;
        addMeshTriangles(particleObject);
    }

    // Zombies dans le même rendu que le décor
    for (const Zombie& zombie : zombies) {
        addZombieTriangles(zombie);
    }

    renderTriangles();
}
void Game::drawWeapon() {
    int recoilOffset = static_cast<int>(weaponRecoil * 25.0f);

    // Arme dessinée en 2D pour rester visible
    SDL_Rect weaponBody = {
        SCREEN_W / 2 + 80,
        SCREEN_H - 125 + recoilOffset,
        190,
        75
    };

    SDL_SetRenderDrawColor(renderer, 40, 43, 48, 255);
    SDL_RenderFillRect(renderer, &weaponBody);

    SDL_Rect barrel = {
        SCREEN_W / 2 + 220,
        SCREEN_H - 145 + recoilOffset,
        110,
        30
    };

    SDL_SetRenderDrawColor(renderer, 65, 68, 72, 255);
    SDL_RenderFillRect(renderer, &barrel);

    SDL_Rect grip = {
        SCREEN_W / 2 + 105,
        SCREEN_H - 70 + recoilOffset,
        55,
        85
    };

    SDL_SetRenderDrawColor(renderer, 75, 55, 38, 255);
    SDL_RenderFillRect(renderer, &grip);

    if (muzzleFlash > 0.0f) {
        SDL_Rect flash = {
            SCREEN_W / 2 + 325,
            SCREEN_H - 155 + recoilOffset,
            50,
            50
        };

        SDL_SetRenderDrawColor(renderer, 255, 210, 80, 255);
        SDL_RenderFillRect(renderer, &flash);
    }
}

void Game::drawHud() {
    SDL_Rect healthBackground = {25, 25, 220, 22};
    SDL_Rect healthBar = {
        25,
        25,
        static_cast<int>(220.0f * clampf(playerHealth / 100.0f, 0.0f, 1.0f)),
        22
    };

    SDL_SetRenderDrawColor(renderer, 35, 35, 35, 220);
    SDL_RenderFillRect(renderer, &healthBackground);

    SDL_SetRenderDrawColor(renderer, 195, 45, 45, 255);
    SDL_RenderFillRect(renderer, &healthBar);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

    // Croix de visée
    SDL_RenderDrawLine(
        renderer,
        SCREEN_W / 2 - 8,
        SCREEN_H / 2,
        SCREEN_W / 2 + 8,
        SCREEN_H / 2
    );

    SDL_RenderDrawLine(
        renderer,
        SCREEN_W / 2,
        SCREEN_H / 2 - 8,
        SCREEN_W / 2,
        SCREEN_H / 2 + 8
    );

    // Compteur de munitions sous forme de rectangles
    int ammo = selectedWeapon == 0 ? pistolAmmo : rifleAmmo;

    for (int i = 0; i < std::min(ammo, 30); ++i) {
        SDL_Rect bullet = {
            SCREEN_W - 45 - (i % 10) * 15,
            30 + (i / 10) * 12,
            9,
            7
        };

        SDL_SetRenderDrawColor(renderer, 235, 190, 65, 255);
        SDL_RenderFillRect(renderer, &bullet);
    }

    // Indicateur de score
    SDL_Rect scoreBar = {25, 58, std::min(score, 500), 8};
    SDL_SetRenderDrawColor(renderer, 230, 190, 70, 255);
    SDL_RenderFillRect(renderer, &scoreBar);
}

void Game::drawPauseScreen() {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);

    SDL_Rect overlay = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer, &overlay);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

    SDL_Rect pauseBox = {
        SCREEN_W / 2 - 100,
        SCREEN_H / 2 - 35,
        200,
        70
    };

    SDL_RenderDrawRect(renderer, &pauseBox);
}

void Game::drawDeathScreen() {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 100, 0, 0, 190);

    SDL_Rect overlay = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer, &overlay);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

    SDL_Rect deathBox = {
        SCREEN_W / 2 - 150,
        SCREEN_H / 2 - 45,
        300,
        90
    };

    SDL_RenderDrawRect(renderer, &deathBox);
}

bool Game::collidesWithHome(V3 p) const {
    const bool insideRoom =
        p.x > -15.45f && p.x < -8.55f &&
        p.z > -3.45f && p.z < 3.45f;

    if (insideRoom) return false;

    const bool insideHouseBounds =
        p.x > -16.2f && p.x < -7.8f &&
        p.z > -4.2f && p.z < 4.2f;

    if (!insideHouseBounds) return false;

    const bool inDoorway =
        p.x > -13.25f && p.x < -10.75f &&
        p.z > 3.15f && p.z < 5.2f;

    if (homeDoorOpen && inDoorway) return false;

    return true;
}

bool Game::collidesWithZombie(V3 p) const {
    for (const Zombie& zombie : zombies) {
        if (!zombie.alive) continue;

        if (distanceXZ(p, zombie.position) < 0.65f) {
            return true;
        }
    }

    return false;
}

bool Game::collidesWithWorld(V3 p) const {
    if (p.x < -MAP_LIMIT || p.x > MAP_LIMIT ||
        p.z < -MAP_LIMIT || p.z > MAP_LIMIT) {
        return true;
    }

    if (collidesWithHome(p)) return true;

    // Collision simplifiée avec les arbres
    for (const Object& object : objects) {
        if (object.scale.y < 2.0f) continue;
        if (object.position.y < 0.5f) continue;

        float dx = p.x - object.position.x;
        float dz = p.z - object.position.z;

        if (std::fabs(dx) < 0.45f &&
            std::fabs(dz) < 0.45f) {
            if (object.position.x < -15.0f ||
                object.position.x > -9.0f ||
                object.position.z < -4.5f ||
                object.position.z > 4.5f) {
                return true;
            }
        }
    }

    return false;
}

void Game::interact() {
    V3 doorPosition = v3(-12.0f, 0.0f, 3.85f);

    if (distanceXZ(playerPosition, doorPosition) > 4.2f) {
        return;
    }

    homeDoorOpen = !homeDoorOpen;

    if (homeDoorIndex >= 0 &&
        homeDoorIndex < static_cast<int>(objects.size())) {
        Object& door = objects[homeDoorIndex];

        door.yaw = homeDoorOpen ? -1.35f : 0.0f;

        if (homeDoorOpen) {
            door.position = v3(-12.95f, 1.1f, 3.35f);
        } else {
            door.position = v3(-12.0f, 1.1f, 3.85f);
        }
    }
}

void Game::collectAmmo() {
    for (AmmoBox& box : ammoBoxes) {
        if (!box.active) continue;

        if (distanceXZ(playerPosition, box.position) < 1.2f) {
            pistolAmmo += 12;
            rifleAmmo += 30;
            box.active = false;
        }
    }
}

void Game::spawnImpact(V3 position, Color tint, int amount) {
    if (amount <= 0) return;

    for (int i = 0; i < amount; ++i) {
        Particle particle;

        particle.position = position;
        particle.velocity = v3(
            randomFloat(-2.0f, 2.0f),
            randomFloat(0.2f, 2.5f),
            randomFloat(-2.0f, 2.0f)
        );

        particle.life = randomFloat(0.2f, 0.7f);
        particle.maxLife = particle.life;
        particle.tint = tint;
        particle.active = true;

        if (particles.size() < MAX_PARTICLES) {
            particles.push_back(particle);
        } else if (!particles.empty()) {
            size_t maxIndex = std::min<size_t>(
                particles.size() - 1,
                static_cast<size_t>(MAX_PARTICLES - 1)
            );
            particles[static_cast<size_t>(randomInt(0, static_cast<int>(maxIndex)))] = particle;
        }
    }
}

void Game::fireWeapon() {
    if (weaponCooldown > 0.0f || playerDead) return;

    int& ammo = selectedWeapon == 0 ? pistolAmmo : rifleAmmo;

    if (ammo <= 0) return;

    --ammo;

    weaponCooldown = selectedWeapon == 0 ? 0.28f : 0.10f;
    muzzleFlash = 0.07f;
    weaponRecoil = 1.0f;

    playSound(shotWav, shotWavLength);

    float cp = std::cos(cameraPitch);
    float sp = std::sin(cameraPitch);

    V3 direction = normalize(v3(
        std::sin(cameraYaw) * cp,
        sp,
        std::cos(cameraYaw) * cp
    ));

    Projectile projectile;
    projectile.position = v3(
        playerPosition.x,
        playerPosition.y + CAMERA_HEIGHT,
        playerPosition.z
    );

    projectile.velocity = mul(
        direction,
        selectedWeapon == 0 ? 38.0f : 55.0f
    );

    projectile.life = 2.0f;
    projectile.active = true;
    projectile.rifle = selectedWeapon == 1;

    bool stored = false;

    for (Projectile& existing : projectiles) {
        if (!existing.active) {
            existing = projectile;
            stored = true;
            break;
        }
    }

    if (!stored && projectiles.size() < MAX_PROJECTILES) {
        projectiles.push_back(projectile);
    }
}

void Game::updateProjectiles(float dt) {
    for (Projectile& projectile : projectiles) {
        if (!projectile.active) continue;

        projectile.position = add(
            projectile.position,
            mul(projectile.velocity, dt)
        );

        projectile.life -= dt;

        if (projectile.life <= 0.0f) {
            projectile.active = false;
            continue;
        }

        for (Zombie& zombie : zombies) {
            if (!zombie.alive) continue;

            V3 target = add(zombie.position, v3(0.0f, 1.0f, 0.0f));

            if (length(sub(projectile.position, target)) < 0.75f) {
                zombie.health -= projectile.rifle ? 55.0f : 35.0f;

                spawnImpact(
                    projectile.position,
                    color(155, 35, 30),
                    8
                );

                playSound(hitWav, hitWavLength);

                projectile.active = false;

                if (zombie.health <= 0.0f) {
                    zombie.alive = false;
                    ++zombiesKilled;
                    score += 100;
                }

                break;
            }
        }
    }
}

void Game::updateParticles(float dt) {
    for (Particle& particle : particles) {
        if (!particle.active) continue;

        particle.position = add(
            particle.position,
            mul(particle.velocity, dt)
        );

        particle.velocity.y -= 4.0f * dt;
        particle.life -= dt;

        if (particle.life <= 0.0f) {
            particle.active = false;
        }
    }
}
        void Game::updateZombies(float dt) {
    for (Zombie& zombie : zombies) {
        if (!zombie.alive) continue;

        zombie.animationTime += dt;

        V3 difference = sub(playerPosition, zombie.position);
        float dist = length(v3(difference.x, 0.0f, difference.z));

        if (dist < 22.0f && dist > 1.1f) {
            V3 direction = normalize(
                v3(difference.x, 0.0f, difference.z)
            );

            V3 next = add(
                zombie.position,
                mul(direction, ZOMBIE_SPEED * dt)
            );

            if (!collidesWithHome(next)) {
                zombie.position = next;
            }
        }

        if (dist < 1.45f) {
            zombie.attackCooldown -= dt;

            if (zombie.attackCooldown <= 0.0f) {
                zombie.attackCooldown = 1.0f;
                playerHealth = std::max(0.0f, playerHealth - 10.0f);
                hurtTimer = 0.25f;

                playSound(zombieWav, zombieWavLength);

                if (playerHealth <= 0.0f) {
                    playerDead = true;
                    deathTimer = 0.0f;
                }
            }
        }
    }
}

void Game::updatePlayer(float dt) {
    padUpdate(&pad);

    u64 down = padGetButtonsDown(&pad);
    u64 held = padGetButtons(&pad);

    if (down & HidNpadButton_Plus) {
        paused = !paused;
    }

    if (paused || playerDead) return;

    if (down & HidNpadButton_X) {
        interact();
    }

    if (down & HidNpadButton_Y) {
        selectedWeapon = 1 - selectedWeapon;
    }

    if (held & HidNpadButton_R) {
        fireWeapon();
    }

    HidAnalogStickState leftStick = padGetStickPos(&pad, 0);
    HidAnalogStickState rightStick = padGetStickPos(&pad, 1);

    float leftX = clampf(
        static_cast<float>(leftStick.x) / 32767.0f,
        -1.0f,
        1.0f
    );

    float leftY = clampf(
        static_cast<float>(leftStick.y) / 32767.0f,
        -1.0f,
        1.0f
    );

    float rightX = clampf(
        static_cast<float>(rightStick.x) / 32767.0f,
        -1.0f,
        1.0f
    );

    float rightY = clampf(
        static_cast<float>(rightStick.y) / 32767.0f,
        -1.0f,
        1.0f
    );

    auto deadZone = [](float value) {
        const float zone = 0.13f;

        if (std::fabs(value) < zone) return 0.0f;

        float sign = value < 0.0f ? -1.0f : 1.0f;

        return sign * (std::fabs(value) - zone) / (1.0f - zone);
    };

    leftX = deadZone(leftX);
    leftY = deadZone(leftY);
    rightX = deadZone(rightX);
    rightY = deadZone(rightY);

    cameraYaw += rightX * 2.5f * dt;
    cameraPitch += rightY * 2.0f * dt;

    cameraPitch = clampf(cameraPitch, -0.85f, 0.85f);

    float forward = leftY;
    float sideways = leftX;

    V3 movement = v3(
        std::sin(cameraYaw) * forward +
            std::cos(cameraYaw) * sideways,
        0.0f,
        std::cos(cameraYaw) * forward -
            std::sin(cameraYaw) * sideways
    );

    float movementLength = length(movement);

    if (movementLength > 1.0f) {
        movement = normalize(movement);
    }

    V3 next = add(
        playerPosition,
        mul(movement, PLAYER_SPEED * dt)
    );

    V3 nextX = playerPosition;
    nextX.x = next.x;

    if (!collidesWithWorld(nextX)) {
        playerPosition.x = nextX.x;
    }

    V3 nextZ = playerPosition;
    nextZ.z = next.z;

    if (!collidesWithWorld(nextZ)) {
        playerPosition.z = nextZ.z;
    }

    collectAmmo();
}

void Game::respawn() {
    playerPosition = homeSpawn;
    playerHealth = 100.0f;

    playerDead = false;
    deathTimer = 0.0f;
    hurtTimer = 0.0f;

    homeDoorOpen = false;

    if (homeDoorIndex >= 0 &&
        homeDoorIndex < static_cast<int>(objects.size())) {
        objects[homeDoorIndex].yaw = 0.0f;
        objects[homeDoorIndex].position = v3(-12.0f, 1.1f, 3.85f);
    }

    for (Zombie& zombie : zombies) {
        if (!zombie.alive) {
            zombie.alive = true;
            zombie.health = 100.0f;
        }
    }
}

void Game::update(float dt) {
    dt = clampf(dt, 0.0f, 0.05f);

    if (hurtTimer > 0.0f) {
        hurtTimer = std::max(0.0f, hurtTimer - dt);
    }

    if (muzzleFlash > 0.0f) {
        muzzleFlash = std::max(0.0f, muzzleFlash - dt);
    }

    if (weaponCooldown > 0.0f) {
        weaponCooldown = std::max(0.0f, weaponCooldown - dt);
    }

    if (weaponRecoil > 0.0f) {
        weaponRecoil = std::max(0.0f, weaponRecoil - dt * 5.0f);
    }

    if (playerDead) {
        deathTimer += dt;

        padUpdate(&pad);

        if (padGetButtonsDown(&pad) & HidNpadButton_A) {
            respawn();
        }

        return;
    }

    updatePlayer(dt);

    if (paused) return;

    updateZombies(dt);
    updateProjectiles(dt);
    updateParticles(dt);
}

void Game::playSound(const Uint8* data, Uint32 length) {
    if (!audioDevice || !data || length == 0) return;

    // Évite de laisser une longue file de sons en attente.
    if (SDL_GetQueuedAudioSize(audioDevice) > 1024 * 1024) {
        SDL_ClearQueuedAudio(audioDevice);
    }

    SDL_QueueAudio(audioDevice, data, length);
}

void Game::initAudio() {
    SDL_AudioSpec desired{};
    desired.freq = 44100;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 2048;

    audioDevice = SDL_OpenAudioDevice(
        nullptr,
        0,
        &desired,
        &audioSpec,
        0
    );

    if (!audioDevice) {
        std::printf("Audio indisponible : %s\n", SDL_GetError());
        return;
    }

    auto loadSound = [&](const char* path,
                         Uint8*& destination,
                         Uint32& destinationLength) {
        SDL_AudioSpec sourceSpec{};
        Uint8* sourceData = nullptr;
        Uint32 sourceLength = 0;

        if (!SDL_LoadWAV(
                path,
                &sourceSpec,
                &sourceData,
                &sourceLength)) {
            std::printf("Impossible de charger %s : %s\n",
                        path, SDL_GetError());
            return;
        }

        SDL_AudioCVT converter{};

        int conversionNeeded = SDL_BuildAudioCVT(
            &converter,
            sourceSpec.format,
            sourceSpec.channels,
            sourceSpec.freq,
            audioSpec.format,
            audioSpec.channels,
            audioSpec.freq
        );

        if (conversionNeeded < 0) {
            std::printf("Conversion audio impossible : %s\n", path);
            SDL_FreeWAV(sourceData);
            return;
        }

        if (conversionNeeded == 0) {
            destination = static_cast<Uint8*>(
                SDL_malloc(sourceLength)
            );

            if (destination) {
                std::memcpy(destination, sourceData, sourceLength);
                destinationLength = sourceLength;
            }

            SDL_FreeWAV(sourceData);
            return;
        }

        converter.len = static_cast<int>(sourceLength);

        size_t allocSize = static_cast<size_t>(sourceLength) *
                         static_cast<size_t>(converter.len_mult);

        converter.buf = static_cast<Uint8*>(SDL_malloc(allocSize));

        if (!converter.buf) {
            SDL_FreeWAV(sourceData);
            return;
        }

        std::memcpy(converter.buf, sourceData, sourceLength);

        if (SDL_ConvertAudio(&converter) != 0) {
            std::printf("Erreur conversion audio : %s\n", path);
            SDL_free(converter.buf);
            SDL_FreeWAV(sourceData);
            return;
        }

        destination = converter.buf;
        destinationLength = static_cast<Uint32>(converter.len_cvt);

        SDL_FreeWAV(sourceData);
    };

    loadSound("romfs/shot.wav", shotWav, shotWavLength);
    loadSound("romfs/hit.wav", hitWav, hitWavLength);
    loadSound("romfs/zombie.wav", zombieWav, zombieWavLength);

    SDL_PauseAudioDevice(audioDevice, 0);
}

Game::~Game() {
    if (audioDevice) {
        SDL_ClearQueuedAudio(audioDevice);
        SDL_CloseAudioDevice(audioDevice);
        audioDevice = 0;
    }

    if (shotWav) {
        SDL_free(shotWav);
        shotWav = nullptr;
    }

    if (hitWav) {
        SDL_free(hitWav);
        hitWav = nullptr;
    }

    if (zombieWav) {
        SDL_free(zombieWav);
        zombieWav = nullptr;
    }

    if (frameTexture) {
        SDL_DestroyTexture(frameTexture);
        frameTexture = nullptr;
    }

    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }

    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    if (depthBuffer) {
        std::free(depthBuffer);
        depthBuffer = nullptr;
    }

    if (pixelBuffer) {
        std::free(pixelBuffer);
        pixelBuffer = nullptr;
    }

#ifdef __SWITCH__
    romfsExit();
#endif

    SDL_Quit();
}

bool Game::init() {
#ifdef __SWITCH__
    Result rc = romfsInit();
    if (R_FAILED(rc)) {
        std::printf("Erreur romfsInit : 0x%x\n", rc);
    }
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::printf("Erreur SDL_Init : %s\n", SDL_GetError());
        return false;
    }

    window = SDL_CreateWindow(
        "MIOZ ARENA SWITCH",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        SCREEN_W,
        SCREEN_H,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        std::printf("Erreur fenêtre : %s\n", SDL_GetError());
        return false;
    }

    renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_ACCELERATED
    );

    if (!renderer) {
        renderer = SDL_CreateRenderer(
            window,
            -1,
            SDL_RENDERER_SOFTWARE
        );
    }

    if (!renderer) {
        std::printf("Erreur renderer : %s\n", SDL_GetError());
        return false;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    boxMesh = makeBox();
    sphereMesh = makeSphere();

#ifdef __SWITCH__
    padConfigureInput(1, HidNpadIdType_NoPads);
    padInitializeDefault(&pad);
#else
    padInitialize(
        &pad,
        HidNpadIdType_Handheld,
        HidNpadStyleTag_NpadHandheld
    );
#endif

    initAudio();
    buildWorld();

    return true;
}

void Game::run() {
    Uint64 previousTime = SDL_GetPerformanceCounter();
    const double frequency =
        static_cast<double>(SDL_GetPerformanceFrequency());

    bool running = true;

    while (running) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }

        Uint64 currentTime = SDL_GetPerformanceCounter();

        float dt = static_cast<float>(
            static_cast<double>(currentTime - previousTime) /
            frequency
        );

        previousTime = currentTime;

        update(dt);

        SDL_SetRenderDrawColor(renderer, 105, 165, 210, 255);
        SDL_RenderClear(renderer);

        drawWorld();

        if (!playerDead) {
            drawWeapon();
            drawHud();
        }

        if (paused) {
            drawPauseScreen();
        }

        if (playerDead) {
            drawDeathScreen();
        }

        SDL_RenderPresent(renderer);
    }
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    Game game;

    if (!game.init()) {
        return 1;
    }

    game.run();

    return 0;
}

