// linked_list_visualiser.cpp
// Compile: g++ linked_list_visualiser.cpp -lraylib -std=c++17 -O2 -o llviz
// Requires raylib.

#include "raylib.h"
#include <vector>
#include <string>
#include <sstream>
#include <functional>
#include <memory>
#include <cmath>
#include <algorithm>
#include <queue>

// ----------------------------- Utility -------------------------------------

static float Lerp(float a, float b, float t) { return a + (b - a) * t; }
static Vector2 Lerp(const Vector2 &a, const Vector2 &b, float t) {
    return { Lerp(a.x, b.x, t), Lerp(a.y, b.y, t) };
}

static std::string ToString(int v) {
    std::ostringstream ss; ss << v; return ss.str();
}

// ----------------------------- Visual constants ----------------------------

const int SCREEN_W = 1100;
const int SCREEN_H = 850;
const Vector2 LIST_AREA_ORIGIN = { 60, 180 };
const float NODE_WIDTH = 84;
const float NODE_HEIGHT = 48;
const float NODE_SPACING = 24;
const float POINTER_LINE_Y = LIST_AREA_ORIGIN.y + NODE_HEIGHT + 10;
const Color NODE_COL = LIGHTGRAY;
const Color NODE_BORDER = DARKGRAY;
const Color HIGHLIGHT_COL = GOLD;
const Color POINTER_COL = DARKBLUE;

// ----------------------------- Animation / Operation system ---------------

enum class OpType { Compare, Swap, InsertVisual, DeleteVisual, Move, HighlightNone, Pause };

struct Op {
    OpType type;
    int idxA = -1;
    int idxB = -1;
    int value = 0;
    float duration = 0.6f; // seconds
    std::string meta;
};

class Animator {
public:
    // queued operations
    std::queue<Op> ops;
    Op current;
    bool running = false;
    float timer = 0.0f;
    float speed = 1.0f; // multiplier (1.0 normal)

    // callbacks for when an operation starts / finishes
    std::function<void(const Op&)> onStart;
    std::function<void(const Op&)> onFinish;

    void Enqueue(const Op &op) { ops.push(op); if(!running) StartNext(); }
    void Clear() { while(!ops.empty()) ops.pop(); running = false; }
    bool Busy() const { return running || !ops.empty(); }

    void StartNext() {
        if (ops.empty()) { running = false; return; }
        current = ops.front(); ops.pop();
        timer = 0.0f;
        running = true;
        if (onStart) onStart(current);
    }

    void Update(float dt) {
        if (!running) return;
        timer += dt * speed;
        if (timer >= current.duration) {
            // finish
            if (onFinish) onFinish(current);
            running = false;
            StartNext();
        }
    }

    float Progress() const {
        if (!running) return 1.0f;
        return std::min(1.0f, timer / current.duration);
    }
};

// ----------------------------- Node & LinkedList ---------------------------

struct VNode {
    int value;
    // position interpolation for animations
    Vector2 pos;       // current
    Vector2 targetPos; // where it should go
    Color color;
    bool highlight = false;

    VNode(int v = 0, Vector2 p = {0,0}) : value(v), pos(p), targetPos(p), color(NODE_COL) {}
};

class LinkedListVisual {
public:
    std::vector<std::unique_ptr<VNode>> nodes; // logical singly-linked list stored in order
    Animator *anim;

    LinkedListVisual(Animator *a) : anim(a) {
        // tie Animator callbacks
        anim->onStart = [this](const Op &op) { this->HandleOpStart(op); };
        anim->onFinish = [this](const Op &op) { this->HandleOpFinish(op); };
    }

    void Reset() {
        nodes.clear();
        RecomputeTargets();
    }

    int Count() const { return (int)nodes.size(); }

    void PushTail(int value) {
        // immediate model update, but visual movement via Op queue
        auto node = std::make_unique<VNode>(value, SpawnPosForNew());
        nodes.push_back(std::move(node));
        RecomputeTargets();
        // Enqueue an insert visual op
        Op o; o.type = OpType::InsertVisual; o.idxA = (int)nodes.size() - 1; o.duration = 0.6f;
        anim->Enqueue(o);
    }

    void PushHead(int value) {
        auto node = std::make_unique<VNode>(value, SpawnPosForNew());
        nodes.insert(nodes.begin(), std::move(node));
        RecomputeTargets();
        Op o; o.type = OpType::InsertVisual; o.idxA = 0; o.duration = 0.6f;
        anim->Enqueue(o);
    }

    // insert after the first node that equals 'afterValue'
    bool InsertAfterValue(int afterValue, int newValue) {
        for (int i = 0; i < Count(); ++i) {
            if (nodes[i]->value == afterValue) {
                auto node = std::make_unique<VNode>(newValue, SpawnPosForNew());
                nodes.insert(nodes.begin() + i + 1, std::move(node));
                RecomputeTargets();
                Op o; o.type = OpType::InsertVisual; o.idxA = i + 1; o.duration = 0.6f;
                anim->Enqueue(o);
                return true;
            }
        }
        return false;
    }

    // delete by value: deletes first occurrence
    bool DeleteByValue(int value) {
        for (int i = 0; i < Count(); ++i) {
            if (nodes[i]->value == value) {
                // mark node to be removed visually
                Op o; o.type = OpType::DeleteVisual; o.idxA = i; o.duration = 0.6f;
                anim->Enqueue(o);
                // actual removal will be done on op finish
                return true;
            }
        }
        return false;
    }

    // delete by position (0-based)
    bool DeleteByPosition(int pos) {
        if (pos < 0 || pos >= Count()) return false;
        Op o; o.type = OpType::DeleteVisual; o.idxA = pos; o.duration = 0.6f;
        anim->Enqueue(o);
        return true;
    }

    // Bubble sort visualization: we enqueue compare and swap operations
    void BubbleSort() {
        // Create a copy of the values to simulate the sorting and determine swaps needed
        int n = Count();
        if (n < 2) return;
        std::vector<int> values;
        for (int i = 0; i < n; ++i) values.push_back(nodes[i]->value);
        
        // Simulate bubble sort on the copy to generate correct swap sequence
        for (int pass = 0; pass < n - 1; ++pass) {
            bool swapped = false;
            for (int i = 0; i < n - pass - 1; ++i) {
                Op cmp; cmp.type = OpType::Compare; cmp.idxA = i; cmp.idxB = i + 1; cmp.duration = 0.5f;
                anim->Enqueue(cmp);
                if (values[i] > values[i+1]) {
                    // Swap in our simulation
                    std::swap(values[i], values[i+1]);
                    // enqueue swap op
                    Op sw; sw.type = OpType::Swap; sw.idxA = i; sw.idxB = i + 1; sw.duration = 0.6f;
                    anim->Enqueue(sw);
                    swapped = true;
                } else {
                    // small pause to show comparison
                    Op p; p.type = OpType::Pause; p.duration = 0.25f;
                    anim->Enqueue(p);
                }
            }
            if (!swapped) break;
        }
        // final unhighlight op
        Op end; end.type = OpType::HighlightNone; end.duration = 0.1f;
        anim->Enqueue(end);
    }

    // Called by Animator on op start to set visual cues (like highlighting)
    void HandleOpStart(const Op &op) {
        switch (op.type) {
            case OpType::Compare:
                ClearHighlights();
                if (ValidIndex(op.idxA)) nodes[op.idxA]->highlight = true;
                if (ValidIndex(op.idxB)) nodes[op.idxB]->highlight = true;
                break;
            case OpType::Swap:
                // highlights remain; we'll animate movement using target positions swap
                // swap target positions immediately so nodes animate to new places
                SwapTargets(op.idxA, op.idxB);
                break;
            case OpType::InsertVisual:
                // highlight inserted node
                ClearHighlights();
                if (ValidIndex(op.idxA)) nodes[op.idxA]->highlight = true;
                break;
            case OpType::DeleteVisual:
                if (ValidIndex(op.idxA)) nodes[op.idxA]->color = RED; // mark for deletion
                break;
            case OpType::Pause:
            case OpType::Move:
            case OpType::HighlightNone:
                break;
        }
    }

    // Called by Animator when op finishes to finalize logical changes
    void HandleOpFinish(const Op &op) {
        switch (op.type) {
            case OpType::Compare:
                // do nothing logical
                break;
            case OpType::Swap:
                // physically swap node objects in vector
                if (ValidIndex(op.idxA) && ValidIndex(op.idxB)) {
                    std::swap(nodes[op.idxA], nodes[op.idxB]);
                    // After swapping objects, recompute targets so nodes know their new positions
                    RecomputeTargets();
                }
                break;
            case OpType::InsertVisual:
                // ensure highlight removed eventually
                if (ValidIndex(op.idxA)) nodes[op.idxA]->highlight = false;
                break;
            case OpType::DeleteVisual:
                if (ValidIndex(op.idxA)) {
                    // remove the node from model
                    nodes.erase(nodes.begin() + op.idxA);
                    // recompute targets so remaining nodes shift into place
                    RecomputeTargets();
                }
                break;
            case OpType::HighlightNone:
                ClearHighlights();
                break;
            case OpType::Pause:
            case OpType::Move:
                break;
        }
    }

    void Update(float dt) {
        // animate each node towards its target position (simple lerp)
        float p = anim->Progress(); // allows step-synced animations too
        // but use dt for smooth movement too: we mix both approaches
        for (size_t i = 0; i < nodes.size(); ++i) {
            // Smooth damp: move fractionally towards target
            nodes[i]->pos = Lerp(nodes[i]->pos, nodes[i]->targetPos, std::min(1.0f, dt * 8.0f + p*0.2f));
            // clamp tiny differences
            if (fabs(nodes[i]->pos.x - nodes[i]->targetPos.x) < 0.3f) nodes[i]->pos.x = nodes[i]->targetPos.x;
            if (fabs(nodes[i]->pos.y - nodes[i]->targetPos.y) < 0.3f) nodes[i]->pos.y = nodes[i]->targetPos.y;
        }
    }

    void Draw() {
        // Draw nodes and pointer arrows
        for (size_t i = 0; i < nodes.size(); ++i) {
            DrawVNode(*nodes[i]);
            // pointer line to next
            if (i + 1 < nodes.size()) {
                Vector2 a = { nodes[i]->pos.x + NODE_WIDTH * 0.9f, nodes[i]->pos.y + NODE_HEIGHT / 2.0f };
                Vector2 b = { nodes[i+1]->pos.x - NODE_WIDTH * 0.1f, nodes[i+1]->pos.y + NODE_HEIGHT / 2.0f };
                DrawLineEx(a, b, 3.0f, POINTER_COL);
                // draw arrow head
                DrawTriangle({b.x - 8, b.y - 6}, {b.x - 8, b.y + 6}, {b.x, b.y}, POINTER_COL);
            }
        }
        // draw head/tail labels
        if (!nodes.empty()) {
            DrawText("HEAD", nodes.front()->pos.x, nodes.front()->pos.y - 22, 12, DARKGREEN);
            DrawText("TAIL", nodes.back()->pos.x, nodes.back()->pos.y - 22, 12, DARKGREEN);
        }
    }

    // For debugging / small helper to dump values
    std::string ValuesString() const {
        std::ostringstream ss;
        ss << "[ ";
        for (auto &n : nodes) ss << n->value << " ";
        ss << "]";
        return ss.str();
    }

private:
    bool ValidIndex(int i) const { return i >= 0 && i < (int)nodes.size(); }

    Vector2 SpawnPosForNew() {
        // spawn above the list area so inserted nodes slide down
        return { LIST_AREA_ORIGIN.x + 20, LIST_AREA_ORIGIN.y - 80 };
    }

    void DrawVNode(const VNode &n) {
        Rectangle r = { n.pos.x, n.pos.y, NODE_WIDTH, NODE_HEIGHT };
        Color fill = n.color;
        if (n.highlight) fill = HIGHLIGHT_COL;
        DrawRectangleRounded(r, 0.12f, 6, fill);
        DrawRectangleRoundedLinesEx(r, 0.12f, 6, 2.0f, NODE_BORDER);
        // draw value
        std::string s = ToString(n.value);
        int tx = (int)(n.pos.x + NODE_WIDTH / 2 - MeasureText(s.c_str(), 20) / 2);
        int ty = (int)(n.pos.y + NODE_HEIGHT / 2 - 10);
        DrawText(s.c_str(), tx, ty, 20, BLACK);
    }

    void ClearHighlights() {
        for (auto &n : nodes) n->highlight = false;
    }

    void RecomputeTargets() {
        // recompute target positions for each node based on order
        float x = LIST_AREA_ORIGIN.x;
        float y = LIST_AREA_ORIGIN.y;
        for (size_t i = 0; i < nodes.size(); ++i) {
            nodes[i]->targetPos = { x + i * (NODE_WIDTH + NODE_SPACING), y };
            // If node had default pos (0,0) set pos to target to avoid jump
            if (nodes[i]->pos.x == 0 && nodes[i]->pos.y == 0) nodes[i]->pos = nodes[i]->targetPos;
        }
    }

    void SwapTargets(int i, int j) {
        if (!ValidIndex(i) || !ValidIndex(j)) return;
        std::swap(nodes[i]->targetPos, nodes[j]->targetPos);
    }
};

// ----------------------------- UI Widgets ---------------------------------

struct Button {
    Rectangle rect;
    std::string label;
    std::function<void()> onClick;

    Button() {}
    Button(float x, float y, float w, float h, const std::string &l, std::function<void()> f)
        : rect{ x, y, w, h }, label(l), onClick(f) {}

    void Draw() const {
        DrawRectangleRec(rect, LIGHTGRAY);
        DrawRectangleLinesEx(rect, 2.0f, DARKGRAY);
        int tw = MeasureText(label.c_str(), 18);
        DrawText(label.c_str(), (int)(rect.x + rect.width / 2 - tw/2), (int)(rect.y + rect.height / 2 - 9), 18, BLACK);
    }

    bool HandleMouse(Vector2 mouse) {
        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(mouse, rect)) {
            if (onClick) onClick();
            return true;
        }
        return false;
    }
};

struct TextBox {
    Rectangle rect;
    std::string text;
    bool active = false;
    int maxLen = 8;

    TextBox() {}
    TextBox(float x, float y, float w, float h) { rect = { x,y,w,h }; }

    void Draw() const {
        DrawRectangleRec(rect, WHITE);
        DrawRectangleLinesEx(rect, 2.0f, DARKGRAY);
        DrawText(text.c_str(), (int)rect.x + 6, (int)rect.y + 6, 18, BLACK);
        if (active) {
            // drawing caret
            int tx = rect.x + 6 + MeasureText(text.c_str(), 18);
            DrawLine(tx, rect.y + 6, tx, rect.y + rect.height - 6, BLACK);
        }
    }

    void HandleEvents() {
        Vector2 m = GetMousePosition();
        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(m, rect)) {
            active = true;
        }
        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON) && !CheckCollisionPointRec(m, rect)) {
            active = false;
        }
        if (active) {
            int key = GetKeyPressed();
            while (key > 0) {
                char c = (char)key;
                if ((c >= '0' && c <= '9') || c == '-' ) {
                    if ((int)text.size() < maxLen) text.push_back(c);
                } else if (c == 8) { // backspace
                    if (!text.empty()) text.pop_back();
                }
                key = GetKeyPressed();
            }
            // handle delete/backspace using keys
            if (IsKeyPressed(KEY_BACKSPACE) && !text.empty()) text.pop_back();
            if (IsKeyPressed(KEY_ENTER)) active = false;
        }
    }

    // convert to int, return defaultVal if invalid
    int GetInt(int defaultVal = 0) const {
        if (text.empty()) return defaultVal;
        try {
            return std::stoi(text);
        } catch (...) {
            return defaultVal;
        }
    }
};

// ----------------------------- Application --------------------------------

int main() {
    InitWindow(SCREEN_W, SCREEN_H, "Linked List Visualiser - raylib");
    SetTargetFPS(60);

    Animator animator;
    LinkedListVisual list(&animator);

    // Camera for auto-zoom
    Camera2D camera = { 0 };
    camera.target = { LIST_AREA_ORIGIN.x, LIST_AREA_ORIGIN.y };
    camera.offset = { LIST_AREA_ORIGIN.x, LIST_AREA_ORIGIN.y };
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;
    float targetZoom = 1.0f;

    // default data
    list.PushTail(10); list.PushTail(30); list.PushTail(20);

    // UI elements
    std::vector<Button> buttons;
    TextBox inputNum(60, 80, 120, 36);
    TextBox inputAfter(200, 80, 120, 36);

    bool modeInsertAfter = false;
    bool deleteByPositionMode = false;
    bool autoPlay = true;
    
    // Algorithm display
    std::string currentAlgorithm = "";
    std::vector<std::string> algorithmSteps;

    // Buttons setup
    float bx = 340;
    float by = 50;
    float bw = 110, bh = 36, gap = 12;

    auto enqueueNothing = [&](){ /* placeholder */ };

    buttons.push_back(Button(bx, by, bw, bh, "Insert Head", [&]() {
        int v = inputNum.GetInt();
        if (!animator.Busy()) {
            list.PushHead(v);
            currentAlgorithm = "Insert Head";
            algorithmSteps = {
                "1. newNode.next = head",
                "2. head = newNode"
            };
        }
    }));

    buttons.push_back(Button(bx + (bw + gap)*1, by, bw, bh, "Insert Tail", [&]() {
        int v = inputNum.GetInt();
        if (!animator.Busy()) {
            list.PushTail(v);
            currentAlgorithm = "Insert Tail";
            algorithmSteps = {
                "1. current = head",
                "2. while current.next != null:",
                "3.     current = current.next",
                "4. current.next = newNode"
            };
        }
    }));

    buttons.push_back(Button(bx + (bw + gap)*2, by, bw, bh, "Insert After", [&]() {
        int v = inputNum.GetInt();
        int after = inputAfter.GetInt();
        if (!animator.Busy()) {
            if (list.InsertAfterValue(after, v)) {
                currentAlgorithm = "Insert After";
                algorithmSteps = {
                    "1. current = head",
                    "2. while current != null:",
                    "3.     if current.value == target:",
                    "4.         newNode.next = current.next",
                    "5.         current.next = newNode",
                    "6.         break",
                    "7.     current = current.next"
                };
            } else {
                // small flash or debug; here we enqueue a brief pause to show failure
                Op p; p.type = OpType::Pause; p.duration = 0.3f; animator.Enqueue(p);
            }
        }
    }));

    // Delete
    buttons.push_back(Button(bx + (bw + gap)*3, by, bw, bh, "Delete Value", [&]() {
        int v = inputNum.GetInt();
        if (!animator.Busy()) {
            if (list.DeleteByValue(v)) {
                currentAlgorithm = "Delete by Value";
                algorithmSteps = {
                    "1. if head.value == target:",
                    "2.     head = head.next",
                    "3.     return",
                    "4. current = head",
                    "5. while current.next != null:",
                    "6.     if current.next.value == target:",
                    "7.         current.next = current.next.next",
                    "8.         return",
                    "9.     current = current.next"
                };
            }
        }
    }));

    buttons.push_back(Button(bx + (bw + gap)*4, by, bw, bh, "Delete Pos", [&]() {
        int p = inputNum.GetInt();
        if (!animator.Busy()) {
            if (list.DeleteByPosition(p)) {
                currentAlgorithm = "Delete by Position";
                algorithmSteps = {
                    "1. if position == 0:",
                    "2.     head = head.next",
                    "3.     return",
                    "4. current = head",
                    "5. for i = 0 to position-2:",
                    "6.     current = current.next",
                    "7. current.next = current.next.next"
                };
            }
        }
    }));

    // Sort
    buttons.push_back(Button(bx + (bw + gap)*5, by, bw, bh, "Bubble Sort", [&]() {
        if (!animator.Busy()) {
            list.BubbleSort();
            currentAlgorithm = "Bubble Sort";
            algorithmSteps = {
                "1. for pass = 0 to n-2:",
                "2.     swapped = false",
                "3.     for i = 0 to n-pass-2:",
                "4.         if list[i] > list[i+1]:",
                "5.             swap(list[i], list[i+1])",
                "6.             swapped = true",
                "7.     if not swapped: break"
            };
        }
    }));

    // Step / Play control
    buttons.push_back(Button(bx, by + bh + gap, bw, bh, "Step", [&]() {
        // If animator is idle and has queued ops this will advance,
        // but we don't maintain a manual op queue external to Animator,
        // so 'Step' will speed-pause: we temporarily set speed and advance the animator by a tick.
        // For simplicity, if animator not running and queue empty do nothing.
        // Implementation note: this could be extended to a real "next op" feature.
        if (!animator.running && !animator.ops.empty()) {
            animator.StartNext();
        } else if (animator.running) {
            // force finish current op
            // Not ideal, but to support step toggling we can set timer to duration.
            animator.timer = animator.current.duration;
        }
    }));

    buttons.push_back(Button(bx + (bw + gap)*1, by + bh + gap, bw, bh, "Play/Pause", [&]() {
        autoPlay = !autoPlay;
    }));

    buttons.push_back(Button(bx + (bw + gap)*2, by + bh + gap, bw, bh, "Reset", [&]() {
        if (!animator.Busy()) {
            list.Reset();
        }
    }));

    // Speed slider (simple clickable area)
    Rectangle speedBar = { bx + (bw + gap)*4, by + bh + gap + 6, 220, 24 };
    float speed = 1.0f;

    // Helper text explanation
    std::string help = "Use number input (left) and 'after' input (right) for Insert After. Click Play to animate.";

    // Main loop
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        // Input handling
        inputNum.HandleEvents();
        inputAfter.HandleEvents();

        Vector2 mouse = GetMousePosition();
        for (auto &b : buttons) b.HandleMouse(mouse);

        // speed bar interaction
        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(mouse, speedBar)) {
            float rel = (mouse.x - speedBar.x) / speedBar.width;
            rel = std::clamp(rel, 0.01f, 1.0f);
            speed = rel * 10.0f; // map to [0, 10.0]
            animator.speed = speed;
        }

        // Update animator (if autoPlay enabled) or let manual Step control
        if (autoPlay) animator.Update(dt);
        else {
            // If paused, still update a tiny bit to keep node interpolation smooth
            animator.Update(dt * 0.0f);
        }
        list.Update(dt);

        // Calculate camera zoom to fit list in listBg
        Rectangle listBg = { LIST_AREA_ORIGIN.x - 10, LIST_AREA_ORIGIN.y - 40, SCREEN_W - 2*LIST_AREA_ORIGIN.x + 20, 220 };
        if (list.Count() > 0) {
            float listWidth = list.Count() * (NODE_WIDTH + NODE_SPACING) - NODE_SPACING;
            float availableWidth = listBg.width - 40; // padding
            if (listWidth > availableWidth) {
                targetZoom = availableWidth / listWidth;
            } else {
                targetZoom = 1.0f;
            }
        } else {
            targetZoom = 1.0f;
        }
        
        // Smooth zoom transition
        camera.zoom = Lerp(camera.zoom, targetZoom, dt * 5.0f);

        // Drawing
        BeginDrawing();
        ClearBackground(RAYWHITE);

        // Title and inputs
        DrawText("Linked List Visualiser (raylib)", 60, 16, 22, DARKBLUE);
        DrawText(help.c_str(), 60, 36, 12, GRAY);

        // Draw input labels
        DrawText("Value:", (int)inputNum.rect.x, (int)inputNum.rect.y - 20, 14, DARKGRAY);
        DrawText("After:", (int)inputAfter.rect.x, (int)inputAfter.rect.y - 20, 14, DARKGRAY);

        inputNum.Draw();
        inputAfter.Draw();

        // Draw buttons
        for (auto &b : buttons) b.Draw();

        // Speed bar UI
        DrawText("Speed", (int)speedBar.x - 60, (int)speedBar.y + 2, 18, DARKGRAY);
        DrawRectangleLinesEx(speedBar, 2.0f, DARKGRAY);
        // slider fill
        float fillW = std::clamp((speed / 10.0f) * speedBar.width, 4.0f, speedBar.width);
        DrawRectangle(speedBar.x, speedBar.y, fillW, speedBar.height, LIGHTGRAY);
        DrawText( TextFormat("%.1fx", animator.speed), speedBar.x + speedBar.width + 8, speedBar.y, 16, DARKGRAY);

        // list area background
        DrawRectangleRec(listBg, Fade(LIGHTGRAY, 0.05f));
        DrawRectangleLinesEx(listBg, 2.0f, Fade(DARKGRAY, 0.06f));

        // Begin camera mode for list drawing
        BeginMode2D(camera);
        list.Draw();
        EndMode2D();

        // Algorithm display area
        Rectangle algoBg = { LIST_AREA_ORIGIN.x - 10, LIST_AREA_ORIGIN.y + 200, SCREEN_W - 2*LIST_AREA_ORIGIN.x + 20, 300 };
        DrawRectangleRec(algoBg, Fade(SKYBLUE, 0.05f));
        DrawRectangleLinesEx(algoBg, 2.0f, Fade(DARKBLUE, 0.3f));
        
        if (!currentAlgorithm.empty()) {
            DrawText(("Algorithm: " + currentAlgorithm).c_str(), algoBg.x + 10, algoBg.y + 10, 18, DARKBLUE);
            
            int stepY = algoBg.y + 40;
            for (size_t i = 0; i < algorithmSteps.size(); ++i) {
                DrawText(algorithmSteps[i].c_str(), algoBg.x + 20, stepY, 16, DARKGRAY);
                stepY += 30;
            }
        } else {
            DrawText("Select an operation to see the algorithm", algoBg.x + 10, algoBg.y + algoBg.height / 2 - 10, 16, GRAY);
        }

        // Footer: current values
        DrawText(("List: " + list.ValuesString()).c_str(), 60, SCREEN_H - 36, 16, DARKGRAY);

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
