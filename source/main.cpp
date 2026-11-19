#include <switch.h>
#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

static constexpr int SCREEN_W = 1280;
static constexpr int SCREEN_H = 720;
static constexpr float PI = 3.1415926535f;
static constexpr float MAP_LIMIT = 106.0f;

struct Vec3 {
    float x, y, z;
};

struct Color {
    Uint8 r, g, b, a;
};

struct Triangle {
    SDL_Vertex vertices[3];
    float depth;
};

static Vec3 add(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

static Vec3 sub(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

static Vec3 rotateY(Vec3 p, float angle) {
    const float c = cosf(angle);
    const float s = sinf(angle);

    return {
        p.x * c - p.z * s,
        p.y,
        p.x * s + p.z * c
    };
}

struct Mesh {
    std::vector<Vec3> points;
    std::vector<uint32_t> indices;
};

static Mesh makeBox(float x, float y, float z) {
    Mesh mesh;

    mesh.points = {
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

    mesh.indices.assign(faces, faces + 36);
    return mesh;
}

static Mesh makeSphere(float radius, int rings = 7, int segments = 10) {
    Mesh mesh;

    for (int y = 0; y <= rings; y++) {
        float v = float(y) / rings;
        float phi = v * PI;

        for (int x = 0; x <= segments; x++) {
            float u = float(x) / segments;
            float angle = u * 2.0f * PI;

            mesh.points.push_back({
                radius * sinf(phi) * cosf(angle),
                radius * cosf(phi),
                radius * sinf(phi) * sinf(angle)
            });
        }
    }

    for (int y = 0; y < rings; y++) {
        for (int x = 0; x < segments; x++) {
            uint32_t a = y * (segments + 1) + x;
            uint32_t b = a + segments + 1;

            mesh.indices.insert(mesh.indices.end(), {
                a, b, a + 1,
                a + 1, b, b + 1
            });
        }
    }

    return mesh;
}

static Mesh makeCylinder(float radius, float height, int segments = 10) {
    Mesh mesh;

    for (int y = 0; y < 2; y++) {
        for (int i = 0; i < segments; i++) {
            float angle = 2.0f * PI * i / segments;

            mesh.points.push_back({
                radius * cosf(angle),
                y ? height / 2.0f : -height / 2.0f,
                radius * sinf(angle)
            });
        }
    }

    for (int i = 0; i < segments; i++) {
        uint32_t next = (i + 1) % segments;

        mesh.indices.insert(mesh.indices.end(), {
            (uint32_t)i,
            (uint32_t)(i + segments),
            next,
            next,
            (uint32_t)(i + segments),
            (uint32_t)(next + segments)
        });
    }

    return mesh;
}

struct Object {
    const Mesh* mesh;
    Vec3 position;
    Vec3 scale;
    float rotation;
    Color color;
};

struct Zombie {
    Vec3 position;
    float cooldown = 0.0f;
    bool alive = true;
};

static Mesh boxMesh = makeBox(1, 1, 1);
static Mesh sphereMesh = makeSphere(1);
static Mesh cylinderMesh = makeCylinder(1, 1);

class Game {
private:
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;

    Vec3 player{0, 0, 0};

    float yaw = 0.0f;
    float pitch = 0.0f;

    int hits = 0;
    float hurtTimer = 0.0f;

    bool dead = false;
    float deathTimer = 0.0f;
    bool titleScreen = true;

    uint32_t randomSeed = 0x4D494F5A;

    std::vector<Zombie> zombies;
    std::vector<Object> objects;

    float random01() {
        randomSeed = 1664525u * randomSeed + 1013904223u;

        return float(randomSeed & 0x00FFFFFF) /
               float(0x01000000);
    }

    void addObject(
        const Mesh* mesh,
        Vec3 position,
        Vec3 scale,
        Color color,
        float rotation = 0.0f
    ) {
        objects.push_back({
            mesh,
            position,
            scale,
            rotation,
            color
        });
    }

    void buildWorld() {
        objects.clear();
        zombies.clear();

        // Ground
        addObject(
            &boxMesh,
            {0, -0.30f, 0},
            {220, 0.5f, 220},
            {75, 142, 65, 255}
        );

        // Procedural trees
        for (int i = 0; i < 34; i++) {
            float angle = random01() * 2.0f * PI;
            float radius = 22.0f + random01() * 63.0f;

            float x = cosf(angle) * radius;
            float z = sinf(angle) * radius;

            addObject(
                &cylinderMesh,
                {x, 1.45f, z},
                {0.65f, 2.9f, 0.65f},
                {105, 68, 39, 255}
            );

            addObject(
                &sphereMesh,
                {x, 3.55f, z},
                {3.4f, 3.5f, 3.4f},
                {40, 112, 48, 255}
            );

            addObject(
                &sphereMesh,
                {x + 0.8f, 3.9f, z - 0.3f},
                {2.2f, 2.4f, 2.2f},
                {48, 130, 53, 255}
            );
        }

        // Seven zombies
        for (int i = 0; i < 7; i++) {
            float angle = random01() * 2.0f * PI;
            float radius = 12.0f + random01() * 11.0f;

            zombies.push_back({
                {
                    cosf(angle) * radius,
                    0,
                    sinf(angle) * radius
                },
                0.0f,
                true
            });
        }
    }

    void respawn() {
        player = {0, 0, 0};

        yaw = 0.0f;
        pitch = 0.0f;

        hits = 0;
        hurtTimer = 0.0f;

        dead = false;
        deathTimer = 0.0f;

        for (auto& zombie : zombies) {
            zombie.alive = true;
        }
    }

    void addMeshTriangles(
        const Object& object,
        std::vector<Triangle>& output
    ) {
        const Mesh& mesh = *object.mesh;

        for (
            size_t i = 0;
            i + 2 < mesh.indices.size();
            i += 3
        ) {
            SDL_Vertex vertices[3];

            float depth = 0.0f;
            bool visible = true;

            for (int k = 0; k < 3; k++) {
                Vec3 point = mesh.points[mesh.indices[i + k]];

                point.x *= object.scale.x;
                point.y *= object.scale.y;
                point.z *= object.scale.z;

                point = rotateY(point, object.rotation);
                point = add(point, object.position);

                Vec3 relative = sub(
                    point,
                    {player.x, 1.55f, player.z}
                );

                Vec3 camera = rotateY(relative, -yaw);

                float cy = cosf(pitch);
                float sy = sinf(pitch);

                float cameraY = camera.y * cy - camera.z * sy;
                float cameraZ = camera.y * sy + camera.z * cy;

                float cameraX = camera.x;

                if (cameraZ < 0.18f) {
                    visible = false;
                    break;
                }

                const float focalLength = 650.0f;

                float screenX =
                    SCREEN_W * 0.5f +
                    cameraX * focalLength / cameraZ;

                float screenY =
                    SCREEN_H * 0.5f -
                    cameraY * focalLength / cameraZ;

                vertices[k].position = {
                    screenX,
                    screenY
                };

                vertices[k].tex_coord = {0.0f, 0.0f};

                vertices[k].color = {
                    object.color.r,
                    object.color.g,
                    object.color.b,
                    object.color.a
                };

                depth += cameraZ;
            }

            if (visible) {
                output.push_back({
                    {vertices[0], vertices[1], vertices[2]},
                    depth / 3.0f
                });
            }
        }
    }

    void drawWorld() {
        std::vector<Triangle> triangles;
        triangles.reserve(7000);

        for (const auto& object : objects) {
            addMeshTriangles(object, triangles);
        }

        // Zombies
        for (const auto& zombie : zombies) {
            if (!zombie.alive) {
                continue;
            }

            Object body{
                &cylinderMesh,
                {zombie.position.x, 0.9f, zombie.position.z},
                {0.8f, 1.5f, 0.8f},
                0.0f,
                {73, 145, 69, 255}
            };

            Object head{
                &sphereMesh,
                {zombie.position.x, 1.95f, zombie.position.z},
                {0.72f, 0.72f, 0.72f},
                0.0f,
                {91, 166, 80, 255}
            };

            Object eyeLeft{
                &sphereMesh,
                {zombie.position.x - 0.20f, 2.0f, zombie.position.z - 0.31f},
                {0.12f, 0.12f, 0.08f},
                0.0f,
                {255, 30, 25, 255}
            };

            Object eyeRight{
                &sphereMesh,
                {zombie.position.x + 0.20f, 2.0f, zombie.position.z - 0.31f},
                {0.12f, 0.12f, 0.08f},
                0.0f,
                {255, 30, 25, 255}
            };

            addMeshTriangles(body, triangles);
            addMeshTriangles(head, triangles);
            addMeshTriangles(eyeLeft, triangles);
            addMeshTriangles(eyeRight, triangles);
        }

        // Draw distant triangles first
        std::sort(
            triangles.begin(),
            triangles.end(),
            [](const Triangle& a, const Triangle& b) {
                return a.depth > b.depth;
            }
        );

        SDL_SetRenderDrawColor(
            renderer,
            111, 177, 225, 255
        );

        SDL_RenderClear(renderer);

        std::vector<SDL_Vertex> vertices;
        std::vector<int> indices;

        vertices.reserve(triangles.size() * 3);
        indices.reserve(triangles.size() * 3);

        for (const auto& triangle : triangles) {
            int base = (int)vertices.size();

            for (const auto& vertex : triangle.vertices) {
                vertices.push_back(vertex);
            }

            indices.push_back(base);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
        }

        if (!vertices.empty()) {
            SDL_RenderGeometry(
                renderer,
                nullptr,
                vertices.data(),
                (int)vertices.size(),
                indices.data(),
                (int)indices.size()
            );
        }
    }

    void drawHUD() {
        SDL_SetRenderDrawBlendMode(
            renderer,
            SDL_BLENDMODE_BLEND
        );

        // Health indicators
        for (int i = 0; i < 3; i++) {
            SDL_Rect heart{
                28 + i * 42,
                26,
                30,
                24
            };

            if (i < hits) {
                SDL_SetRenderDrawColor(
                    renderer, 65, 65, 65, 255
                );
            } else {
                SDL_SetRenderDrawColor(
                    renderer, 220, 35, 45, 255
                );
            }

            SDL_RenderFillRect(renderer, &heart);
        }

        // Crosshair
        SDL_SetRenderDrawColor(
            renderer, 255, 255, 255, 230
        );

        SDL_RenderDrawLine(
            renderer,
            SCREEN_W / 2 - 10,
            SCREEN_H / 2,
            SCREEN_W / 2 + 10,
            SCREEN_H / 2
        );

        SDL_RenderDrawLine(
            renderer,
            SCREEN_W / 2,
            SCREEN_H / 2 - 10,
            SCREEN_W / 2,
            SCREEN_H / 2 + 10
        );

        // Damage flash
        if (hurtTimer > 0.0f) {
            Uint8 alpha = (Uint8)(hurtTimer * 115.0f);

            SDL_SetRenderDrawColor(
                renderer, 180, 0, 0, alpha
            );

            SDL_Rect screen{
                0, 0, SCREEN_W, SCREEN_H
            };

            SDL_RenderFillRect(renderer, &screen);
        }

        // Title / death panel
        if (titleScreen || dead) {
            SDL_SetRenderDrawColor(
                renderer, 0, 0, 0, 175
            );

            SDL_Rect panel{
                SCREEN_W / 2 - 260,
                SCREEN_H / 2 - 75,
                520,
                150
            };

            SDL_RenderFillRect(renderer, &panel);

            SDL_SetRenderDrawColor(
                renderer, 255, 255, 255, 255
            );

            SDL_Rect bar{
                SCREEN_W / 2 - 170,
                SCREEN_H / 2 - 12,
                340,
                24
            };

            SDL_RenderFillRect(renderer, &bar);

            SDL_SetRenderDrawColor(
                renderer, 40, 130, 50, 255
            );

            SDL_Rect inner{
                SCREEN_W / 2 - 165,
                SCREEN_H / 2 - 7,
                330,
                14
            };

            SDL_RenderFillRect(renderer, &inner);
        }
    }

    void update(float deltaTime) {
        if (hurtTimer > 0.0f) {
            hurtTimer = std::max(
                0.0f,
                hurtTimer - deltaTime
            );
        }

        const u64 keysDown =
            hidKeysDown(CONTROLLER_P1_AUTO);

        const u64 keysHeld =
            hidKeysHeld(CONTROLLER_P1_AUTO);

        if (titleScreen) {
            if (keysDown & (KEY_A | KEY_PLUS)) {
                titleScreen = false;
            }
            return;
        }

        if (dead) {
            deathTimer -= deltaTime;

            if (deathTimer <= 0.0f) {
                respawn();
            }

            return;
        }

        // Left stick + D-pad movement
        float moveX = 0.0f;
        float moveZ = 0.0f;

        if (keysHeld & KEY_DLEFT)  moveX -= 1.0f;
        if (keysHeld & KEY_DRIGHT) moveX += 1.0f;
        if (keysHeld & KEY_DUP)    moveZ -= 1.0f;
        if (keysHeld & KEY_DDOWN)  moveZ += 1.0f;

        HidAnalogStickState leftStick =
            hidJoystickRead(
                CONTROLLER_P1_AUTO,
                JOYSTICK_LEFT
            );

        moveX += leftStick.x / 32767.0f;
        moveZ -= leftStick.y / 32767.0f;

        float length = sqrtf(
            moveX * moveX + moveZ * moveZ
        );

        if (length > 1.0f) {
            moveX /= length;
            moveZ /= length;
        }

        Vec3 movement = rotateY(
            {
                moveX * 7.0f * deltaTime,
                0.0f,
                moveZ * 7.0f * deltaTime
            },
            yaw
        );

        // Invisible perimeter barriers
        player.x = std::clamp(
            player.x + movement.x,
            -MAP_LIMIT,
            MAP_LIMIT
        );

        player.z = std::clamp(
            player.z + movement.z,
            -MAP_LIMIT,
            MAP_LIMIT
        );

        // Right stick camera
        HidAnalogStickState rightStick =
            hidJoystickRead(
                CONTROLLER_P1_AUTO,
                JOYSTICK_RIGHT
            );

        yaw -= rightStick.x / 32767.0f *
               2.4f * deltaTime;

        pitch = std::clamp(
            pitch + rightStick.y / 32767.0f *
            2.0f * deltaTime,
            -1.48f,
            1.48f
        );

        // Zombie AI
        for (auto& zombie : zombies) {
            if (!zombie.alive) {
                continue;
            }

            Vec3 direction = sub(
                player,
                zombie.position
            );

            float distance = sqrtf(
                direction.x * direction.x +
                direction.z * direction.z
            );

            if (distance > 1.55f && distance > 0.001f) {
                zombie.position.x +=
                    direction.x / distance *
                    2.25f * deltaTime;

                zombie.position.z +=
                    direction.z / distance *
                    2.25f * deltaTime;
            } else {
                zombie.cooldown -= deltaTime;

                if (zombie.cooldown <= 0.0f) {
                    hits++;
                    hurtTimer = 0.65f;
                    zombie.cooldown = 1.25f;

                    if (hits >= 3) {
                        dead = true;
                        deathTimer = 2.0f;
                    }
                }
            }
        }

        if (keysDown & KEY_PLUS) {
            titleScreen = true;
        }
    }

public:
    bool init() {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            return false;
        }

        window = SDL_CreateWindow(
            "MIOZ ZOMBIE WORLD",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            SCREEN_W,
            SCREEN_H,
            SDL_WINDOW_FULLSCREEN
        );

        if (!window) {
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
            return false;
        }

        buildWorld();

        return true;
    }

    void run() {
        bool quit = false;

        Uint64 lastTime = SDL_GetPerformanceCounter();

        while (appletMainLoop() && !quit) {
            hidScanInput();

            SDL_Event event;

            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    quit = true;
                }
            }

            Uint64 currentTime =
                SDL_GetPerformanceCounter();

            float deltaTime =
                (float)(
                    (double)(currentTime - lastTime) /
                    SDL_GetPerformanceFrequency()
                );

            lastTime = currentTime;

            deltaTime = std::min(
                deltaTime,
                0.05f
            );

            update(deltaTime);
            drawWorld();
            drawHUD();

            SDL_RenderPresent(renderer);
        }
    }

    ~Game() {
        if (renderer) {
            SDL_DestroyRenderer(renderer);
        }

        if (window) {
            SDL_DestroyWindow(window);
        }

        SDL_Quit();
    }
};

int main(int argc, char* argv[]) {
    Game game;

    if (game.init()) {
        game.run();
    }

    return 0;
}
