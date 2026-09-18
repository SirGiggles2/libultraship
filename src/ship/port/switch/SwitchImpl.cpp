#ifdef __SWITCH__
#include "ship/port/switch/SwitchImpl.h"
#include <switch.h>
#include <SDL2/SDL.h>
#include "ship/port/switch/SwitchPerformanceProfiles.h"
#include "libultraship/bridge/consolevariablebridge.h"
#include <spdlog/spdlog.h>
#include "ship/Context.h"
#include "ship/audio/Audio.h"

#include <imgui_internal.h>

#define DOCKED_MODE 1
#define HANDHELD_MODE 0

static AppletHookCookie applet_hook_cookie;
static bool isRunning = true;
static bool hasFocus = true;
static bool isShowingVirtualKeyboard = true;
// Which clock service is up. Before 8.0.0 the CPU clock is set through pcv, after that
// through clkrst; neither may be called without its matching initialise.
static bool clkrstReady = false;
static bool pcvReady = false;
// Exit() is reached from two places - the SDL window backend's Destroy() and the game's
// own shutdown - and either can be the only one to run, so it has to be idempotent.
static bool switchInitialised = false;

void DetectAppletMode();
void SetCpuClock(Ship::SwitchProfiles profile);

static void on_applet_hook(AppletHookType hook, void* param);

void Ship::Switch::Init(SwitchPhase phase) {
    switch (phase) {
        case PreInitPhase:
            DetectAppletMode();
            break;
        case PostInitPhase:
            if (switchInitialised) {
                break;
            }
            switchInitialised = true;
            appletInitializeGamePlayRecording();
#ifdef DEBUG
            socketInitializeDefault();
            nxlinkStdio();
#endif
            appletSetGamePlayRecordingState(true);
            appletHook(&applet_hook_cookie, on_applet_hook, NULL);
            appletSetFocusHandlingMode(AppletFocusHandlingMode_NoSuspend);
            if (hosversionBefore(8, 0, 0)) {
                pcvReady = R_SUCCEEDED(pcvInitialize());
            } else {
                clkrstReady = R_SUCCEEDED(clkrstInitialize());
            }
            break;
    }
}

void Ship::Switch::Exit() {
    if (!switchInitialised) {
        return;
    }
    switchInitialised = false;
#ifdef DEBUG
    socketExit();
#endif
    // Put the CPU back to stock before tearing the session down, so an exit while
    // overclocked does not leave the console warm for the next homebrew.
    SetCpuClock(Ship::STOCK);
    if (clkrstReady) {
        clkrstExit();
        clkrstReady = false;
    }
    if (pcvReady) {
        pcvExit();
        pcvReady = false;
    }
    appletUnhook(&applet_hook_cookie);
    appletSetGamePlayRecordingState(false);
}

void Ship::Switch::ImGuiSetupFont(ImFontAtlas* fonts) {
    plInitialize(PlServiceType_User);
    static PlFontData stdFontData, extFontData;

    PlFontData fonts_std;
    PlFontData fonts_ext;

    plGetSharedFontByType(&fonts_std, PlSharedFontType_Standard);
    plGetSharedFontByType(&fonts_ext, PlSharedFontType_NintendoExt);

    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;

    strcpy(config.Name, "Nintendo Standard");
    fonts->AddFontFromMemoryTTF(fonts_std.address, fonts_std.size, 24.0f, &config, fonts->GetGlyphRangesCyrillic());

    strcpy(config.Name, "Nintendo Ext");
    static const ImWchar ranges[] = {
        0xE000, 0xE06B, 0xE070, 0xE07E, 0xE080, 0xE099, 0xE0A0, 0xE0BA, 0xE0C0, 0xE0D6, 0xE0E0, 0xE0F5, 0xE100,
        0xE105, 0xE110, 0xE116, 0xE121, 0xE12C, 0xE130, 0xE13C, 0xE140, 0xE14D, 0xE150, 0xE153, 0,
    };

    fonts->AddFontFromMemoryTTF(fonts_ext.address, fonts_ext.size, 24.0f, &config, ranges);
    fonts->Build();

    plExit();
}

void Ship::Switch::ImGuiProcessEvent(bool wantsTextInput) {
    // Null whenever the active item is not a text field, which is most frames.
    ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetActiveID());

    if (wantsTextInput) {
        if (!isShowingVirtualKeyboard) {
            if (state != nullptr) {
                state->ClearText();
            }

            isShowingVirtualKeyboard = true;
            SDL_StartTextInput();
        }
    } else {
        if (isShowingVirtualKeyboard) {
            isShowingVirtualKeyboard = false;
            SDL_StopTextInput();
        }
    }
}

bool Ship::Switch::IsRunning() {
    return isRunning;
}

void Ship::Switch::GetDisplaySize(int* width, int* height) {
    switch (appletGetOperationMode()) {
        case DOCKED_MODE:
            *width = 1920;
            *height = 1080;
            break;
        case HANDHELD_MODE:
        default:
            *width = 1280;
            *height = 720;
            break;
    }
}

void SetCpuClock(Ship::SwitchProfiles profile) {
    if (profile < Ship::MAXIMUM || profile > Ship::POWERSAVINGM3) {
        return;
    }

    if (hosversionBefore(8, 0, 0)) {
        if (pcvReady) {
            pcvSetClockRate(PcvModule_CpuBus, SWITCH_CPU_SPEEDS_VALUES[profile]);
        }
    } else if (clkrstReady) {
        ClkrstSession session = { 0 };
        if (R_SUCCEEDED(clkrstOpenSession(&session, PcvModuleId_CpuBus, 3))) {
            clkrstSetClockRate(&session, SWITCH_CPU_SPEEDS_VALUES[profile]);
            clkrstCloseSession(&session);
        }
    }
}

void Ship::Switch::ApplyOverclock(void) {
    // Stock rather than maximum by default: an overclock is a deliberate choice, not
    // something a port should impose on every console that runs it.
    SetCpuClock((Ship::SwitchProfiles)CVarGetInteger(CVAR_SWITCH_PERF_MODE, (int)Ship::STOCK));
}

void Ship::Switch::PrintErrorMessageToScreen(const char* str, ...) {
    consoleInit(NULL);

    va_list args;
    va_start(args, str);
    vprintf(str, args);
    va_end(args);

    while (appletMainLoop()) {
        consoleUpdate(NULL);
    }

    consoleExit(NULL);
}

static void on_applet_hook(AppletHookType hook, void* param) {
    AppletFocusState focus_state;

    /* Exit request */
    switch (hook) {
        case AppletHookType_OnExitRequest:
            isRunning = false;
            break;

            /* Focus state*/
        case AppletHookType_OnFocusState:
            focus_state = appletGetFocusState();
            hasFocus = focus_state == AppletFocusState_InFocus;

            if (!hasFocus) {
                SetCpuClock(Ship::STOCK);
            } else {
                Ship::Switch::ApplyOverclock();
                // reinitialize audio subsystem to fix audio problems after resuming from sleep
                // see https://github.com/HarbourMasters/Shipwright/issues/3317
                SPDLOG_INFO("restarting SDL audio system to work around audio problems on resume");
                Ship::Context::GetRawInstance()->GetAudio()->SetCurrentAudioBackend(Ship::AudioBackend::SDL);
            }

            break;

            /* Performance mode */
        case AppletHookType_OnPerformanceMode:
            Ship::Switch::ApplyOverclock();
            break;
        default:
            break;
    }
}

// Shown under the error text on the console screen. Kept short so they fit a single line
// at the bottom of a 80x45 console.
const char* RandomTexts[] = {
    "Guh-huh!",
    "DNI Y RUOY ETUB",
    "Somebody's been eating my notes",
    "Bottles says hi",
    "Mumbo no like this",
    "That tickles!",
    "Not enough jiggies",
    "Kazooie is asleep. Do not disturb.",
};

static const char* RandomText() {
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned)time(nullptr));
        seeded = true;
    }
    return RandomTexts[rand() % (sizeof(RandomTexts) / sizeof(RandomTexts[0]))];
}

void DetectAppletMode() {
    AppletType at = appletGetAppletType();
    if (at == AppletType_Application || at == AppletType_SystemApplication)
        return;

    Ship::Switch::PrintErrorMessageToScreen("\x1b[2;2HThis was launched in applet mode, which does not have"
                                            "\x1b[3;2Henough memory to run the game."
                                            "\x1b[5;2HRelaunch in full-memory mode: hold R while opening any"
                                            "\x1b[6;2Hinstalled game to reach the homebrew menu."
                                            "\x1b[44;2H%s",
                                            RandomText());
}

void Ship::Switch::ThrowMissingOTR(std::string OTRPath) {
    Ship::Switch::PrintErrorMessageToScreen("\x1b[2;2HCould not find %s."
                                            "\x1b[4;2HGenerate the .o2r files on a PC and copy them next to"
                                            "\x1b[5;2Hthe .nro on your SD card, then relaunch."
                                            "\x1b[44;2H%s",
                                            OTRPath.c_str(), RandomText());
}
#endif
