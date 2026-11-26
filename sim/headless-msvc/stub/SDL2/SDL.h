/* headless stub: just enough SDL for sim/main.c to compile; --snapshot never calls these */
#pragma once
#include <stdint.h>
typedef uint8_t Uint8; typedef uint32_t Uint32; typedef uint32_t SDL_AudioDeviceID;
typedef struct { int freq; int format; int channels; int samples; void (*callback)(void *, Uint8 *, int); void *userdata; } SDL_AudioSpec;
#define AUDIO_S16SYS 0x8010
#define SDL_INIT_AUDIO 0x10
#define SDL_BUTTON_LEFT 1
#define SDL_BUTTON(x) (1u << ((x) - 1))
enum { SDL_SCANCODE_A = 4, SDL_SCANCODE_B, SDL_SCANCODE_C, SDL_SCANCODE_D, SDL_SCANCODE_E, SDL_SCANCODE_F, SDL_SCANCODE_G, SDL_SCANCODE_H,
       SDL_SCANCODE_I, SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_L, SDL_SCANCODE_M, SDL_SCANCODE_N, SDL_SCANCODE_O, SDL_SCANCODE_P,
       SDL_SCANCODE_Q, SDL_SCANCODE_R, SDL_SCANCODE_S, SDL_SCANCODE_T, SDL_SCANCODE_U, SDL_SCANCODE_V, SDL_SCANCODE_W, SDL_SCANCODE_X,
       SDL_SCANCODE_Y, SDL_SCANCODE_Z, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_ESCAPE = 41 };
Uint32 SDL_GetTicks(void); void SDL_Delay(Uint32 ms); const char *SDL_GetError(void);
int SDL_InitSubSystem(Uint32 f); SDL_AudioDeviceID SDL_OpenAudioDevice(const char *, int, const SDL_AudioSpec *, SDL_AudioSpec *, int);
void SDL_PauseAudioDevice(SDL_AudioDeviceID, int); void SDL_LockAudioDevice(SDL_AudioDeviceID); void SDL_UnlockAudioDevice(SDL_AudioDeviceID);
const Uint8 *SDL_GetKeyboardState(int *n); Uint32 SDL_GetMouseState(int *x, int *y);
