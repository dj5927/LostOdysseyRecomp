#include <stdafx.h>
#include <hid/hid.h>
#include <debug/menu_overlay.h>
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <condition_variable>
#include <future>

std::atomic<uint32_t> g_presentedSwaps{0};
namespace
{
std::atomic<uint32_t> g_settingsFilterCalls{0};
std::atomic<bool> g_overlayVisible{false};
std::mutex g_overlayStubMutex;
std::condition_variable g_overlayStubCv;
bool g_blockOverlay = false;
bool g_overlayEntered = false;
}
namespace settings
{
bool FilterInput(uint16_t&, int16_t, int16_t)
{
    ++g_settingsFilterCalls;
    return false;
}
}
namespace frame_timing { uint64_t InputTick() { return 0; } }
namespace debug_menu
{
void ToggleOverlay()
{
    std::unique_lock lock(g_overlayStubMutex);
    if (!g_blockOverlay) return;
    g_overlayEntered = true;
    g_overlayStubCv.notify_all();
    g_overlayStubCv.wait(lock, [] { return !g_blockOverlay; });
}
bool IsOverlayVisible() { return g_overlayVisible.load(); }
void HandleInput(InputAction) {}
}

static void Check(bool value, const char* message)
{
    if (!value) { fprintf(stderr, "FAIL: %s (%s)\n", message, SDL_GetError()); std::exit(1); }
}

int main()
{
    hid::Init();
    const int first = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
    const int second = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
    Check(first >= 0 && second >= 0, "attach two controllers");
    auto* a = SDL_JoystickOpen(first);
    auto* b = SDL_JoystickOpen(second);
    Check(a && b, "open virtual joysticks");
    auto sample = [] {
        SDL_JoystickUpdate();
        XAMINPUT_STATE state;
        memset(&state, 0xff, sizeof(state)); // GetState must clear old caller state.
        Check(hid::GetState(0, &state) == 0, "get state");
        return state.Gamepad;
    };
    sample(); // consume added events
    SDL_JoystickSetVirtualButton(a, SDL_CONTROLLER_BUTTON_A, 1);
    Check(sample().wButtons & XAMINPUT_GAMEPAD_A, "first controller A");
    SDL_JoystickSetVirtualButton(a, SDL_CONTROLLER_BUTTON_A, 0);
    SDL_JoystickSetVirtualButton(b, SDL_CONTROLLER_BUTTON_B, 1);
    auto state = sample();
    Check((state.wButtons & XAMINPUT_GAMEPAD_B) && !(state.wButtons & XAMINPUT_GAMEPAD_A), "second controller takes over");
    hid::HandleKeyboardEvent(SDL_SCANCODE_Z, true);
    state = sample();
    Check((state.wButtons & (XAMINPUT_GAMEPAD_A | XAMINPUT_GAMEPAD_B)) == (XAMINPUT_GAMEPAD_A | XAMINPUT_GAMEPAD_B), "keyboard with connected controllers");
    hid::HandleKeyboardEvent(SDL_SCANCODE_Z, false);
    Check(!(sample().wButtons & XAMINPUT_GAMEPAD_A), "keyboard release");
    SDL_JoystickSetVirtualButton(b, SDL_CONTROLLER_BUTTON_B, 0);
    SDL_JoystickSetVirtualAxis(a, SDL_CONTROLLER_AXIS_LEFTX, 1000);
    SDL_JoystickSetVirtualAxis(b, SDL_CONTROLLER_AXIS_LEFTX, 20000);
    Check(sample().sThumbLX == 20000, "idle pad does not overwrite moving pad");
    hid::HandleKeyboardEvent(SDL_SCANCODE_J, true);
    Check(sample().sThumbLX == -32768, "keyboard stick overrides pad while held");
    hid::HandleKeyboardEvent(SDL_SCANCODE_J, false);
    Check(sample().sThumbLX == 20000, "pad resumes after keyboard release");
    SDL_JoystickSetVirtualAxis(b, SDL_CONTROLLER_AXIS_LEFTX, 0);
    Check(sample().sThumbLX == 0, "idle drift deadzone");
    SDL_JoystickSetVirtualAxis(b, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 32767);
    Check(sample().bRightTrigger == 255, "second pad trigger");
    SDL_JoystickSetVirtualAxis(b, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
    hid::HandleKeyboardEvent(SDL_SCANCODE_R, true);
    Check(sample().bRightTrigger == 255, "keyboard trigger");
    hid::ClearKeyboardState();
    Check(sample().bRightTrigger == 0, "focus loss releases keyboard");

    g_overlayVisible = true;
    g_settingsFilterCalls = 0;
    SDL_JoystickSetVirtualButton(a, SDL_CONTROLLER_BUTTON_A, 1);
    Check(sample().wButtons == 0, "debug overlay did not consume game input");
    Check(g_settingsFilterCalls.load() == 0, "settings menu consumed debug overlay input");
    SDL_JoystickSetVirtualButton(a, SDL_CONTROLLER_BUTTON_A, 0);
    sample();
    Check(g_settingsFilterCalls.load() == 0, "settings menu ran while debug overlay was visible");
    g_overlayVisible = false;
    sample();
    Check(g_settingsFilterCalls.load() != 0, "settings input did not resume after debug overlay closed");

    sample();
    {
        std::lock_guard lock(g_overlayStubMutex);
        g_blockOverlay = true;
        g_overlayEntered = false;
    }
    SDL_JoystickSetVirtualButton(a, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 1);
    SDL_JoystickSetVirtualButton(a, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 1);
    auto menuAction = std::async(std::launch::async, sample);
    {
        std::unique_lock lock(g_overlayStubMutex);
        Check(g_overlayStubCv.wait_for(lock, std::chrono::seconds(2), [] { return g_overlayEntered; }),
              "debug menu action did not start");
    }
    auto keyboardEvent = std::async(std::launch::async, [] { hid::HandleKeyboardEvent(SDL_SCANCODE_Z, true); });
    Check(keyboardEvent.wait_for(std::chrono::seconds(1)) == std::future_status::ready,
          "debug menu action retained HID device lock");
    {
        std::lock_guard lock(g_overlayStubMutex);
        g_blockOverlay = false;
    }
    g_overlayStubCv.notify_all();
    menuAction.get();
    keyboardEvent.get();
    hid::ClearKeyboardState();
    SDL_JoystickSetVirtualButton(a, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 0);
    SDL_JoystickSetVirtualButton(a, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0);
    sample();

    SDL_JoystickSetVirtualButton(b, SDL_CONTROLLER_BUTTON_B, 1);
    sample();
    SDL_JoystickClose(b);
    Check(SDL_JoystickDetachVirtual(second) == 0, "detach second");
    Check(!(sample().wButtons & XAMINPUT_GAMEPAD_B), "disconnect releases state");
    SDL_JoystickSetVirtualButton(a, SDL_CONTROLLER_BUTTON_A, 1);
    Check(sample().wButtons & XAMINPUT_GAMEPAD_A, "remaining controller works");
    SDL_JoystickClose(a);
    Check(SDL_JoystickDetachVirtual(first) == 0, "detach first");
    sample();
    hid::HandleKeyboardEvent(SDL_SCANCODE_Z, true);
    Check(sample().wButtons & XAMINPUT_GAMEPAD_A, "keyboard with no controllers");
    hid::ClearKeyboardState();
    Check(sample().wButtons == 0, "neutral state does not retain caller bits");
    const int reconnected = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
    Check(reconnected >= 0, "reattach controller");
    auto* c = SDL_JoystickOpen(reconnected);
    Check(c != nullptr, "open reattached controller");
    // Runtime mode: another thread pumps and forwards events to HID.
    hid::SetExternalEventPump(true);
    hid::HandleControllerEvent(SDL_CONTROLLERDEVICEADDED, reconnected);
    hid::HandleControllerEvent(SDL_CONTROLLERDEVICEADDED, reconnected); // duplicate notification
    SDL_JoystickSetVirtualButton(c, SDL_CONTROLLER_BUTTON_Y, 1);
    Check(sample().wButtons & XAMINPUT_GAMEPAD_Y, "reattached controller with external event pump");
    const auto instance = SDL_JoystickInstanceID(c);
    SDL_JoystickClose(c);
    SDL_JoystickDetachVirtual(reconnected);
    hid::HandleControllerEvent(SDL_CONTROLLERDEVICEREMOVED, instance);
    Check(sample().wButtons == 0, "external hot-unplug clears held input");
    SDL_Quit();
    puts("PASS: multiple controllers, keyboard switching, deadzone, triggers, hot-unplug and state reset");
}
