#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <signal.h>
#ifdef __linux__
#include <unistd.h> /* access() */
#else
#include <io.h>
#endif

#include <SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "pac.h"

#define CONTROLLER_DEADZONE 8000

static bool should_quit = false;
static bool has_focus = true;
static bool is_paused = false;

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *texture = NULL;
static SDL_GameController *controller = NULL;
static SDL_AudioDeviceID audio_device = 0;

static pac *p = NULL;
static uint32_t current_time = 0;
static uint32_t last_time = 0;

static void update_screen(pac *const p)
{
	int pitch = 0;
	void *pixels = NULL;

	if (SDL_LockTexture(texture, NULL, &pixels, &pitch)) {
		SDL_Log("Unable to lock texture: %s", SDL_GetError());
	} else {
		SDL_memcpy(pixels, p->screen_buffer, pitch * PAC_SCREEN_HEIGHT);
	}
	SDL_UnlockTexture(texture);
}

static void push_sample(int16_t sample)
{
	if (SDL_QueueAudio(audio_device, &sample, sizeof(int16_t) * 1)) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to queue audio: %s", SDL_GetError());
	}
}

static void send_quit_event()
{
	should_quit = true;
}

static void screenshot(pac *const p)
{
	// generate filename
	time_t t;
	struct tm tm;
	char *filename;
	int retval;
	
	t = time(NULL);
	filename = (char *) SDL_calloc(FILENAME_MAX, sizeof(char));

#ifdef __linux__
	tm = *localtime(&t);
	sprintf(filename, "%d-%d-%d %d.%d.%d - %s.bmp", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, "pac");
	retval = access(filename, R_OK);
#else
	localtime_s(&tm, &t);
	sprintf_s(filename, FILENAME_MAX, "%d-%d-%d %d.%d.%d - %s.bmp", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, "pac");
	retval = _access(filename, 0);
#endif

	if (retval) { // file not exists
		const uint32_t pitch = sizeof(uint8_t) * 3 * PAC_SCREEN_WIDTH;
		const uint8_t depth = 32;
		// render screen buffer to BMP file
		SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(p->screen_buffer, PAC_SCREEN_WIDTH, PAC_SCREEN_HEIGHT, depth, pitch, SDL_PIXELFORMAT_RGB24);
		if (s) {
			SDL_SaveBMP(s, filename);
			SDL_FreeSurface(s);
			SDL_Log("Saved screenshot: %s", filename);
		} else {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to create RGB surface: %s", SDL_GetError());
		}
	} else { // file exists
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Cannot save screenshot: file %s already exists", filename);
	}
	SDL_free(filename);
}

static void mainloop(void)
{
	current_time = SDL_GetTicks();

	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		if (e.type == SDL_QUIT) {
			send_quit_event();
		} else if (e.type == SDL_WINDOWEVENT) {
			if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
				has_focus = true;
			} else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
				has_focus = false;
			}
		} else if (e.type == SDL_KEYDOWN) {
			switch (e.key.keysym.scancode) {
			case SDL_SCANCODE_1:
				p->p1_start = 1;
				break; // start (1p)
			case SDL_SCANCODE_2:
				p->p2_start = 1;
				break; // start (2p)
			case SDL_SCANCODE_UP:
				p->p1_up = 1;
				break; // up
			case SDL_SCANCODE_DOWN:
				p->p1_down = 1;
				break; // down
			case SDL_SCANCODE_LEFT:
				p->p1_left = 1;
				break; // left
			case SDL_SCANCODE_RIGHT:
				p->p1_right = 1;
				break; // right
			case SDL_SCANCODE_C:
			case SDL_SCANCODE_5:
				p->coin_s1 = 1;
				break; // coin (slot 1)
			case SDL_SCANCODE_V:
			case SDL_SCANCODE_6:
				p->coin_s2 = 1;
				break; // coin (slot 2)
			case SDL_SCANCODE_T:
				p->board_test = 1;
				break; // board test
			case SDL_SCANCODE_M:
				p->mute_audio = !p->mute_audio;
				break;
			case SDL_SCANCODE_P:
				is_paused = !is_paused;
				break;
			case SDL_SCANCODE_S:
				screenshot(p);
				break;
			case SDL_SCANCODE_I:
				pac_cheat_invincibility(p);
				break;
			case SDL_SCANCODE_SPACE:
				p->speed = 2;
				break;
			default:
				break;
			}
		} else if (e.type == SDL_KEYUP) {
			switch (e.key.keysym.scancode) {
			case SDL_SCANCODE_1:
				p->p1_start = 0;
				break; // start (1p)
			case SDL_SCANCODE_2:
				p->p2_start = 0;
				break; // start (2p)
			case SDL_SCANCODE_UP:
				p->p1_up = 0;
				break; // up
			case SDL_SCANCODE_DOWN:
				p->p1_down = 0;
				break; // down
			case SDL_SCANCODE_LEFT:
				p->p1_left = 0;
				break; // left
			case SDL_SCANCODE_RIGHT:
				p->p1_right = 0;
				break; // right
			case SDL_SCANCODE_C:
			case SDL_SCANCODE_5:
				p->coin_s1 = 0;
				break; // coin
			case SDL_SCANCODE_V:
			case SDL_SCANCODE_6:
				p->coin_s2 = 0;
				break; // coin (slot 2)
			case SDL_SCANCODE_T:
				p->board_test = 0;
				break; // board test
			case SDL_SCANCODE_SPACE:
				p->speed = 1;
				// clear the queued audio to avoid audio delays
				SDL_ClearQueuedAudio(audio_device);
				break;
			default:
				break;
			}
		} else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
			switch (e.cbutton.button) {
			case SDL_CONTROLLER_BUTTON_A:
				p->coin_s1 = 1;
				break;
			case SDL_CONTROLLER_BUTTON_DPAD_UP:
				p->p1_up = 1;
				break;
			case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
				p->p1_down = 1;
				break;
			case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
				p->p1_left = 1;
				break;
			case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
				p->p1_right = 1;
				break;
			case SDL_CONTROLLER_BUTTON_START:
				p->p1_start = 1;
				break;
			case SDL_CONTROLLER_BUTTON_BACK:
				p->p2_start = 1;
				break;
			case SDL_CONTROLLER_BUTTON_B:
				p->speed = 2;
				break;
			case SDL_CONTROLLER_BUTTON_X:
				pac_cheat_invincibility(p);
				break;
			}
		} else if (e.type == SDL_CONTROLLERBUTTONUP) {
			switch (e.cbutton.button) {
			case SDL_CONTROLLER_BUTTON_A:
				p->coin_s1 = 0;
				break;
			case SDL_CONTROLLER_BUTTON_DPAD_UP:
				p->p1_up = 0;
				break;
			case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
				p->p1_down = 0;
				break;
			case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
				p->p1_left = 0;
				break;
			case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
				p->p1_right = 0;
				break;
			case SDL_CONTROLLER_BUTTON_START:
				p->p1_start = 0;
				break;
			case SDL_CONTROLLER_BUTTON_BACK:
				p->p2_start = 0;
				break;
			case SDL_CONTROLLER_BUTTON_B:
				p->speed = 1;
				break;
			}
		} else if (e.type == SDL_CONTROLLERAXISMOTION) {
			switch (e.caxis.axis) {
			case SDL_CONTROLLER_AXIS_LEFTX:
				if (e.caxis.value < -CONTROLLER_DEADZONE) {
					p->p1_left = 1;
				} else if (e.caxis.value > CONTROLLER_DEADZONE) {
					p->p1_right = 1;
				} else {
					p->p1_left = 0;
					p->p1_right = 0;
				}
				break;
			case SDL_CONTROLLER_AXIS_LEFTY:
				if (e.caxis.value < -CONTROLLER_DEADZONE) {
					p->p1_up = 1;
				} else if (e.caxis.value > CONTROLLER_DEADZONE) {
					p->p1_down = 1;
				} else {
					p->p1_up = 0;
					p->p1_down = 0;
				}
				break;
			}
		} else if (e.type == SDL_CONTROLLERDEVICEADDED) {
			const int controller_id = e.cdevice.which;
			if (controller == NULL && SDL_IsGameController(controller_id)) {
				controller = SDL_GameControllerOpen(controller_id);
			}
		} else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
			if (controller != NULL) {
				SDL_GameControllerClose(controller);
				controller = NULL;
			}
		}
	}

	if (!is_paused && has_focus) {
		pac_update(p, (current_time - last_time));
	}

	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);

	last_time = current_time;
}

void pac_quit(void)
{
	static bool once = false;

	if (once) { // in order to be sure that this function is called only one time
		return;
	}
	once = true;

	if (p && p->audio_buffer) {
		free(p->audio_buffer);
		SDL_free(p);
	}
	if (controller) {
		SDL_GameControllerClose(controller);
	}
	if (texture) {
		SDL_DestroyTexture(texture);
	}
	if (renderer) {
		SDL_DestroyRenderer(renderer);
	}
	if (window) {
		SDL_DestroyWindow(window);
	}
	if (audio_device) {
		SDL_CloseAudioDevice(audio_device);
	}
	SDL_Quit();
}

#ifdef main
#undef main
#endif // main

int main(int argc, char **argv)
{
	int retval, i;
	SDL_RendererInfo renderer_info;
	SDL_AudioSpec audio_spec;
	char *base_path, *rom_dir;

#ifdef __linux__
	signal(SIGINT, (__sighandler_t) send_quit_event);
#else
	signal(SIGINT, (_crt_signal_t) send_quit_event);
#endif
	atexit(pac_quit);

	// SDL init
	retval = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER);
	if (retval) {
		SDL_Log("Unable to initialise SDL: %s", SDL_GetError());

		exit(EXIT_FAILURE);
	}

	SDL_SetHint(SDL_HINT_BMP_SAVE_LEGACY_FORMAT, "1");

	// create SDL window
	window = SDL_CreateWindow("pac", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, PAC_SCREEN_WIDTH * 2, PAC_SCREEN_HEIGHT * 2, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if (window == NULL) {
		SDL_Log("Unable to create window: %s", SDL_GetError());

		exit(EXIT_FAILURE);
	}

	SDL_SetWindowMinimumSize(window, PAC_SCREEN_WIDTH, PAC_SCREEN_HEIGHT);
	// create renderer
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (renderer == NULL) {
		SDL_Log("Unable to create renderer: %s", SDL_GetError());

		exit(EXIT_FAILURE);
	}

	retval = SDL_RenderSetLogicalSize(renderer, PAC_SCREEN_WIDTH, PAC_SCREEN_HEIGHT);
	if (retval) {
		SDL_Log("Unable to set logical size: %s", SDL_GetError());

		exit(EXIT_FAILURE);
	}

	// print info on renderer:
	retval = SDL_GetRendererInfo(renderer, &renderer_info);
	if (retval) {
		SDL_Log("Unable to get render info: %s", SDL_GetError());

		exit(EXIT_FAILURE);
	}
	SDL_Log("Using renderer %s", renderer_info.name);

	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, PAC_SCREEN_WIDTH, PAC_SCREEN_HEIGHT);
	if (texture == NULL) {
		SDL_Log("Unable to create texture: %s", SDL_GetError());

		exit(EXIT_FAILURE);
	}

	// audio init
	SDL_zero(audio_spec);
	audio_spec.freq = 44100;
	audio_spec.format = AUDIO_S16SYS;
	audio_spec.channels = 1;
	audio_spec.samples = 1024;
	audio_spec.callback = NULL;
	audio_device = SDL_OpenAudioDevice(NULL, 0, &audio_spec, NULL, 0);

	if (audio_device) {
		SDL_Log("Audio device has been opened (%s)", SDL_GetCurrentAudioDriver());
	} else {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to open audio: %s", SDL_GetError());
	}
	SDL_PauseAudioDevice(audio_device, 0); // start playing

	// controller init: opening the first available controller
	controller = NULL;
	for (i = 0; i < SDL_NumJoysticks(); i++) {
		if (SDL_IsGameController(i)) {
			controller = SDL_GameControllerOpen(i);
			if (controller) {
				SDL_Log("game controller detected: %s", SDL_GameControllerNameForIndex(i));
				break;
			} else {
				SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "could not open game controller: %s", SDL_GetError());
			}
		}
	}

	// pac init
	// ignoring "-psn" argument from macOS Finder
	// https://hg.libsdl.org/SDL/file/c005c49beaa9/test/testdropfile.c#l47
	if (argc > 1 && SDL_strncmp(argv[1], "-psn", 4) == 0) {
		argc--;
		argv++;
	}

	base_path = SDL_GetBasePath();
	rom_dir = argc > 1 ? argv[1] : base_path;

	p = (pac *) SDL_malloc(sizeof(pac));
	retval = pac_init(p, rom_dir);
	SDL_free(base_path);

	if (retval) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Missing rom files", "Please copy rom files next to pac executable.", window);

		exit(EXIT_FAILURE);
	}
	p->sample_rate = audio_spec.freq;
	p->push_sample = push_sample;
	p->update_screen = update_screen;
	update_screen(p);

	// main loop
	current_time = last_time = SDL_GetTicks();

#ifdef __EMSCRIPTEN__
	emscripten_set_main_loop(mainloop, 0, 1);
#else
	while (!should_quit) {
		mainloop();
	}
#endif

	pac_quit();

	exit(EXIT_SUCCESS);
}
#ifdef __cplusplus
}
#endif