#include <raylib.h>

int main(void) {
    InitWindow(800, 450, "cco + raylib (C)");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(RAYWHITE);
        DrawText("Hello from cco + raylib (C)", 170, 200, 20, MAROON);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
