/* Exercise SDL3's audio-stream callback with a deterministic dummy device.
 * Queued guest PCM must be consumed even when no sound card is present. */
#include <stdint.h>
#include <stdio.h>
#include <SDL3/SDL.h>
#include "win32hle.h"

const recomp_dispatch_entry_t recomp_dispatch_table[1];
const uint32_t recomp_dispatch_count = 0;

int main(void) {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    int stream = hle_audio_stream_open(44100, 2);
    if (stream < 0) { fprintf(stderr, "FAIL audio stream opens\n"); return 1; }
    static int16_t pcm[2 * 44100];
    hle_audio_stream_push(stream, pcm, 44100);
    int before = hle_audio_stream_queued(stream);
    for (int i = 0; i < 100 && hle_audio_stream_queued(stream) >= before; i++) SDL_Delay(10);
    int after = hle_audio_stream_queued(stream);
    hle_audio_stream_close(stream);
    if (before <= 0 || after >= before) {
        fprintf(stderr, "FAIL audio cursor did not advance (%d -> %d)\n", before, after);
        return 1;
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    printf("audio_selftest: callback consumed queued PCM (%d -> %d frames)\n", before, after);
    return 0;
}
