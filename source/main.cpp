            const V3 zc = v3(zombie.position.x, zombie.position.y + 1.0f, zombie.position.z);
            const float dx = p.position.x - zc.x;
            const float dy = p.position.y - zc.y;
            const float dz = p.position.z - zc.z;
            if ((dx * dx + dy * dy + dz * dz) < 1.15f * 1.15f) {
                zombie.health -= p.damage;
                p.active = false;
                spawnImpact(p.position, Color{200, 45, 45, 255});
                if (zombie.health <= 0.0f) {
                    zombie.alive = false;
                    ++zombiesKilled;
                    score += 100;
                    spawnImpact(zc, Color{90, 160, 70, 255});
                }
                break;
            }
        }
        if (p.active && (p.position.y <= 0.0f || collidesWithWorld(p.position))) {
            spawnImpact(p.position);
            p.active = false;
        }
    }
}

void Game::updateParticles(float dt) {
    for (size_t i = 0; i < particles.size(); ) {
        Particle& pt = particles[i];
        pt.life -= dt;
        if (pt.life <= 0.0f) {
            particles[i] = particles.back();
            particles.pop_back();
            continue;
        }
        pt.velocity.y -= 9.8f * dt;
        pt.position = add(pt.position, mul(pt.velocity, dt));
        if (pt.position.y < 0.02f) {
            pt.position.y = 0.02f;
            pt.velocity.y = -pt.velocity.y * 0.35f;
            pt.velocity.x *= 0.7f;
            pt.velocity.z *= 0.7f;
        }
        ++i;
    }
}

// --- RENDU 3D & HUD ---
void Game::addMeshTriangles(const Object& object, std::vector<Tri>& output) {
    if (!object.mesh) return;

    const float cosYaw = std::cos(object.yaw);
    const float sinYaw = std::sin(object.yaw);
    const float cosCamYaw = std::cos(cameraYaw);
    const float sinCamYaw = std::sin(cameraYaw);
    const float cosCamPitch = std::cos(cameraPitch);
    const float sinCamPitch = std::sin(cameraPitch);

    const V3 camPos = v3(playerPosition.x, playerPosition.y + CAMERA_HEIGHT, playerPosition.z);

    auto transformWorld = [&](V3 local) -> V3 {
        V3 p = v3(
            (local.x - object.pivotOffset.x) * object.scale.x,
            (local.y - object.pivotOffset.y) * object.scale.y,
            (local.z - object.pivotOffset.z) * object.scale.z
        );
        float rx = p.x * cosYaw + p.z * sinYaw;
        float rz = -p.x * sinYaw + p.z * cosYaw;
        return v3(object.position.x + rx, object.position.y + p.y, object.position.z + rz);
    };

    auto worldToCam = [&](V3 w) -> V3 {
        float dx = w.x - camPos.x;
        float dy = w.y - camPos.y;
        float dz = w.z - camPos.z;

        // Rotation yaw autour de Y (caméra orientée vers +sin(yaw), +cos(yaw))
        float x1 = dx * cosCamYaw - dz * sinCamYaw;
        float z1 = dx * sinCamYaw + dz * cosCamYaw;

        // Rotation pitch autour de X
        float y2 = dy * cosCamPitch - z1 * sinCamPitch;
        float z2 = dy * sinCamPitch + z1 * cosCamPitch;
        return v3(x1, y2, z2);
    };

    const auto& verts = object.mesh->vertices;
    const auto& idx = object.mesh->indices;

    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        V3 w0 = transformWorld(verts[idx[i]]);
        V3 w1 = transformWorld(verts[idx[i + 1]]);
        V3 w2 = transformWorld(verts[idx[i + 2]]);

        // Backface culling simple en espace monde
        V3 e1 = sub(w1, w0);
        V3 e2 = sub(w2, w0);
        V3 normal = v3(e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x);
        V3 toCam = sub(camPos, w0);
        if (dot(normal, toCam) <= 0.0f) continue;

        V3 c0 = worldToCam(w0);
        V3 c1 = worldToCam(w1);
        V3 c2 = worldToCam(w2);

        if (c0.z <= NEAR_PLANE || c1.z <= NEAR_PLANE || c2.z <= NEAR_PLANE) continue;

        // Éclairage directionnel léger
        float lenN = length(normal);
        float light = 0.72f;
        if (lenN > 0.0001f) {
            V3 sunDir = v3(0.45f, 0.78f, 0.42f);
            float nDotL = std::max(0.0f, (normal.x * sunDir.x + normal.y * sunDir.y + normal.z * sunDir.z) / lenN);
            light = clampf(0.55f + nDotL * 0.45f, 0.45f, 1.0f);
        }

        SDL_Color sdlCol{
            (Uint8)clampf(object.color.r * light, 0.0f, 255.0f),
            (Uint8)clampf(object.color.g * light, 0.0f, 255.0f),
            (Uint8)clampf(object.color.b * light, 0.0f, 255.0f),
            object.color.a
        };

        Tri tri;
        tri.v[0] = SDL_Vertex{ SDL_FPoint{ SCREEN_W * 0.5f + (c0.x * CAMERA_FOCAL) / c0.z, SCREEN_H * 0.5f - (c0.y * CAMERA_FOCAL) / c0.z }, sdlCol, SDL_FPoint{0.0f, 0.0f} };
        tri.v[1] = SDL_Vertex{ SDL_FPoint{ SCREEN_W * 0.5f + (c1.x * CAMERA_FOCAL) / c1.z, SCREEN_H * 0.5f - (c1.y * CAMERA_FOCAL) / c1.z }, sdlCol, SDL_FPoint{0.0f, 0.0f} };
        tri.v[2] = SDL_Vertex{ SDL_FPoint{ SCREEN_W * 0.5f + (c2.x * CAMERA_FOCAL) / c2.z, SCREEN_H * 0.5f - (c2.y * CAMERA_FOCAL) / c2.z }, sdlCol, SDL_FPoint{0.0f, 0.0f} };
        tri.depth = (c0.z + c1.z + c2.z) / 3.0f;
        output.push_back(tri);
    }
}

void Game::drawZombie(const Zombie& zombie) {
    if (!zombie.alive) return;
    const float swing = std::sin(zombie.animationTime * 7.0f) * 0.45f;
    const float dx = playerPosition.x - zombie.position.x;
    const float dz = playerPosition.z - zombie.position.z;
    const float yaw = std::atan2(dx, dz);

    Object body{ &boxMesh, add(zombie.position, v3(0.0f, 1.0f, 0.0f)), v3(0.65f, 0.9f, 0.35f), Color{55, 110, 65, 255}, yaw };
    Object head{ &boxMesh, add(zombie.position, v3(0.0f, 1.72f, 0.0f)), v3(0.45f, 0.45f, 0.45f), Color{95, 155, 85, 255}, yaw };
    Object armL{ &boxMesh, add(zombie.position, v3(-0.45f, 1.25f, 0.25f)), v3(0.22f, 0.22f, 0.75f), Color{95, 155, 85, 255}, yaw + swing * 0.25f };
    Object armR{ &boxMesh, add(zombie.position, v3(0.45f, 1.25f, 0.25f)), v3(0.22f, 0.22f, 0.75f), Color{95, 155, 85, 255}, yaw - swing * 0.25f };
    Object legL{ &boxMesh, add(zombie.position, v3(-0.18f, 0.38f, 0.0f)), v3(0.25f, 0.76f, 0.25f), Color{45, 55, 85, 255}, yaw + swing };
    Object legR{ &boxMesh, add(zombie.position, v3(0.18f, 0.38f, 0.0f)), v3(0.25f, 0.76f, 0.25f), Color{45, 55, 85, 255}, yaw - swing };

    addMeshTriangles(body, triangles);
    addMeshTriangles(head, triangles);
    addMeshTriangles(armL, triangles);
    addMeshTriangles(armR, triangles);
    addMeshTriangles(legL, triangles);
    addMeshTriangles(legR, triangles);
}

void Game::drawAllZombies() {
    for (const Zombie& z : zombies) {
        drawZombie(z);
    }
}

void Game::drawWorld() {
    // Ciel dégradé simple
    SDL_SetRenderDrawColor(renderer, 115, 175, 230, 255);
    SDL_RenderClear(renderer);

    SDL_Rect skyBottom{0, SCREEN_H / 2, SCREEN_W, SCREEN_H / 2};
    SDL_SetRenderDrawColor(renderer, 155, 200, 235, 255);
    SDL_RenderFillRect(renderer, &skyBottom);

    triangles.clear();
    triangles.reserve(objects.size() * 12 + zombies.size() * 72 + projectiles.size() * 12 + ammoBoxes.size() * 12 + particles.size() * 12);

    for (const Object& obj : objects) {
        addMeshTriangles(obj, triangles);
    }

    for (const AmmoBox& box : ammoBoxes) {
        if (!box.active) continue;
        Object crate{ &boxMesh, box.position, v3(0.65f, 0.45f, 0.45f), Color{215, 165, 35, 255}, 0.0f };
        addMeshTriangles(crate, triangles);
    }

    for (const Projectile& p : projectiles) {
        if (!p.active) continue;
        Object bullet{ &sphereMesh, p.position, v3(0.08f, 0.08f, 0.08f), Color{255, 230, 90, 255}, 0.0f };
        addMeshTriangles(bullet, triangles);
    }

    for (const Particle& pt : particles) {
        Object spark{ &boxMesh, pt.position, v3(pt.size, pt.size, pt.size), pt.color, 0.0f };
        addMeshTriangles(spark, triangles);
    }

    drawAllZombies();

    // Tri du plus lointain au plus proche (painter's algorithm)
    std::sort(triangles.begin(), triangles.end(), [](const Tri& a, const Tri& b) {
        return a.depth > b.depth;
    });

    for (const Tri& t : triangles) {
        SDL_RenderGeometry(renderer, nullptr, t.v, 3, nullptr, 0);
    }

    drawWeapon();
    drawHud();

    if (paused) drawPauseScreen();
    if (playerDead) drawDeathScreen();

    SDL_RenderPresent(renderer);
}

void Game::drawWeapon() {
    const float recoilOffset = weaponRecoil * 26.0f;
    const int baseX = SCREEN_W - 360 + (int)(recoilOffset * 0.5f);
    const int baseY = SCREEN_H - 185 + (int)recoilOffset;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // Corps de l'arme
    SDL_Rect body{baseX, baseY, 175, 58};
    if (selectedWeapon == 0) {
        SDL_SetRenderDrawColor(renderer, 65, 70, 78, 255);
    } else {
        SDL_SetRenderDrawColor(renderer, 48, 52, 58, 255);
        body.w = 235;
        body.x -= 45;
    }
    SDL_RenderFillRect(renderer, &body);

    // Canon
    SDL_Rect barrel{body.x + body.w - 15, body.y + 10, (selectedWeapon == 0 ? 55 : 95), 20};
    SDL_SetRenderDrawColor(renderer, 35, 38, 42, 255);
    SDL_RenderFillRect(renderer, &barrel);

    // Flash de tir
    if (muzzleFlash > 0.0f) {
        SDL_Rect flash{barrel.x + barrel.w, barrel.y - 14, 48, 48};
        SDL_SetRenderDrawColor(renderer, 255, 210, 70, 220);
        SDL_RenderFillRect(renderer, &flash);
    }

    // Viseur central
    SDL_SetRenderDrawColor(renderer, 240, 240, 240, 210);
    SDL_Rect hLine{SCREEN_W / 2 - 12, SCREEN_H / 2 - 1, 24, 2};
    SDL_Rect vLine{SCREEN_W / 2 - 1, SCREEN_H / 2 - 12, 2, 24};
    SDL_RenderFillRect(renderer, &hLine);
    SDL_RenderFillRect(renderer, &vLine);
}

void Game::drawHud() {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // Flash rouge lorsque le joueur prend des dégâts
    if (hurtTimer > 0.0f) {
        Uint8 alpha = (Uint8)clampf((hurtTimer / 0.25f) * 110.0f, 0.0f, 110.0f);
        SDL_Rect screenOverlay{0, 0, SCREEN_W, SCREEN_H};
        SDL_SetRenderDrawColor(renderer, 200, 20, 20, alpha);
        SDL_RenderFillRect(renderer, &screenOverlay);
    }

    // Barre de vie (en bas à gauche)
    SDL_Rect hpBg{36, SCREEN_H - 64, 260, 24};
    SDL_SetRenderDrawColor(renderer, 25, 25, 30, 200);
    SDL_RenderFillRect(renderer, &hpBg);

    int hpWidth = (int)clampf((playerHealth / 100.0f) * 252.0f, 0.0f, 252.0f);
    SDL_Rect hpFill{40, SCREEN_H - 60, hpWidth, 16};
    SDL_SetRenderDrawColor(renderer, 210, 55, 55, 245);
    SDL_RenderFillRect(renderer, &hpFill);

    // Munitions (en bas à droite)
    int currentAmmo = (selectedWeapon == 0) ? pistolAmmo : rifleAmmo;
    int ammoBars = clampf(currentAmmo / 5, 0, 36);
    SDL_Rect ammoBg{SCREEN_W - 256, SCREEN_H - 64, 220, 24};
    SDL_SetRenderDrawColor(renderer, 25, 25, 30, 200);
    SDL_RenderFillRect(renderer, &ammoBg);

    for (int i = 0; i < ammoBars; ++i) {
        SDL_Rect tick{SCREEN_W - 250 + i * 6, SCREEN_H - 58, 3, 12};
        SDL_SetRenderDrawColor(renderer, (selectedWeapon == 0 ? 245 : 100), (selectedWeapon == 0 ? 195 : 210), 75, 245);
        SDL_RenderFillRect(renderer, &tick);
    }

    // Score / kills en haut à gauche
    SDL_Rect scoreBar{36, 32, clampf(zombiesKilled * 18.0f + 40.0f, 40.0f, 320.0f), 14};
    SDL_SetRenderDrawColor(renderer, 255, 225, 95, 210);
    SDL_RenderFillRect(renderer, &scoreBar);
    (void)score;
}

void Game::drawPauseScreen() {
    SDL_Rect overlay{0, 0, SCREEN_W, SCREEN_H};
    SDL_SetRenderDrawColor(renderer, 10, 12, 18, 165);
    SDL_RenderFillRect(renderer, &overlay);

    SDL_Rect panel{SCREEN_W / 2 - 180, SCREEN_H / 2 - 90, 360, 180};
    SDL_SetRenderDrawColor(renderer, 35, 40, 52, 235);
    SDL_RenderFillRect(renderer, &panel);

    SDL_Rect btnA{SCREEN_W / 2 - 90, SCREEN_H / 2 - 15, 180, 44};
    SDL_SetRenderDrawColor(renderer, 75, 165, 95, 245);
    SDL_RenderFillRect(renderer, &btnA);
}

void Game::drawDeathScreen() {
    SDL_Rect overlay{0, 0, SCREEN_W, SCREEN_H};
    SDL_SetRenderDrawColor(renderer, 120, 15, 15, 175);
    SDL_RenderFillRect(renderer, &overlay);

    SDL_Rect panel{SCREEN_W / 2 - 190, SCREEN_H / 2 - 80, 380, 160};
    SDL_SetRenderDrawColor(renderer, 35, 20, 22, 240);
    SDL_RenderFillRect(renderer, &panel);
}

// --- AUDIO & CYCLE DE VIE ---
void Game::initAudio() {
    SDL_AudioSpec desired{};
    desired.freq = 44100;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;
    desired.callback = nullptr;

    audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, nullptr, 0);

    // Charge les WAV depuis romfs si disponibles (fallback silencieux si absent)
    SDL_LoadWAV("romfs:/shot.wav", &desired, &wavBufferShot, &wavLengthShot);
    SDL_LoadWAV("romfs:/zombie.wav", &desired, &wavBufferZombie, &wavLengthZombie);
    SDL_LoadWAV("romfs:/reload.wav", &desired, &wavBufferReload, &wavLengthReload);
}

void Game::playSound(Uint8* wavBuffer, Uint32 wavLength) {
    if (audioDevice == 0 || !wavBuffer || wavLength == 0) return;
    SDL_QueueAudio(audioDevice, wavBuffer, wavLength);
}

bool Game::init() {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) return false;

    window = SDL_CreateWindow("MIOZ ARENA SWITCH", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN);
    if (!window) return false;

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) return false;

    initAudio();
    buildWorld();
    return true;
}

void Game::run() {
    Uint64 lastTicks = SDL_GetPerformanceCounter();
    const double freq = (double)SDL_GetPerformanceFrequency();

    while (appletMainLoop()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) return;
        }

        const u64 buttonsDown = padGetButtonsDown(&pad);
        if (buttonsDown & HidNpadButton_Plus) break;

        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)((double)(now - lastTicks) / freq);
        lastTicks = now;

        update(dt);
        drawWorld();
    }
}

Game::~Game() {
    if (audioDevice != 0) SDL_CloseAudioDevice(audioDevice);
    if (wavBufferShot) SDL_FreeWAV(wavBufferShot);
    if (wavBufferZombie) SDL_FreeWAV(wavBufferZombie);
    if (wavBufferReload) SDL_FreeWAV(wavBufferReload);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    romfsInit();

    Game game;
    if (game.init()) {
        game.run();
    }

    romfsExit();
    return 0;
}
