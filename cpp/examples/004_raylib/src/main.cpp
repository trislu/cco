#include <raylib.h>

int main() {
    InitWindow(800, 450, "cco + raylib");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(RAYWHITE);
        DrawText("Hello from cco + raylib", 190, 200, 20, MAROON);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
