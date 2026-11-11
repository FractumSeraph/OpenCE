/* Adapted from OpenH1/OpenH1, revision 6750db5a6c9716857bd1e63d9ec188a20c49f1d3.
 * Copyright OpenH1 contributors; AGPL-3.0-or-later.
 * Provenance and hardware measurements: docs/reading/xbox_audio.md. */

/*
DSOUND_SDL.C

Xbox DirectSound for all ports: a software mixer on an SDL3 audio stream.

The game plays most sounds through DirectSound streams: 16-bit stereo PCM
(music and other uncompressed sounds) and Xbox ADPCM, mono or stereo, at 22
or 44 kHz; the loading screen's chant is buffers (below). A packet is decoded to 16-bit PCM when the game submits it, since
the sound cache may reuse its memory once the packet completes. The mixer
runs on SDL's audio thread; for every voice it resamples to the output rate
with the measured Xbox resampling kernel (which is how SetFrequency changes
pitch; resampling) and applies:
	- the stream volume (millibels),
	- the front left and right mix bin volumes of 2D voices,
	- for 3D voices, DirectSound's inverse distance rolloff between the
	  minimum and maximum distance, an equal power pan from the source's
	  direction in listener space when HRTF is disabled, and the I3DL2 direct path, obstruction
	  and occlusion levels, their high frequency levels a low pass at the
	  environment's HF reference: a sound behind a wall is muffled,
	- for 3D voices, the Doppler shift of the voice's and the listener's
	  velocities, which moves the resampling step (doppler_ratio). Positioned
	  stereo sources also shift when audio.stereo_doppler is enabled.
3D voices also send to an I3DL2 reverb of the environment the listener is in
(dsound_xbox_reverb.inl). Mono 3D voices use the measured Xbox HRTF on
stereo and surround outputs (dsound_xbox_hrtf.inl) with audio.hrtf "xbox";
port (this fork): or, on stereo, ChupathingyCE's binaural rendering of
measured head responses for headphones with "on" or "auto" (binaural
rendering). A moving 3D voice is Doppler shifted (doppler_ratio;
audio.doppler = false leaves it), and port (ChupathingyCE's): a voice whose
tag gives it a sound cone is quieter facing away (voice_cone_gain), as
DirectSound did both. A look-ahead limiter keeps the sum under full scale
(limit). A voice the game cuts off while it plays fades out over 5 ms rather
than stopping dead, which clicked (voice_cut).

The output is stereo, 5.1 or 7.1 (audio.channels; port: "auto" is 5.1 or
7.1 on a device of 6 or 8 channels, else stereo). For surround the speaker
config reports AC-3, as an Xbox set to Dolby Digital does: the game then
sends its 2D voices to the center and back mix bins too
(dsound_initialize_channel). 3D voices through the Xbox HRTF play from the
front and back pairs (surround_split); panned, 3D voices and stereo voices
positioned in the world go around the speakers the Xbox's DirectSound panned
them between, the front and back pairs (and on 7.1 the sides), not the
center, which is the 2D voices' (surround_pan). The reverb plays from the
front and back pairs (on 7.1 the sides too). The mix bins 0-5 are in SDL's
channel order (FL, FR, FC, LFE, BL, BR; 7.1 adds SL, SR), so bin n is output
channel n; on 7.1 the back bins play from the side and back pairs. The game
sends nothing to the LFE bin. An explicit LFE send is low-pass filtered at
86 Hz.

Packets the mixer has finished are completed from DirectSoundDoWork, which
the game calls every frame, and from Flush, never from the audio thread:
the game's completion callback is not meant to run concurrently with it.
On the desktop DirectSoundDoWork also runs on sdl_platform.c's swap sound
thread, but only while the game's thread is blocked in the driver showing a
frame (an unfocused window's long swaps), and the game's thread waits for it
to finish before going on: never at once with the game's.

Without an audio device, a clock thread runs the same mixer into a scratch
buffer, so streams still drain at their real rate.

The loading screen's four pitched chant voices use DirectSound buffers.
Their PCM or Xbox ADPCM data is copied and decoded when set, then mixed
dry into the front speakers under the master and effects volumes.

The audio probe (port/linux/game/audio_probe.c) records the mixer's output,
and the reverb's input and output apart (recording).

audio.volume sets the master volume (default 1.0); audio.reverb = false
turns the reverb off; audio.hrtf chooses the HRTF ("xbox", "on", "auto",
"off"; true and false, OpenCE's, are "xbox" and "off"); audio.resampler =
"xbox" uses the measured Xbox kernel. "sinc" selects a band-limited low
pass, and "linear" (or ChupathingyCE's audio.resampling = "linear")
interpolates neighbouring frames; audio.enabled = false skips opening a
device; audio.buffer_frames sets the device's buffer; audio.channels chooses
stereo, 5.1 or 7.1; audio.doppler and audio.stereo_doppler are above;
audio.output_device names the output (port_config.c).
*/

#include "platform.h"
#include "sdl_platform.h"
#include "port_config.h"
#include "voice_audio.h"
#include "thread_stop.h"
#include "capture.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef HALO_BROWSER
#include <emscripten/emscripten.h>
#include <stdatomic.h>
#endif

#define OUTPUT_RATE 48000
#define OUTPUT_CHANNELS 2 /* the stereo mix's, and the reverb's outputs */
/* (port: 7.1's eight, ChupathingyCE's; the mixer's tests may take 5.1's six,
whose layout they read) */
#ifndef MAXIMUM_OUTPUT_CHANNELS
#define MAXIMUM_OUTPUT_CHANNELS 8
#endif
#define MAXIMUM_STREAM_PACKETS 64
#define MIX_CHUNK_FRAMES 1024
/* port (ChupathingyCE's): the most mixed in one pass under mixer_lock (mix):
the game's thread waits out a pass's mix of a voice to start, stop or change
a sound, so a pass is about 5 ms of sound - at 1024, up to 2.7 ms of the
Switch's game thread for each such call in a busy firefight */
#define MIX_PASS_FRAMES 256
/* the frames a voice cut off fades out over (voice_cut): 5 ms */
#define CUT_FADE_FRAMES 240

#include "dsound_xbox_types.h"

/* port (ChupathingyCE's): the binaural rendering (binaural) */
#define HRTF_MAXIMUM_TAPS 64
#define HRTF_MAXIMUM_DELAY 48
#define HRTF_INPUT 2048 /* a mix and the longest delay before it, a power of 2 */
#define HRTF_FADE_SECONDS 0.1f


/* ---------- voices */

struct voice_packet
{
	XMEDIAPACKET packet;
	short *samples;           /* interleaved, source channel count */
	unsigned long frames;
	BOOL finished;            /* played out by the mixer, not yet completed */
};

struct sdl_stream
{
	/* must be first: in C an IDirectSoundStream is just { lpVtbl } */
	IDirectSoundStream object;
	struct sdl_stream *next;
	ULONG reference_count;
	LPFNXMEDIAOBJECTCALLBACK callback;
	LPVOID context;

	/* format */
	BOOL adpcm;
	unsigned long channels;
	DWORD sample_rate;
	DWORD frequency;

	BOOL paused;

	/* 2D gains */
	float volume;             /* SetVolume */
	float mix_left, mix_right;
	/* a stereo voice of a sound in the world: panned towards it, -1 left
	to 1 right, its distance, and the fade of its volume with distance that
	the game made (dsound_sdl_stream_set_stereo_position) */
	BOOL stereo_positioned;
	float stereo_pan;
	float stereo_distance;
	float stereo_distance_fade;
	/* how fast it comes towards the listener, in world units a second, for
	its Doppler effect (doppler_ratio) */
	float stereo_approach;
	float headroom;
	/* on surround output: the gains from each source channel to each
	speaker mix bin (SetMixBins, SetMixBinVolumes) */
	float surround_bins[2][MAXIMUM_OUTPUT_CHANNELS];

	/* 3D */
	BOOL has_3d;
	DWORD mode;
	float position[3];
	float minimum_distance, maximum_distance;
	/* the I3DL2 direct path's level and high frequency level, in millibels
	with obstruction and occlusion (SetI3DL2Source) */
	LONG direct, direct_hf;
	/* the I3DL2 room (reverb) send's level and high frequency level, in
	millibels with occlusion, and its rolloff factor (SetI3DL2Source) */
	LONG room, room_hf;
	float room_rolloff_factor;
	/* the voice's velocity in the game's units a second (SetVelocity) */
	float velocity[3];
	/* port (ChupathingyCE's): SetConeAngles (degrees), SetConeOrientation
	and SetConeOutsideVolume (millibels): voice_cone_gain */
	DWORD cone_inside, cone_outside;
	float cone_orientation[3];
	LONG cone_outside_volume;

	struct voice_packet packets[MAXIMUM_STREAM_PACKETS];
	unsigned long packet_head;
	unsigned long packet_count;
	/* the next frame to take from the first packet not finished */
	unsigned long cursor;
	/* the resampler's (resampler_reset): the last RESAMPLER_HISTORY frames
	taken, each channel's apart and each frame twice, RESAMPLER_HISTORY apart,
	so that the frames a low pass reaches are side by side however the ring
	wraps (resampler_dot); how many were taken, the frame the output is at and
	how far past it, and the frames of silence taken since the packets ran
	out */
	float history[2][2 * RESAMPLER_HISTORY];
	unsigned long history_count;
	unsigned long center;
	double phase;
	unsigned long silence;
	/* gains and low pass coefficients the mixer is ramping from, to avoid
	clicks */
	float current_left, current_right, current_room;
	float current_direct_lowpass, current_room_lowpass;
	float current_surround[2][MAXIMUM_OUTPUT_CHANNELS];
	BOOL gains_valid;
	/* the direct path's low pass, for each channel, and the room send's */
	float direct_lowpass[2];
	float room_lowpass;
	/* port (ChupathingyCE's binaural rendering, binaural): the voice's mono
	direct path, the last taps - 1 samples of each ear's copy of it delayed
	by its interaural delay, where the input is written next, and the
	filters, delays and gain it is moving from (none yet while hrtf_valid is
	FALSE) */
	float hrtf_input[HRTF_INPUT];
	float hrtf_tail[2][HRTF_MAXIMUM_TAPS];
	unsigned long hrtf_position;
	float hrtf_filters[2][HRTF_MAXIMUM_TAPS];
	float hrtf_delays[2];
	float current_hrtf;
	BOOL hrtf_valid;
	/* what the voice was playing when the game cut it off, faded out
	(voice_cut): frames of output_channels floats and their room send, and
	how many of them have played */
	float cut[CUT_FADE_FRAMES * MAXIMUM_OUTPUT_CHANNELS];
	float cut_send[CUT_FADE_FRAMES];
	unsigned long cut_played;
	/* being flushed (stream_flush): the mixer leaves the packets not yet
	given back, which the cut has played already, while the game's callback
	runs without the lock */
	BOOL flushing;
	double current_step;
	float current_doppler;
	float target_left, target_right, target_room;
	double target_step;
	float target_direct_lowpass, target_room_lowpass;
	float target_surround[2][MAXIMUM_OUTPUT_CHANNELS];
	unsigned long ramp_frames;
	BOOL hrtf_on;
	float hrtf_mix;
	struct hrtf_parameters hrtf_current, hrtf_target;
	float hrtf_frames[2][HRTF_FRAMES];
	float hrtf_bands[HRTF_BANDS - 1][2];
};

static pthread_mutex_t mixer_lock = PTHREAD_MUTEX_INITIALIZER;
/* port (ChupathingyCE's): the mixer holds mixer_lock a voice at a time, not
for a whole chunk's mix (mix): the game's thread, which takes it to start,
stop, feed or change a sound, waited out every voice's mix, 7% of a Switch's
game thread in a 30-player match. The game's every taking of the lock counts
in mixer_lock_wanted, and between voices the mixer lets it in (musl's mutex
hands the lock to no waiter: unlocked and taken again at once, the mixer
would most often have it back first). Between voices the next stream to mix
is mix_cursor, which a stream let go then moves past it (stream_release). */
static int mixer_lock_wanted;
static struct sdl_stream *mix_cursor;
#define mixer_lock_take() \
	do \
	{ \
		__atomic_add_fetch(&mixer_lock_wanted, 1, __ATOMIC_RELAXED); \
		pthread_mutex_lock(&mixer_lock); \
		__atomic_sub_fetch(&mixer_lock_wanted, 1, __ATOMIC_RELAXED); \
	} \
	while (0)
static struct sdl_stream *streams;

/* the listener, in DirectSound's left-handed +y up space */
static struct
{
	float position[3];
	float front[3];
	float top[3];
	float rolloff_factor;
	/* meters a unit (SetDistanceFactor): the game sets 3.048, a world unit
	being 10 feet. Only Doppler uses it (doppler_ratio) */
	float distance_factor;
	/* the listener's velocity in the game's units a second (SetVelocity):
	the game sets none, and gives each voice its velocity relative to the
	listener (sound_manager.c) */
	float velocity[3];
	/* DirectSound's Doppler factor, which the game leaves at 1: a multiple
	of the velocities */
	float doppler_factor;
} listener = { { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, 1.0f, 1.0f, { 0, 0, 0 }, 1.0f };

/* port (this fork's browser build): audio.volume, and the master volume
the mixer plays at, 0 while the page is muted (platform_audio_set_muted) */
static float configured_master_volume = 1.0f;
static float master_volume = 1.0f;
#ifdef HALO_BROWSER
static BOOL master_muted = TRUE;
#else
static BOOL master_muted = FALSE;
#endif

void platform_audio_set_muted(BOOL muted)
{
	pthread_mutex_lock(&mixer_lock);
	master_muted = muted;
	master_volume = master_muted ? 0.0f : configured_master_volume;
	pthread_mutex_unlock(&mixer_lock);
}

/* Buffers own decoded samples independently of the game's loading memory.
All mutable buffer state and the list are protected by mixer_lock. */
struct sdl_buffer
{
	struct sdl_buffer *next;
	ULONG reference_count;
	unsigned long channels, block_bytes, block_frames;
	BOOL adpcm, playing, looping;
	short *samples;
	unsigned long frames, loop_start, loop_end;
	double position, rate;
	float volume, current_volume;
};

static struct sdl_buffer *buffers;
/* 2, 6 or 8 (audio.channels: stereo, 5.1, 7.1), chosen when the device
first opens; the game sets its mix bins by it, so it holds while the game
runs (another device is given the same channels, which SDL converts) */
static unsigned long output_channels = 2;
/* audio.hrtf "xbox": mono 3D voices through the measured Xbox HRTF
(dsound_xbox_hrtf.inl), crossfaded as it is turned on or off */
static BOOL hrtf_enabled = TRUE;
/* port (ChupathingyCE's): audio.hrtf "on" or "auto": 3D voices rendered for
headphones on stereo (binaural, hrtf_update), and how far the mixer has
faded to it (0 to 1) */
static volatile BOOL hrtf_wanted = FALSE;
static float hrtf_level = 0.0f;
/* port (ChupathingyCE's): audio.doppler */
static BOOL doppler_enabled = TRUE;

/* the I3DL2 listener properties (SetI3DL2Listener), which the reverb models:
DirectSound's defaults until the game sets an environment, their room level
leaving it silent; serial counts the changes */
static DSI3DL2LISTENER environment =
{
	DSBVOLUME_MIN, 0, 0.0f, 1.49f, 0.83f, -2602, 0.007f, 200, 0.011f, 100.0f, 100.0f, 5000.0f
};
static unsigned long environment_serial;
/* audio.reverb */
static BOOL reverb_enabled = TRUE;
/* audio.stereo_doppler: positioned stereo voices shift as 3D ones do
(doppler_ratio); the Xbox played stereo sounds at their own pitch */
static BOOL stereo_doppler_enabled = FALSE;

static float gain_from_millibels(LONG millibels)
{
	if (millibels <= DSBVOLUME_MIN)
		return 0.0f;
	return powf(10.0f, (float)millibels / 2000.0f);
}

/* value, or 0 once it is far too small to hear: the state of a filter or of
the reverb fed silence decays into the denormals, which many CPUs work out
slowly, and neither x86 nor ARM flushes them to zero by default */
static float voice_level(float gain)
{
	float size = fabsf(gain), level;

	if (size >= VOICE_LEVEL_KNEE)
		return gain;
	if (size <= 0.0f)
		return 0.0f;
	level = (VOICE_LEVEL_SILENT + 20.0f * log10f(size)) * VOICE_LEVEL_STEP;
	if (level <= 0.0f)
		return 0.0f;
	return gain < 0.0f ? -level : level;
}

static float flush_denormal(float value)
{
	return fabsf(value) < 1.0e-20f ? 0.0f : value;
}

/* cos(2 pi f / OUTPUT_RATE) of a filter's frequency f, within what the
output rate holds */
static float frequency_cosine(float frequency)
{
	if (frequency < 20.0f)
		frequency = 20.0f;
	if (frequency > 0.45f * OUTPUT_RATE)
		frequency = 0.45f * OUTPUT_RATE;
	return cosf(2.0f * 3.14159265f * frequency / OUTPUT_RATE);
}

/* the coefficient a of the one-pole low pass y = x + a (y' - x) whose gain is
gain (at most 1) at the frequency of cosine (frequency_cosine) */
static float lowpass_coefficient(float gain, float cosine)
{
	float a, b;

	if (gain >= 1.0f)
		return 0.0f;
	if (gain < 0.001f)
		gain = 0.001f;
	a = 1.0f - gain * gain;
	b = 1.0f - gain * gain * cosine;
	return (b - sqrtf(b * b - a * a)) / a;
}

/* ---------- decoding */

static const int ima_index_table[16] =
{
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8,
};

static const int ima_step_table[89] =
{
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
	253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
	1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
	3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
	11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
	32767,
};

static int ima_expand(int nibble, int *predictor, int *index)
{
	int step = ima_step_table[*index];
	int difference = step >> 3;

	if (nibble & 1) difference += step >> 2;
	if (nibble & 2) difference += step >> 1;
	if (nibble & 4) difference += step;
	if (nibble & 8) difference = -difference;
	*predictor += difference;
	if (*predictor > 32767) *predictor = 32767;
	if (*predictor < -32768) *predictor = -32768;
	*index += ima_index_table[nibble];
	if (*index < 0) *index = 0;
	if (*index > 88) *index = 88;
	return *predictor;
}

/* Xbox ADPCM: per block, a 4-byte header per channel (first sample, step
index), then 4-byte groups of eight nibbles, low nibble first, alternating
between channels; 64 samples per channel. The header's sample is the block's
first, and the nibbles code the 63 after it: the 64th nibble only pads the
block (the maps' sounds always have 0 there), and decoding it in place of the
header's sample put a wrong sample in every 64, a buzz at 344 Hz in 22 kHz
sounds. */
static short *decode_adpcm(const unsigned char *source, unsigned long size, unsigned long channels,
	unsigned long *frame_count)
{
	unsigned long block_bytes = XBOX_ADPCM_BLOCK_BYTES * channels;
	unsigned long blocks = size / block_bytes;
	short *samples = malloc((blocks ? blocks : 1) * XBOX_ADPCM_BLOCK_SAMPLES * channels * sizeof(short));
	unsigned long block, channel;

	if (!samples)
	{
		*frame_count = 0;
		return NULL;
	}
	for (block = 0; block < blocks; block++)
	{
		const unsigned char *data = source + block * block_bytes;
		short *output = samples + block * XBOX_ADPCM_BLOCK_SAMPLES * channels;

		for (channel = 0; channel < channels; channel++)
		{
			const unsigned char *header = data + channel * 4;
			int predictor = (short)(header[0] | (header[1] << 8));
			int index = header[2] > 88 ? 88 : header[2];
			unsigned long group, byte;

			output[channel] = (short)predictor;
			for (group = 0; group < 8; group++)
			{
				const unsigned char *nibbles = data + 4 * channels + (group * channels + channel) * 4;

				for (byte = 0; byte < 4; byte++)
				{
					/* nibble n codes sample n + 1 */
					unsigned long sample = group * 8 + byte * 2 + 1;

					output[sample * channels + channel] = (short)ima_expand(nibbles[byte] & 0xf, &predictor, &index);
					if (sample + 1 < XBOX_ADPCM_BLOCK_SAMPLES)
						output[(sample + 1) * channels + channel] = (short)ima_expand(nibbles[byte] >> 4, &predictor, &index);
				}
			}
		}
	}
	*frame_count = blocks * XBOX_ADPCM_BLOCK_SAMPLES;
	return samples;
}

#ifdef HALO_CONSOLE_CACHE_SCREEN
/* (the first start's screens, before there is a mixer: steamlink_cache_screen.c) */
short *console_decode_adpcm(const unsigned char *source, unsigned long size, unsigned long *frames);
short *console_decode_adpcm(const unsigned char *source, unsigned long size, unsigned long *frames)
{
	return decode_adpcm(source, size, 1, frames);
}
#endif

static short *decode_pcm(const unsigned char *source, unsigned long size, unsigned long channels,
	unsigned long *frame_count)
{
	unsigned long frames = size / (2 * channels);
	short *samples = malloc((frames ? frames : 1) * channels * sizeof(short));

	if (samples)
	{
#ifdef HALO_BIG_ENDIAN
		/* (the maps' samples are little-endian) */
		unsigned long index;

		for (index = 0; index < frames * channels; index++)
			samples[index] = (short)(source[index * 2] | (source[index * 2 + 1] << 8));
#else
		memcpy(samples, source, frames * channels * sizeof(short));
#endif
	}
	*frame_count = samples ? frames : 0;
	return samples;
}

/* ---------- 3D */

static float dot3(const float *a, const float *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* DirectSound's inverse distance law, held beyond the maximum distance. The
game sets a voice's maximum distance before its minimum, so the maximum can
be under the minimum for a moment: it is held at the minimum */
static float distance_attenuation(const struct sdl_stream *stream, float distance, float rolloff_factor)
{
	float minimum = stream->minimum_distance;
	float maximum = stream->maximum_distance > minimum ? stream->maximum_distance : minimum;

	if (minimum <= 0.0f || distance <= minimum)
		return 1.0f;
	if (distance > maximum)
		distance = maximum;
	return minimum / (minimum + rolloff_factor * (distance - minimum));
}

static float clamp_unit(float value)
{
	return value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
}

/* port (ChupathingyCE's): where a 3D voice is from the listener: to the
right, up and ahead, in the game's units */
static void voice_direction(const struct sdl_stream *stream, float *side, float *up, float *ahead)
{
	float offset[3], right_axis[3];
	int axis;

	if (stream->mode == DS3DMODE_HEADRELATIVE)
	{
		*side = stream->position[0];
		*up = stream->position[1];
		*ahead = stream->position[2];
		return;
	}
	for (axis = 0; axis < 3; axis++)
		offset[axis] = stream->position[axis] - listener.position[axis];
	/* left-handed: right = top x front */
	right_axis[0] = listener.top[1] * listener.front[2] - listener.top[2] * listener.front[1];
	right_axis[1] = listener.top[2] * listener.front[0] - listener.top[0] * listener.front[2];
	right_axis[2] = listener.top[0] * listener.front[1] - listener.top[1] * listener.front[0];
	*side = dot3(offset, right_axis);
	*up = dot3(offset, listener.top);
	*ahead = dot3(offset, listener.front);
}

/* port (ChupathingyCE's): DirectSound's sound cone: a voice is at its full
level inside half its inside angle of where it faces, at its outside volume
beyond half its outside angle, and between the two in between (in
millibels). The game sets each sound's cone from its tag; the retail maps'
sounds leave the angles 0 (and the channels start at 0), which the Xbox
played as no cone, as it does 360. The I3DL2 room send is not coned: the
room still hears a sound facing away */
static float voice_cone_gain(const struct sdl_stream *stream)
{
	float to_listener[3], length, facing, angle, inside, outside, fraction;
	int axis;

	if (!stream->cone_outside || (stream->cone_inside >= 360 && stream->cone_outside >= 360))
		return 1.0f;
	for (axis = 0; axis < 3; axis++)
	{
		to_listener[axis] = stream->mode == DS3DMODE_HEADRELATIVE ? -stream->position[axis] :
			listener.position[axis] - stream->position[axis];
	}
	length = sqrtf(dot3(to_listener, to_listener)) * sqrtf(dot3(stream->cone_orientation, stream->cone_orientation));
	if (length <= 1.0e-6f)
		return 1.0f;
	facing = dot3(to_listener, stream->cone_orientation) / length;
	angle = acosf(facing < -1.0f ? -1.0f : facing > 1.0f ? 1.0f : facing) * 57.29578f;
	inside = 0.5f * (float)stream->cone_inside;
	outside = 0.5f * (float)(stream->cone_outside > stream->cone_inside ? stream->cone_outside : stream->cone_inside);
	if (angle <= inside)
		return 1.0f;
	if (angle >= outside || outside <= inside)
		return gain_from_millibels(stream->cone_outside_volume);
	fraction = (angle - inside) / (outside - inside);
	return gain_from_millibels((LONG)(fraction * (float)stream->cone_outside_volume));
}

/* ---------- Doppler

The game gives each 3D voice its velocity (units a second, relative to the
listener: sound_manager.c update_channels) and the Xbox's DirectSound shifted
its pitch by them, as a sound passing by is heard: a Banshee, a Ghost or a
plasma bolt rising as it comes and falling as it goes. An Xbox's shift is
the first order one: the frequency times 1 + (D v_listener - D v_source) / c,
c the speed of sound, D the Doppler factor, the velocities' parts along the
line from the listener to the source in metres a second (the distance
factor's metres a unit), the source's away from the listener, the
listener's towards the source. Recorded on an Xbox, a source passing at 20
units a second was shifted 1.178 coming and 0.822 going, and at 40, 1.356
and 0.644: the same either way, and twice as far at twice the speed, where
(c + D v_listener) / (c + D v_source) gives 1.216 and 0.849, then 1.551 and
0.738. Each part is held within the speed of sound, and the shift to an
octave either way, so that a projectile passing close by, or a velocity
that jumps (a teleport), takes no voice up out of hearing. A voice's shift
is the one its position and velocities give at each mix, moved to over
PARAMETER_RAMP_FRAMES as a change of its pitch is (doppler_heard): an
Xbox's moved with each of the game's updates and settled within 2 ms.
Recorded on an Xbox, a source passing at 40 units a second, its position set
every 34 ms, stepped its pitch at each set; a glide over 80 ms (as the mixer
had) was 166 cents off it, and 37 cents at 20 units a second. */

static float doppler_ratio(const struct sdl_stream *stream)
{
	float offset[3], distance, source_speed, listener_speed = 0.0f, ratio, limit = DOPPLER_SPEED_OF_SOUND;
	int axis;

	/* (port, ChupathingyCE's audio.doppler = false: every voice at its own
	pitch) */
	if (!doppler_enabled)
		return 1.0f;
	if (stereo_doppler_enabled && !stream->has_3d && stream->stereo_positioned)
	{
		ratio = 1.0f + stream->stereo_approach * listener.distance_factor * listener.doppler_factor / DOPPLER_SPEED_OF_SOUND;
		return ratio < 0.5f ? 0.5f : ratio > 2.0f ? 2.0f : ratio;
	}
	if (!stream->has_3d || stream->mode == DS3DMODE_DISABLE)
		return 1.0f;
	for (axis = 0; axis < 3; axis++)
	{
		offset[axis] = stream->position[axis] -
			(stream->mode == DS3DMODE_HEADRELATIVE ? 0.0f : listener.position[axis]);
	}
	distance = sqrtf(dot3(offset, offset));
	if (distance < 1.0e-4f)
		return 1.0f;
	source_speed = listener.doppler_factor * listener.distance_factor * dot3(stream->velocity, offset) / distance;
	/* (head-relative: the voice's velocity is the listener's already) */
	if (stream->mode != DS3DMODE_HEADRELATIVE)
		listener_speed = listener.doppler_factor * listener.distance_factor * dot3(listener.velocity, offset) / distance;
	source_speed = source_speed < -limit ? -limit : source_speed > limit ? limit : source_speed;
	listener_speed = listener_speed < -limit ? -limit : listener_speed > limit ? limit : listener_speed;
	ratio = 1.0f + (listener_speed - source_speed) / DOPPLER_SPEED_OF_SOUND;
	if (ratio > DOPPLER_MAXIMUM_RATIO)
		ratio = DOPPLER_MAXIMUM_RATIO;
	if (ratio < 1.0f / DOPPLER_MAXIMUM_RATIO)
		ratio = 1.0f / DOPPLER_MAXIMUM_RATIO;
	return ratio;
}

/* the shift a voice plays in a mix: its doppler_ratio, kept for the
voices' report (the step ramps to it, mix_voice) */
static float doppler_heard(struct sdl_stream *stream)
{
	stream->current_doppler = doppler_ratio(stream);
	return stream->current_doppler;
}

#include "dsound_xbox_hrtf.inl"

/* On 5.1 (AC-3) the Xbox played a 3D voice from the front pair of speakers
and the back pair, never the center or the LFE: the same pair of ears' sound
on both (the HRTF's, or the pan's), a sound behind heard as the direction
mirrored ahead (azimuth 180 - azimuth), and its power split between the
pairs by its direction, DirectSound's azimuth and elevation (hrtf_angles):
the back pair's share 0.5 + (1 - |elevation| / 90)(|azimuth| / 90 - 1),
held to 0 to 1. Ahead it is all in the front pair, behind all in the back,
to the side half in each, above and below towards half. Recorded on an Xbox
(its AC-3 decoded), a sweep at each 30 degrees around and at 45 and 135
degrees 40 above and below: each pair's two speakers alike in time and level
ahead and behind, both pairs the same time and level apart between their
speakers to the side (660 us, the HRTF's), the back pair's share 0, 0, 0.21,
0.50, 0.79, 1, 1 from ahead round to behind, 0.21 and 0.79 at 45 and 135
degrees, up or down; the center and the LFE silent. So here; on stereo the
front pair alone, all round (spatialize). pair: the front pair's gain and
the back pair's (port: on 7.1 the back pair's share plays from the side and
back pairs, as the Xbox's back bins do there: voice_surround_gains) */
static void surround_split(float right, float up, float ahead, float *pair)
{
	float azimuth = 0.0f, elevation = 0.0f, share;

	if (output_channels < 6)
	{
		pair[0] = 1.0f;
		pair[1] = 0.0f;
		return;
	}
	if (right * right + up * up + ahead * ahead > 1.0e-8f)
		hrtf_angles(right, up, ahead, &azimuth, &elevation);
	share = 0.5f + (1.0f - fabsf(elevation) / 90.0f) * (fabsf(azimuth) / 90.0f - 1.0f);
	share = share < 0.0f ? 0.0f : share > 1.0f ? 1.0f : share;
	pair[0] = sqrtf(1.0f - share);
	pair[1] = sqrtf(share);
}

/* the gains of a 3D voice to the left and right (the pan's), the gains of
the front and back pairs of speakers (surround_split) and the distance
rolloff's attenuation (distance_gain); for a mono voice through the HRTF
(hrtf_on: chosen, or crossfading from it), its gain to each ear and its way
there (hrtf_out) */
static void spatialize(const struct sdl_stream *stream, float *left, float *right, float *pair,
	float *distance_gain, BOOL *hrtf_on, struct hrtf_parameters *hrtf_out)
{
	float offset[3], right_axis[3], distance, attenuation, pan, side, ahead, up;
	int axis;

	if (stream->mode == DS3DMODE_HEADRELATIVE)
	{
		for (axis = 0; axis < 3; axis++)
			offset[axis] = stream->position[axis];
		side = offset[0];
		up = offset[1];
		ahead = offset[2];
	}
	else
	{
		for (axis = 0; axis < 3; axis++)
			offset[axis] = stream->position[axis] - listener.position[axis];
		/* left-handed: right = top x front */
		right_axis[0] = listener.top[1] * listener.front[2] - listener.top[2] * listener.front[1];
		right_axis[1] = listener.top[2] * listener.front[0] - listener.top[0] * listener.front[2];
		right_axis[2] = listener.top[0] * listener.front[1] - listener.top[1] * listener.front[0];
		side = dot3(offset, right_axis);
		up = dot3(offset, listener.top);
		ahead = dot3(offset, listener.front);
	}
	/* in the game's units, those of the minimum and maximum distances: the
	distance factor turns units into meters for Doppler, and scaling by it
	here put every 3D sound 3 times as far away, up to 10 dB quieter */
	distance = sqrtf(dot3(offset, offset));
	attenuation = distance_attenuation(stream, distance, listener.rolloff_factor);
	*distance_gain = attenuation;

	/* equal power pan; close sources and sources straight ahead or behind
	stay centred, and neither ear drops below a quarter */
	{
		float horizontal = sqrtf(side * side + ahead * ahead);
		float angle;

		pan = horizontal > 1.0e-4f ? side / horizontal : 0.0f;
		if (distance < stream->minimum_distance && stream->minimum_distance > 0.0f)
			pan *= distance / stream->minimum_distance;
		pan *= 0.75f;
		angle = (pan + 1.0f) * 0.25f * 3.14159265f;
		*left = cosf(angle) * 1.41421356f * 0.70710678f;
		*right = sinf(angle) * 1.41421356f * 0.70710678f;
	}
	/* and through the HRTF, each ear at the pan's gain in the middle (-3
	dB), the head's filters doing the rest */
	surround_split(side, up, ahead, pair);
	*hrtf_on = stream->channels == 1 && (hrtf_enabled || stream->hrtf_mix > 0.0f);
	if (*hrtf_on)
	{
		/* (on surround a sound behind as the direction mirrored ahead) */
		hrtf_place(side, up, output_channels == 8 ? fabsf(ahead) : output_channels == 6 ? fabsf(ahead) : ahead, distance,
			output_channels >= 6 && ahead < 0.0f, hrtf_out);
		hrtf_out->gain = 0.70710678f * attenuation * gain_from_millibels(stream->direct) * voice_cone_gain(stream);
		hrtf_out->pair[0] = pair[0];
		hrtf_out->pair[1] = pair[1];
	}
	/* (port: and the voice's sound cone, ChupathingyCE's) */
	*left *= attenuation * gain_from_millibels(stream->direct) * voice_cone_gain(stream);
	*right *= attenuation * gain_from_millibels(stream->direct) * voice_cone_gain(stream);
}

/* the voice's gains to the left and right, and to the reverb (only 3D voices
send to it, as their I3DL2 mix bin did on the Xbox), with the coefficients of
the low passes of its direct path (3D voices: obstruction and occlusion
muffle it) and of the send; a 3D voice's gains of the front and back pairs
of speakers (pair: surround_split; others 1 and 0); a 3D voice through the
HRTF (hrtf_on) has its way to each ear too (hrtf_out) */
static void voice_gains(const struct sdl_stream *stream, float *left, float *right, float *pair, float *room,
	float *direct_lowpass, float *room_lowpass, BOOL *hrtf_on, struct hrtf_parameters *hrtf_out)
{
	float level_3d = -1.0f, level_direct = -1.0f;

	pair[0] = 1.0f;
	pair[1] = 0.0f;
	*room = 0.0f;
	*direct_lowpass = 0.0f;
	*room_lowpass = 0.0f;
	*hrtf_on = FALSE;
	if (stream->has_3d && stream->mode != DS3DMODE_DISABLE)
	{
		float distance_gain, cosine = frequency_cosine(environment.flHFReference);

		spatialize(stream, left, right, pair, &distance_gain, hrtf_on, hrtf_out);
		level_3d = VOICE_LEVEL_3D * distance_gain * stream->volume;
		level_direct = level_3d * gain_from_millibels(stream->direct);
		/* (port: the sound cone lowers the direct level as the direct path's
		own level does) */
		level_direct *= voice_cone_gain(stream);
		if (stream->direct_hf < stream->direct)
			*direct_lowpass = lowpass_coefficient(gain_from_millibels(stream->direct_hf - stream->direct), cosine);
		if (reverb_enabled)
		{
			LONG level = environment.lRoom + stream->room;
			LONG high_level = environment.lRoom + environment.lRoomHF + stream->room_hf;

			/* VOICE_LEVEL_3D times the room level and the direct path's
			distance rolloff, whatever the room rolloff: recorded on an
			Xbox (issue 792's eighth run), the cave's reverb of a noise
			burst 0.5 to 20 m away (minimum distance 1 m) is 6.0 dB over
			its room level and rolloff to within 0.1 dB, at room levels of
			-20, -10 and 0 dB and the source's own -10 alike, and a room
			rolloff factor of 1 leaves it as it was. (#790's cap at the
			rolloff, fitted at 2 and 10 m, was 4 dB loud from 3 m and 6
			dB quiet within 1 m) */
			*room = VOICE_LEVEL_3D * gain_from_millibels(level) * distance_gain;
			if (*room > 0.0f && high_level < level)
				*room_lowpass = lowpass_coefficient(gain_from_millibels(high_level - level), cosine);
		}
	}
	else if (stream->channels == 2 && stream->stereo_positioned)
	{
		/* the equal power pan of a 3D voice, at the gains of a 2D one when
		centred (the game fades it with distance), its direct path muffled
		and its room send made as a 3D voice's are (SetI3DL2Source) */
		float angle = (stream->stereo_pan + 1.0f) * 0.25f * 3.14159265f;
		float direct = gain_from_millibels(stream->direct);
		float cosine = frequency_cosine(environment.flHFReference);

		*left = cosf(angle) * 1.41421356f * stream->mix_left * direct;
		*right = sinf(angle) * 1.41421356f * stream->mix_right * direct;
		if (stream->direct_hf < stream->direct)
			*direct_lowpass = lowpass_coefficient(gain_from_millibels(stream->direct_hf - stream->direct), cosine);
		if (reverb_enabled)
		{
			LONG level = environment.lRoom + stream->room;
			LONG high_level = environment.lRoom + environment.lRoomHF + stream->room_hf;

			/* the room's rolloff with distance, a 3D voice's; the volume
			holds the game's fade with it, which a 3D voice's room send does
			not take, so it is taken back out (no further than a twentieth:
			past that the send fades out with the sound) */
			*room = gain_from_millibels(level) *
				distance_attenuation(stream, stream->stereo_distance,
					environment.flRoomRolloffFactor + stream->room_rolloff_factor) /
				(stream->stereo_distance_fade > 0.05f ? stream->stereo_distance_fade : 0.05f);
			if (*room > 0.0f && high_level < level)
				*room_lowpass = lowpass_coefficient(gain_from_millibels(high_level - level), cosine);
		}
	}
	else
	{
		*left = stream->mix_left;
		*right = stream->mix_right;
	}
	/* (the Xbox's quietest levels before audio.volume, which it did not
	have: voice_level) */
	if (level_3d < 0.0f)
	{
		*left = voice_level(*left * stream->volume) * master_volume;
		*right = voice_level(*right * stream->volume) * master_volume;
		*room = voice_level(*room * stream->volume) * master_volume;
	}
	else
	{
		/* a 3D voice's are those of its volume and distance rolloff
		together, at VOICE_LEVEL_3D times, its other gains after them:
		recorded on an Xbox (issue 714's seventh run), a voice 10 m away
		(-20 dB of rolloff, -23 with the pan's) stepped down from -20 dB
		played as a 2D voice 14 dB quieter would, its direct path and its
		reverb alike: exact to -30 dB, -40 at -42.8, -42 at -52.1, silent
		from -44 (its whole level's would be silent from -34). Its direct
		path's are of its direct level too (obstruction and occlusion
		with it), its reverb's not: recorded again (issue 792's eighth
		run), voices 0.5 to 40 m away, ahead, to the right and at 45
		degrees, went silent where this says to within a dB, each ear at
		the same step (the pan after it), and a voice 10 m away with a
		direct level of -10 dB, or obstructed by -10 dB, 10 dB sooner,
		its reverb where the voice's without it went. The HRTF's gain
		too: without it every mono 3D voice on headphones played at full
		volume, whatever audio.volume, audio.effects_volume and the
		game's own gain said, and only the 2D voices followed them */
		float scale = level_direct > 0.0f ?
			voice_level(level_direct) / level_direct * stream->volume * master_volume : 0.0f;
		float room_scale = level_3d > 0.0f ? voice_level(level_3d) / level_3d * stream->volume * master_volume : 0.0f;

		*left *= scale;
		*right *= scale;
		*room *= room_scale;
		if (*hrtf_on)
			hrtf_out->gain *= scale;
	}
}

/* the 5.1 speakers around the listener, clockwise from the center, and
their directions in radians (ITU-R BS.775: fronts at 30 degrees, backs at
110) */
#define SURROUND_RING_SPEAKERS 5
static const unsigned long surround_ring_channel[SURROUND_RING_SPEAKERS] = { 2, 1, 5, 4, 0 };
static const float surround_ring_angle[SURROUND_RING_SPEAKERS + 1] =
{
	0.0f, 0.52359878f, 1.91986218f, 4.36332313f, 5.75958653f, 6.28318531f,
};
/* port (ChupathingyCE's 7.1): the seven around the listener on 7.1, the
sides at 90 degrees and the backs at 150 */
#define SURROUND_RING_71_SPEAKERS 7
static const unsigned long surround_ring_71_channel[SURROUND_RING_71_SPEAKERS] = { 2, 1, 7, 5, 4, 6, 0 };
static const float surround_ring_71_angle[SURROUND_RING_71_SPEAKERS + 1] =
{
	0.0f, 0.52359878f, 1.57079633f, 2.61799388f, 3.66519143f, 4.71238898f, 5.75958653f, 6.28318531f,
};

/* the speaker gains of a sound from a direction, side (right) and ahead:
vector base amplitude panning (Pulkki's VBAP, in the plane) between the two
speakers either side of it, blended (in power) towards all five equally by
spread, 0 to 1. Their power sums to 1, as the stereo pan's does.

VBAP takes the gains g1 and g2 whose sum g1 l1 + g2 l2 of the speakers'
directions l1 and l2 points the sound's way p, then scales them to a power
of 1: g1 = sin(a2 - a) / sin(a2 - a1) and g2 = sin(a - a1) / sin(a2 - a1),
a the sound's angle, a1 and a2 the speakers', the common divisor gone with
the scaling. So a sound at a speaker plays from it alone, one between two
from both, as the tangent law of stereo panning puts a phantom image (the
listener between them hears it where it is), and no pair is wider than 180
degrees (the backs' 140 is the widest). */
static void surround_pan(float side, float ahead, float spread, float *gains)
{
	float angle = atan2f(side, ahead);
	unsigned long segment, speaker;
	/* (port: on 7.1 the ring of seven, ChupathingyCE's 7.1: the ring's names
	below are its tables then) */
	unsigned long ring_speakers = output_channels == 8 ? SURROUND_RING_71_SPEAKERS : SURROUND_RING_SPEAKERS;
	const unsigned long *ring_channel = output_channels == 8 ? surround_ring_71_channel : surround_ring_channel;
	const float *ring_angle = output_channels == 8 ? surround_ring_71_angle : surround_ring_angle;
#undef SURROUND_RING_SPEAKERS
#define SURROUND_RING_SPEAKERS ring_speakers
#define surround_ring_channel ring_channel
#define surround_ring_angle ring_angle

	for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
		gains[speaker] = 0.0f;
	if (angle < 0.0f)
		angle += 6.28318531f;
	for (segment = 0; segment < SURROUND_RING_SPEAKERS - 1; segment++)
	{
		if (angle < surround_ring_angle[segment + 1])
			break;
	}
	{
		float first = sinf(surround_ring_angle[segment + 1] - angle);
		float second = sinf(angle - surround_ring_angle[segment]);
		float length;

		first = first > 0.0f ? first : 0.0f;
		second = second > 0.0f ? second : 0.0f;
		length = sqrtf(first * first + second * second);
		if (length < 1.0e-6f)
		{
			first = 1.0f;
			length = 1.0f;
		}
		gains[surround_ring_channel[segment]] = first / length;
		gains[surround_ring_channel[(segment + 1) % SURROUND_RING_SPEAKERS]] = second / length;
	}
	for (speaker = 0; speaker < SURROUND_RING_SPEAKERS; speaker++)
	{
		float *gain = &gains[surround_ring_channel[speaker]];

		*gain = sqrtf((1.0f - spread) * *gain * *gain + spread / SURROUND_RING_SPEAKERS);
	}
#undef surround_ring_angle
#undef surround_ring_channel
#undef SURROUND_RING_SPEAKERS
#define SURROUND_RING_SPEAKERS 5
}

/* on 5.1, a voice's gains from each source channel to each speaker (the
direct path's; its room send and low passes are voice_gains'):
	- a 3D voice's left and right gains (voice_gains: the pan's, with its
	  volume) on the front pair and the back pair, at their gains (pair,
	  surround_split), a mono voice's both from its one channel, a stereo
	  voice's from its left and right; through the HRTF, its way to each
	  ear (hrtf_process) instead, crossfaded with these as the choice
	  changes;
	- a stereo voice positioned in the world has its channels' middle (the
	  first row) panned towards it in the front half, and their difference
	  (the second) kept on the front pair as wide as the stereo mix keeps it;
	- a 2D voice goes by its mix bins.
On 7.1 (port, ChupathingyCE's) the back pair's share plays from the side and
back pairs alike, each at half its power, as the Xbox's back bins do there;
the positioned stereo voices' ring has the sides too (surround_pan). */
static void voice_surround_gains(const struct sdl_stream *stream, float left, float right, const float *pair,
	float gains[2][MAXIMUM_OUTPUT_CHANNELS])
{
	unsigned long speaker;

	memset(gains, 0, 2 * sizeof(gains[0]));
	if (stream->has_3d && stream->mode != DS3DMODE_DISABLE)
	{
		unsigned long second = stream->channels == 2 ? 1 : 0;

		gains[0][0] = left * pair[0];
		gains[0][4] = left * pair[1];
		gains[second][1] = right * pair[0];
		gains[second][5] = right * pair[1];
#if MAXIMUM_OUTPUT_CHANNELS >= 8
		if (output_channels == 8)
		{
			gains[0][6] = gains[0][4] *= 0.70710678f;
			gains[second][7] = gains[second][5] *= 0.70710678f;
		}
#endif
		return;
	}
	if (stream->channels == 2 && stream->stereo_positioned)
	{
		float pan = stream->stereo_pan < -1.0f ? -1.0f : stream->stereo_pan > 1.0f ? 1.0f : stream->stereo_pan;
		float angle = (pan + 1.0f) * 0.25f * 3.14159265f;
		float direct = gain_from_millibels(stream->direct);
		float far_gain = (cosf(angle) < sinf(angle) ? cosf(angle) : sinf(angle)) * 1.41421356f;

		/* the middle at the stereo pan's level (1 on each side when centred,
		a power of 2) */
		surround_pan(sinf(pan * 1.57079633f), cosf(pan * 1.57079633f), 0.1f, gains[0]);
		for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
			gains[0][speaker] *= 1.41421356f * stream->mix_left * direct;
		gains[1][0] = far_gain * stream->mix_left * direct;
		gains[1][1] = -far_gain * stream->mix_right * direct;
	}
	else
	{
		memcpy(gains, stream->surround_bins, 2 * sizeof(gains[0]));
		if (stream->channels == 1)
			memset(gains[1], 0, sizeof(gains[1]));
#if MAXIMUM_OUTPUT_CHANNELS >= 8
		if (output_channels == 8)
		{
			int row;

			for (row = 0; row < 2; row++)
			{
				gains[row][6] = gains[row][4] *= 0.70710678f;
				gains[row][7] = gains[row][5] *= 0.70710678f;
			}
		}
#endif
	}
	/* (voice_level, as voice_gains) */
	for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
	{
		gains[0][speaker] = voice_level(gains[0][speaker] * stream->volume) * master_volume;
		gains[1][speaker] = voice_level(gains[1][speaker] * stream->volume) * master_volume;
	}
}

/* ---------- binaural rendering

Port (ChupathingyCE's): audio.hrtf = "on" (or "auto", on an output whose name says it is
headphones) renders 3D voices for headphones on stereo output: each one is
heard through the head-related impulse responses of its direction, the
filtering of the head and the outer ears, and the far ear hears it later,
by the interaural delay, so that a sound is placed all around the listener,
above and behind too, instead of between the ears. The Xbox's DirectSound
filtered 3D voices with HRTFs of its own (the game asks for its full
quality ones, DirectSoundUseFullHRTF); these are measured ones, the SADIE II
database's KEMAR, diffuse-field equalized, so that they place a sound
without changing its tone overall (port/assets/hrtf, tools/hrtf_assets.py).

The responses are a grid of directions, each ear's made minimum phase and
HRTF taps long, its delay apart: a voice's filter is the four directions
around it blended by how near each is, and its delays blended the same way,
which keeps the blend free of the comb filtering of mixing two delays. Each
mix the filter and the delays move to the voice's direction: the output
crossfades from the old filter's to the new one's over the mix, and each
ear's delay slides to its new length, so a moving sound moves smoothly,
without clicks. Inside its minimum distance a voice blends towards the
stereo pan's centered sound (the player's own footsteps and weapons stay in
the head, as the stereo pan keeps them), and turning audio.hrtf on or off
fades over HRTF_FADE_SECONDS. Its level is the stereo pan's: both ears
together have a power of 1 over all directions. 2D voices, the reverb and
surround output are as they were; audio.hrtf "xbox" is the measured Xbox
HRTF instead (dsound_xbox_hrtf.inl). */

extern const unsigned int dsound_hrtf_data[];
extern const unsigned long dsound_hrtf_data_size;

static struct
{
	BOOL loaded;
	unsigned long taps, elevations, azimuths;
	/* radians */
	float first_elevation, elevation_step, azimuth_step;
	/* for each direction (elevation by elevation, azimuth counterclockwise
	from ahead), each ear's taps, last first (hrtf_convolve), and its delay in
	samples; taps is the file's rounded up to a multiple of 4, the first
	ones 0 */
	float *filters;
	float *delays;
} binaural;

static unsigned long hrtf_read_u32(const unsigned char *data)
{
	return (unsigned long)data[0] | ((unsigned long)data[1] << 8) | ((unsigned long)data[2] << 16) |
		((unsigned long)data[3] << 24);
}

static float hrtf_read_float(const unsigned char *data)
{
	unsigned int bits = (unsigned int)hrtf_read_u32(data);
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static int hrtf_read_s16(const unsigned char *data)
{
	return (short)(data[0] | (data[1] << 8));
}

/* the embedded responses (the file tools/hrtf_assets.py describes); FALSE
without them */
static BOOL hrtf_load(void)
{
	const unsigned char *data = (const unsigned char *)dsound_hrtf_data;
	unsigned long size = (unsigned long)dsound_hrtf_data_size, count, index, ear, tap, record, file_taps;
	float scale;

	if (binaural.loaded)
		return TRUE;
	if (size < 40 || memcmp(data, "HRTF", 4) || hrtf_read_u32(data + 4) != 1 ||
		hrtf_read_u32(data + 8) != OUTPUT_RATE)
	{
		return FALSE;
	}
	file_taps = hrtf_read_u32(data + 12);
	binaural.taps = (file_taps + 3) & ~3UL;
	binaural.elevations = hrtf_read_u32(data + 16);
	binaural.first_elevation = hrtf_read_float(data + 20) * 0.017453293f;
	binaural.elevation_step = hrtf_read_float(data + 24) * 0.017453293f;
	binaural.azimuths = hrtf_read_u32(data + 28);
	binaural.azimuth_step = hrtf_read_float(data + 32) * 0.017453293f;
	scale = hrtf_read_float(data + 36);
	count = binaural.elevations * binaural.azimuths;
	record = 4 + 4 * file_taps;
	if (!file_taps || binaural.taps > HRTF_MAXIMUM_TAPS || !count || binaural.elevation_step <= 0.0f ||
		binaural.azimuth_step <= 0.0f || size < 40 + count * record)
	{
		return FALSE;
	}
	binaural.filters = calloc(count * 2 * binaural.taps, sizeof(float));
	binaural.delays = malloc(count * 2 * sizeof(float));
	if (!binaural.filters || !binaural.delays)
	{
		free(binaural.filters);
		free(binaural.delays);
		return FALSE;
	}
	for (index = 0; index < count; index++)
	{
		const unsigned char *entry = data + 40 + index * record;

		for (ear = 0; ear < 2; ear++)
		{
			float delay = (float)hrtf_read_s16(entry + 2 * ear) / 256.0f;

			binaural.delays[index * 2 + ear] = delay < 0.0f ? 0.0f : delay > HRTF_MAXIMUM_DELAY ? HRTF_MAXIMUM_DELAY : delay;
			for (tap = 0; tap < file_taps; tap++)
			{
				binaural.filters[(index * 2 + ear) * binaural.taps + binaural.taps - 1 - tap] =
					(float)hrtf_read_s16(entry + 4 + 2 * (ear * file_taps + tap)) * scale;
			}
		}
	}
	binaural.loaded = TRUE;
	return TRUE;
}

/* the filters and delays of a voice from a direction (side to the right, up,
ahead): the four grid directions around it, blended */
static void hrtf_direction(float side, float up, float ahead, float filters[2][HRTF_MAXIMUM_TAPS], float *delays)
{
	float horizontal = sqrtf(side * side + ahead * ahead);
	float height = up, azimuth = atan2f(-side, ahead), elevation = atan2f(height, horizontal);
	float ring, step, ring_fraction, step_fraction;
	unsigned long rings[2], steps[2], ear, tap;
	int corner;

	if (azimuth < 0.0f)
		azimuth += 6.28318531f;
	ring = (elevation - binaural.first_elevation) / binaural.elevation_step;
	if (ring < 0.0f)
		ring = 0.0f;
	if (ring > (float)(binaural.elevations - 1))
		ring = (float)(binaural.elevations - 1);
	rings[0] = (unsigned long)ring;
	rings[1] = rings[0] + 1 < binaural.elevations ? rings[0] + 1 : rings[0];
	ring_fraction = ring - (float)rings[0];
	step = azimuth / binaural.azimuth_step;
	steps[0] = (unsigned long)step % binaural.azimuths;
	steps[1] = (steps[0] + 1) % binaural.azimuths;
	step_fraction = step - floorf(step);
	memset(filters, 0, 2 * sizeof(filters[0]));
	delays[0] = delays[1] = 0.0f;
	for (corner = 0; corner < 4; corner++)
	{
		unsigned long index = rings[corner >> 1] * binaural.azimuths + steps[corner & 1];
		float weight = ((corner >> 1) ? ring_fraction : 1.0f - ring_fraction) *
			((corner & 1) ? step_fraction : 1.0f - step_fraction);

		if (weight <= 0.0f)
			continue;
		for (ear = 0; ear < 2; ear++)
		{
			const float *source = binaural.filters + (index * 2 + ear) * binaural.taps;

			for (tap = 0; tap < binaural.taps; tap++)
				filters[ear][tap] += source[tap] * weight;
			delays[ear] += binaural.delays[index * 2 + ear] * weight;
		}
	}
}

/* how much of a 3D voice the binaural rendering takes (0 to 1), the rest
the stereo pan's: none on surround output or with audio.hrtf off, and the
fade between while it is turned on or off */
static float hrtf_share(const struct sdl_stream *stream)
{
	if (hrtf_level <= 0.0f || output_channels != 2 || !stream->has_3d || stream->mode == DS3DMODE_DISABLE)
		return 0.0f;
	return hrtf_level;
}

/* a voice's target for this mix: its filters and delays, and its gain (the
direct path's, as spatialize's but for the pan). Inside its minimum
distance the filters blend towards the centered sound (each ear the voice
as it is, at the stereo pan's level) and the delays towards none: the
filters are minimum phase, their energy at their start, so the blend does
not cancel itself as mixing the two outputs would */
static float hrtf_target(const struct sdl_stream *stream, float share, float filters[2][HRTF_MAXIMUM_TAPS],
	float *delays)
{
	float side, up, ahead, distance;

	voice_direction(stream, &side, &up, &ahead);
	distance = sqrtf(side * side + up * up + ahead * ahead);
	hrtf_direction(side, up, ahead, filters, delays);
	if (stream->minimum_distance > 0.0f && distance < stream->minimum_distance)
	{
		float near = distance / stream->minimum_distance;
		unsigned long ear, tap;

		for (ear = 0; ear < 2; ear++)
		{
			for (tap = 0; tap < binaural.taps; tap++)
				filters[ear][tap] *= near;
			filters[ear][binaural.taps - 1] += (1.0f - near) * 0.70710678f;
			delays[ear] *= near;
		}
	}
	/* (the voice's level after the Xbox's quietest steps, as voice_gains) */
	{
		float attenuation = distance_attenuation(stream, distance, listener.rolloff_factor);
		float direct = gain_from_millibels(stream->direct) * voice_cone_gain(stream);
		float level = VOICE_LEVEL_3D * attenuation * stream->volume * direct;

		return level > 0.0f ? share * attenuation * direct * voice_level(level) / level * stream->volume * master_volume :
			0.0f;
	}
}

/* a voice starting (over), or taken up by the rendering again: silence
behind it */
static void hrtf_reset(struct sdl_stream *stream)
{
	memset(stream->hrtf_input, 0, sizeof(stream->hrtf_input));
	memset(stream->hrtf_tail, 0, sizeof(stream->hrtf_tail));
	stream->hrtf_position = 0;
	stream->current_hrtf = 0.0f;
	stream->hrtf_valid = FALSE;
}

/* the sample of input delay + 1 samples before position (a cubic Hermite
interpolation between the four around it). The delay is never negative, so
a conversion to int is its floor: floorf is a call into the C library on
32-bit x86, which made this five times slower there */
static float hrtf_delayed(const float *input, unsigned long position, float delay)
{
	/* (a sample more than the delay, so the newest of the four is one
	written already) */
	int whole = (int)delay;
	float fraction = delay - (float)whole;
	unsigned long newest = position - 1 - (unsigned long)whole;
	float y0 = input[(newest + 1) & (HRTF_INPUT - 1)];
	float y1 = input[newest & (HRTF_INPUT - 1)];
	float y2 = input[(newest - 1) & (HRTF_INPUT - 1)];
	float y3 = input[(newest - 2) & (HRTF_INPUT - 1)];
	float c1 = 0.5f * (y2 - y0);
	float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
	float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

	return ((c3 * fraction + c2) * fraction + c1) * fraction + y1;
}

/* adds to sums (frames of them) the filter (taps of it, last first) run
over samples (taps - 1 before the first frame's, then the frames'): tap by
tap over all the frames, which vectorizes, each tap a constant */
static void hrtf_convolve(const float *filter, const float *samples, float *sums, unsigned long frames,
	unsigned long taps)
{
	unsigned long tap, frame;

	for (tap = 0; tap < taps; tap++)
	{
		float weight = filter[tap];
		const float *source = samples + tap;

		for (frame = 0; frame < frames; frame++)
			sums[frame] += weight * source[frame];
	}
}

/* the binaural rendering of a voice's mono direct path, mono (frames of it,
at most MIX_CHUNK_FRAMES), added to output (frames of stereo); its targets
for the mix computed by hrtf_target. The mix's input goes in first; then
for each ear its delayed copy, the delay sliding to the new one, and that
filtered a block at a time */
static void hrtf_binaural_mix(struct sdl_stream *stream, const float *mono, float *output, unsigned long frames,
	float target_gain, float filters[2][HRTF_MAXIMUM_TAPS], const float *delays)
{
	unsigned long taps = binaural.taps, position = stream->hrtf_position, frame, ear;
	BOOL fading;
	float gain, ramp_gain;
	/* an ear's samples: the taps - 1 before the mix, then the mix's; and
	the old filter's sums and the new one's */
	float samples[HRTF_MAXIMUM_TAPS + MIX_CHUNK_FRAMES], old_sums[MIX_CHUNK_FRAMES], new_sums[MIX_CHUNK_FRAMES];

	if (!stream->hrtf_valid)
	{
		memcpy(stream->hrtf_filters, filters, sizeof(stream->hrtf_filters));
		stream->hrtf_delays[0] = delays[0];
		stream->hrtf_delays[1] = delays[1];
		stream->hrtf_valid = TRUE;
	}
	fading = memcmp(stream->hrtf_filters, filters, sizeof(stream->hrtf_filters)) != 0;
	gain = stream->current_hrtf;
	ramp_gain = (target_gain - gain) / (float)frames;
	for (frame = 0; frame < frames; frame++)
	{
		stream->hrtf_input[(position + frame) & (HRTF_INPUT - 1)] = mono[frame] * gain;
		gain += ramp_gain;
	}
	for (ear = 0; ear < 2; ear++)
	{
		float delay = stream->hrtf_delays[ear], ramp_delay = (delays[ear] - delay) / (float)frames;
		float *delayed = samples + taps - 1;

		memcpy(samples, stream->hrtf_tail[ear], (taps - 1) * sizeof(float));
		for (frame = 0; frame < frames; frame++)
		{
			delayed[frame] = hrtf_delayed(stream->hrtf_input, position + frame, delay);
			delay += ramp_delay;
		}
		memset(old_sums, 0, frames * sizeof(float));
		hrtf_convolve(stream->hrtf_filters[ear], samples, old_sums, frames, taps);
		if (fading)
		{
			float fade = 0.0f, ramp_fade = 1.0f / (float)frames;

			memset(new_sums, 0, frames * sizeof(float));
			hrtf_convolve(filters[ear], samples, new_sums, frames, taps);
			for (frame = 0; frame < frames; frame++)
			{
				old_sums[frame] += (new_sums[frame] - old_sums[frame]) * fade;
				fade += ramp_fade;
			}
		}
		for (frame = 0; frame < frames; frame++)
			output[frame * 2 + ear] += old_sums[frame];
		memcpy(stream->hrtf_tail[ear], samples + frames, (taps - 1) * sizeof(float));
	}
	stream->hrtf_position = position + frames;
	memcpy(stream->hrtf_filters, filters, sizeof(stream->hrtf_filters));
	stream->hrtf_delays[0] = delays[0];
	stream->hrtf_delays[1] = delays[1];
	stream->current_hrtf = target_gain;
}

/* ---------- resampling

Each voice is resampled to the output rate by band-limited interpolation (J.
O. Smith's): an output sample is the source frames around its moment, each
weighted by a windowed sinc low pass centred there. The low pass keeps
RESAMPLER_CUTOFF of the source's band (a 22 kHz voice is 0.5 dB down at 10
kHz) and takes the images of it out (80 dB down), but for those of its last
few hundred hertz, which lie beside them, about 20 dB down. Linear
interpolation, which the mixer did before, left the images only 8 to 20 dB
down, a gritty haze above 11 kHz over every 22 kHz voice. A voice
played faster than the output rate takes its frames (a step over 1) gets the
low pass narrowed to match, up to RESAMPLER_MAXIMUM_STRETCH times, so it
does not alias. The frames come from the voice's packets in turn, so the low
pass reads straight across a packet's end into the next. */

/* the low pass's one side, RESAMPLER_TABLE_STEPS values a source frame */
static float resampler_table[RESAMPLER_ZERO_CROSSINGS * RESAMPLER_TABLE_STEPS + 2];
/* the same low pass as the weights of a voice's taps 1 - RESAMPLER_ZERO_CROSSINGS
to RESAMPLER_ZERO_CROSSINGS, at each of RESAMPLER_TABLE_STEPS phases between two
source frames (and one row more, for the last's blend): a voice at the output
rate or slower blends two rows by its phase, which is what the table gave tap
by tap, a third of the work. resampler_deltas holds each row's step to the
next, so the blend is a multiply and an add a tap */
#define RESAMPLER_TAPS (2 * RESAMPLER_ZERO_CROSSINGS)
static float resampler_phases[RESAMPLER_TABLE_STEPS + 1][RESAMPLER_TAPS] RESAMPLER_ALIGNED;
static float resampler_deltas[RESAMPLER_TABLE_STEPS][RESAMPLER_TAPS] RESAMPLER_ALIGNED;
/* the Xbox's resampler, as a voice's reverb send hears it: a sinc low pass at
the source's own Nyquist rate, Hann windowed to SEND_ZERO_CROSSINGS frames
each side (six taps), and never narrowed for a voice played faster than the
output rate, so that it lets through about half of the source's top few
kilohertz's images (-6 dB at the source's Nyquist rate) and aliases what a
voice pitched up has above the output's. The reverb takes every other frame
of the send unfiltered (reverb_process), which folds the send's top octave
onto its bottom one: so a 44.1 kHz voice's images at 22 to 24 kHz are the
reverb's low end. Recorded on an Xbox (OpenH1 issue 714, its sixth run:
clicks and noise in the cave at 22.05, 44.1 and 48 kHz, at pitches 0.75 to
1.5), a 44.1 kHz click's reverberation is 3.6 dB louder below 1 kHz at pitch
1 than through the mixer's resampler, 6 dB pitched up 1.5 times, and a 48
kHz one's 1 dB at 0.75; this low pass gives each within 0.5 dB (a width of
three frames each side fits best, of two to eight, and a cutoff at the
source's Nyquist rate, of 0.9 to 1.1 of it). At a phase of 0 it is the
frames themselves, so a voice at the output's rate sends its frames as they
are, as the Xbox's did. The direct path has a longer one (direct_phases). A row
a phase, and its step to the next, as resampler_phases and resampler_deltas */
static float send_phases[RESAMPLER_TABLE_STEPS + 1][SEND_TAPS] RESAMPLER_ALIGNED;
static float send_deltas[RESAMPLER_TABLE_STEPS][SEND_TAPS] RESAMPLER_ALIGNED;

/* the Xbox's resampler on a voice's direct path, audio.resampler's "xbox"
(the default): a low pass at the source's Nyquist rate, never narrowed, over
four source frames each side, as recorded on an Xbox (OpenH1 issue 792, its
eighth run: two trains of sixteen one-sample clicks at 22.05 kHz, pitched 1
and 0.9, each click on another phase, the clicks' frames laid on one time
line by the rate the Xbox played them at). In each source frame from the
center it is a polynomial of the distance into the frame, as here: these
play the recordings back to within -70 dB, with corners at the whole frames,
where the best smooth low pass's is -35 dB. With it the Xbox's 22.05 kHz
voices are played as the Xbox played them: the passband (-2.2 dB at 0.4 of
the source's rate, -6.9 at 0.5; the Kaiser windowed sinc fitted before, -1.9
and -6.0), the images a 4 kHz tone leaves at 0.82, 1.18 and 1.82 of the
source's rate (-63, -71 and -79 dB: the sinc's -70, -99, -101) and the low
pass's lobe from 1.3 to 1.6 of it, at -33 dB, which comes down to the
output's 13 to 19 kHz (issue 714's seventh run: a 22.05 kHz click's images
above 16 kHz, 9 to 40 dB over any smooth low pass's). It is the
coefficients, not their phases: tables of 16 to 64 phases, unblended, fit
the clicks worse than one blended. The Xbox's step a frame is rounded too
(22050 Hz played at 22042.97, 17640 at 17636.72, 24000 and 12000 exactly),
by under 0.6 cents, which is not copied. A row a frame from the center, its
coefficients from the constant up */
static const double direct_kernel[DIRECT_ZERO_CROSSINGS][DIRECT_KERNEL_ORDER + 1] =
{
	{ 1.0007795, -0.2495059, -1.3652166, 0.3404684, 0.4084059, -0.1349805 },
	{ -0.0001219, -0.6004896, 0.7474932, 0.0843083, -0.3094547, 0.0781494 },
	{ -0.0001317, 0.1000168, -0.0728184, -0.1081077, 0.1009553, -0.0199766 },
	{ -0.0001926, -0.0076839, -0.0064829, 0.0419929, -0.0412200, 0.0134312 },
};
/* direct_kernel as a table of phases: a row a phase, and its step to the
next, as resampler_phases */
static float direct_phases[RESAMPLER_TABLE_STEPS + 1][DIRECT_TAPS] RESAMPLER_ALIGNED;
static float direct_deltas[RESAMPLER_TABLE_STEPS][DIRECT_TAPS] RESAMPLER_ALIGNED;

/* how a voice's frames are converted to the output's rate: the Xbox's low
pass (direct_phases), the mixer's windowed sinc low pass, or between the two
frames either side (linear: it lets more of the images above the source's
rate through, which some hear as brighter) */
enum resampler_mode
{
	RESAMPLER_XBOX,
	RESAMPLER_SINC,
	RESAMPLER_LINEAR
};
/* audio.resampler */
static enum resampler_mode resampler_mode = RESAMPLER_XBOX;

/* audio.resampler's mode: the Xbox's for any value but "sinc" and "linear" */
static enum resampler_mode resampler_mode_read(void)
{
	const char *setting = config_string("audio.resampler");
	/* (port, this fork: ChupathingyCE's audio.resampling = "linear" too, the
	same choice under its name, a config file's that has it; Audio Setup's
	Sound Processing screen sets audio.resampler) */
	const char *chupathingy = config_string("audio.resampling");

	if (chupathingy && !strcmp(chupathingy, "linear"))
		return RESAMPLER_LINEAR;
	if (setting && !strcmp(setting, "sinc"))
		return RESAMPLER_SINC;
	return setting && !strcmp(setting, "linear") ? RESAMPLER_LINEAR : RESAMPLER_XBOX;
}

static double bessel_i0(double x)
{
	double sum = 1.0, term = 1.0;
	int k;

	for (k = 1; k < 64 && term > 1.0e-12 * sum; k++)
	{
		term *= (x / (2.0 * k)) * (x / (2.0 * k));
		sum += term;
	}
	return sum;
}

static void resampler_initialize(void)
{
	unsigned long index;

	for (index = 0; index <= RESAMPLER_ZERO_CROSSINGS * RESAMPLER_TABLE_STEPS; index++)
	{
		double distance = (double)index / RESAMPLER_TABLE_STEPS;
		double edge = distance / RESAMPLER_ZERO_CROSSINGS;
		double angle = 3.14159265358979 * RESAMPLER_CUTOFF * distance;
		double sinc = index ? sin(angle) / angle : 1.0;
		double window = bessel_i0(RESAMPLER_KAISER_BETA * sqrt(1.0 - edge * edge)) / bessel_i0(RESAMPLER_KAISER_BETA);

		resampler_table[index] = (float)(RESAMPLER_CUTOFF * sinc * window);
	}
	resampler_table[RESAMPLER_ZERO_CROSSINGS * RESAMPLER_TABLE_STEPS + 1] = 0.0f;
}

/* the low pass, distance source frames from its centre times
RESAMPLER_TABLE_STEPS */
static float resampler_weight(float distance)
{
	unsigned long index = (unsigned long)distance;
	float fraction;

	if (index >= RESAMPLER_ZERO_CROSSINGS * RESAMPLER_TABLE_STEPS)
		return 0.0f;
	fraction = distance - (float)index;
	return resampler_table[index] + (resampler_table[index + 1] - resampler_table[index]) * fraction;
}

static void resampler_phases_initialize(void)
{
	unsigned long phase, tap;

	for (phase = 0; phase <= RESAMPLER_TABLE_STEPS; phase++)
	{
		for (tap = 0; tap < 2 * RESAMPLER_ZERO_CROSSINGS; tap++)
		{
			float distance = fabsf((float)((long)tap + 1 - RESAMPLER_ZERO_CROSSINGS) -
				(float)phase / RESAMPLER_TABLE_STEPS);

			resampler_phases[phase][tap] = resampler_weight(distance * RESAMPLER_TABLE_STEPS);
		}
	}
	for (phase = 0; phase < RESAMPLER_TABLE_STEPS; phase++)
	{
		for (tap = 0; tap < RESAMPLER_TAPS; tap++)
			resampler_deltas[phase][tap] = resampler_phases[phase + 1][tap] - resampler_phases[phase][tap];
	}
	for (phase = 0; phase < RESAMPLER_TABLE_STEPS + 1; phase++)
	{
		for (tap = 0; tap < SEND_TAPS; tap++)
		{
			double distance = (double)((long)tap + 1 - SEND_ZERO_CROSSINGS) - (double)phase / RESAMPLER_TABLE_STEPS;
			double angle = 3.14159265358979 * distance;

			/* (on a frame, exactly it, and 0 at the others) */
			if (phase % RESAMPLER_TABLE_STEPS == 0)
				send_phases[phase][tap] = distance == 0.0 ? 1.0f : 0.0f;
			else if (fabs(distance) >= SEND_ZERO_CROSSINGS)
				send_phases[phase][tap] = 0.0f;
			else
			{
				send_phases[phase][tap] = (float)(sin(angle) / angle *
					(0.5 + 0.5 * cos(angle / SEND_ZERO_CROSSINGS)));
			}
		}
	}
	for (phase = 0; phase < RESAMPLER_TABLE_STEPS; phase++)
	{
		for (tap = 0; tap < SEND_TAPS; tap++)
			send_deltas[phase][tap] = send_phases[phase + 1][tap] - send_phases[phase][tap];
	}
	for (phase = 0; phase < RESAMPLER_TABLE_STEPS + 1; phase++)
	{
		for (tap = 0; tap < DIRECT_TAPS; tap++)
		{
			double distance = fabs((double)((long)tap + 1 - DIRECT_ZERO_CROSSINGS) -
				(double)phase / RESAMPLER_TABLE_STEPS);
			unsigned long frame = (unsigned long)distance;

			/* (on a frame, exactly it, and 0 at the others) */
			if (phase % RESAMPLER_TABLE_STEPS == 0)
				direct_phases[phase][tap] = distance == 0.0 ? 1.0f : 0.0f;
			else if (frame >= DIRECT_ZERO_CROSSINGS)
				direct_phases[phase][tap] = 0.0f;
			else
			{
				double into = distance - (double)frame, weight = 0.0;
				int power;

				for (power = DIRECT_KERNEL_ORDER; power >= 0; power--)
					weight = weight * into + direct_kernel[frame][power];
				direct_phases[phase][tap] = (float)weight;
			}
		}
	}
	for (phase = 0; phase < RESAMPLER_TABLE_STEPS; phase++)
	{
		for (tap = 0; tap < DIRECT_TAPS; tap++)
			direct_deltas[phase][tap] = direct_phases[phase + 1][tap] - direct_phases[phase][tap];
	}
}

/* The low pass over a voice's frames, the sum of each frame times its weight:
four taps at a time where the compiler has vectors (SSE, NEON), summed in four
lanes, then the lanes together. Only the order of the additions differs from
tap by tap, a difference of the order of float's rounding (-115 dB or less);
RESAMPLER_SCALAR adds them tap by tap (tools/harness/tests/test_mixer.py). */
#if (defined(__GNUC__) || defined(__clang__)) && !defined(RESAMPLER_SCALAR)
typedef float resampler_vector __attribute__((vector_size(16)));
typedef float resampler_unaligned_vector __attribute__((vector_size(16), aligned(4)));
/* (each row of the phase tables whole vectors, aligned) */
typedef char resampler_taps_in_fours[RESAMPLER_TAPS % 4 ? -1 : 1];
#define RESAMPLER_LANES 4
#else
#define RESAMPLER_LANES 1
#endif

/* frames[0, RESAMPLER_TAPS) weighted by the phase table's row blended blend of
the way to the next */
static float resampler_dot(const float *frames, const float *weights, const float *deltas, float blend)
{
#if RESAMPLER_LANES == 4
	resampler_vector sum = { 0.0f, 0.0f, 0.0f, 0.0f }, blends = { blend, blend, blend, blend };
	int tap;

	for (tap = 0; tap < RESAMPLER_TAPS; tap += 4)
	{
		resampler_vector weight = *(const resampler_vector *)(weights + tap) +
			*(const resampler_vector *)(deltas + tap) * blends;

		sum += *(const resampler_unaligned_vector *)(frames + tap) * weight;
	}
	return (sum[0] + sum[1]) + (sum[2] + sum[3]);
#else
	float sum = 0.0f;
	int tap;

	for (tap = 0; tap < RESAMPLER_TAPS; tap++)
		sum += frames[tap] * (weights[tap] + deltas[tap] * blend);
	return sum;
#endif
}

/* frames[0, DIRECT_TAPS) weighted by a row of direct_phases, as resampler_dot */
static float direct_dot(const float *frames, const float *weights, const float *deltas, float blend)
{
#if RESAMPLER_LANES == 4
	resampler_vector sum = { 0.0f, 0.0f, 0.0f, 0.0f }, blends = { blend, blend, blend, blend };
	int tap;

	for (tap = 0; tap < DIRECT_TAPS; tap += 4)
	{
		resampler_vector weight = *(const resampler_vector *)(weights + tap) +
			*(const resampler_vector *)(deltas + tap) * blends;

		sum += *(const resampler_unaligned_vector *)(frames + tap) * weight;
	}
	return (sum[0] + sum[1]) + (sum[2] + sum[3]);
#else
	float sum = 0.0f;
	int tap;

	for (tap = 0; tap < DIRECT_TAPS; tap++)
		sum += frames[tap] * (weights[tap] + deltas[tap] * blend);
	return sum;
#endif
}

/* resampler_dot of two channels' frames, with the same weights */
static void resampler_dot2(const float *left_frames, const float *right_frames, const float *weights,
	const float *deltas, float blend, float *left, float *right)
{
#if RESAMPLER_LANES == 4
	resampler_vector left_sum = { 0.0f, 0.0f, 0.0f, 0.0f }, right_sum = left_sum;
	resampler_vector blends = { blend, blend, blend, blend };
	int tap;

	for (tap = 0; tap < RESAMPLER_TAPS; tap += 4)
	{
		resampler_vector weight = *(const resampler_vector *)(weights + tap) +
			*(const resampler_vector *)(deltas + tap) * blends;

		left_sum += *(const resampler_unaligned_vector *)(left_frames + tap) * weight;
		right_sum += *(const resampler_unaligned_vector *)(right_frames + tap) * weight;
	}
	*left = (left_sum[0] + left_sum[1]) + (left_sum[2] + left_sum[3]);
	*right = (right_sum[0] + right_sum[1]) + (right_sum[2] + right_sum[3]);
#else
	*left = resampler_dot(left_frames, weights, deltas, blend);
	*right = resampler_dot(right_frames, weights, deltas, blend);
#endif
}

/* the voice's frame at its phase, its channels' middle, through the Xbox's
resampler (send_phases), for the reverb's send: as resampler_dot, over
SEND_TAPS */
static float send_frame(const struct sdl_stream *stream)
{
	double position = stream->phase * RESAMPLER_TABLE_STEPS;
	unsigned long row = (unsigned long)position;
	float blend = (float)(position - (double)row);
	unsigned long first = (stream->center + 1 - SEND_ZERO_CROSSINGS) % RESAMPLER_HISTORY;
	const float *left = stream->history[0] + first, *right = stream->history[1] + first;
	const float *weights = send_phases[row], *deltas = send_deltas[row];
	int tap;
#if RESAMPLER_LANES == 4
	resampler_vector sum = { 0.0f, 0.0f, 0.0f, 0.0f }, blends = { blend, blend, blend, blend };

	for (tap = 0; tap < SEND_TAPS; tap += 4)
	{
		resampler_vector weight = *(const resampler_vector *)(weights + tap) +
			*(const resampler_vector *)(deltas + tap) * blends;
		resampler_vector frames = *(const resampler_unaligned_vector *)(left + tap);

		if (stream->channels == 2)
			frames = (frames + *(const resampler_unaligned_vector *)(right + tap)) * 0.5f;
		sum += frames * weight;
	}
	return (sum[0] + sum[1]) + (sum[2] + sum[3]);
#else
	float sum = 0.0f;

	for (tap = 0; tap < SEND_TAPS; tap++)
	{
		float frame = stream->channels == 2 ? 0.5f * (left[tap] + right[tap]) : left[tap];

		sum += frame * (weights[tap] + deltas[tap] * blend);
	}
	return sum;
#endif
}

/* a voice starting (over): silence before its first frame, which the output
starts at */
static void resampler_reset(struct sdl_stream *stream)
{
	memset(stream->history, 0, sizeof(stream->history));
	stream->history_count = RESAMPLER_ZERO_CROSSINGS * RESAMPLER_MAXIMUM_STRETCH;
	stream->center = stream->history_count;
	stream->phase = 0.0;
	stream->silence = 0;
}

/* how much the low pass is narrowed for a voice taking frames step at a time:
for one faster than the output rate, to keep what it takes above the output's
band out (up to RESAMPLER_MAXIMUM_STRETCH times) */
static float resampler_scale(double step)
{
	return step > 1.0 ? (float)(1.0 / (step < RESAMPLER_MAXIMUM_STRETCH ? step : RESAMPLER_MAXIMUM_STRETCH)) : 1.0f;
}

static float packet_sample(const struct voice_packet *packet, unsigned long frame, unsigned long channel,
	unsigned long channels)
{
	return packet->samples[frame * channels + channel] * (1.0f / 32768.0f);
}

/* the voice's next frame, finishing the packets it passes; FALSE once they
run out */
static BOOL take_frame(struct sdl_stream *stream, float *frame)
{
	for (;;)
	{
		struct voice_packet *packet = NULL;
		unsigned long position;

		for (position = 0; position < stream->packet_count; position++)
		{
			struct voice_packet *candidate = &stream->packets[(stream->packet_head + position) % MAXIMUM_STREAM_PACKETS];

			if (!candidate->finished)
			{
				packet = candidate;
				break;
			}
		}
		if (!packet)
			return FALSE;
		if (stream->cursor < packet->frames)
		{
			frame[0] = packet_sample(packet, stream->cursor, 0, stream->channels);
			frame[1] = packet_sample(packet, stream->cursor, stream->channels - 1, stream->channels);
			stream->cursor++;
			return TRUE;
		}
		stream->cursor = 0;
		packet->finished = TRUE;
	}
}

/* whether the voice has a frame to take; the packets with none left are
marked finished on the way, as take_frame marks them */
static BOOL voice_has_frames(struct sdl_stream *stream)
{
	unsigned long position;

	for (position = 0; position < stream->packet_count; position++)
	{
		struct voice_packet *packet = &stream->packets[(stream->packet_head + position) % MAXIMUM_STREAM_PACKETS];

		if (packet->finished)
			continue;
		if (stream->cursor < packet->frames)
			return TRUE;
		stream->cursor = 0;
		packet->finished = TRUE;
	}
	return FALSE;
}

/* ---------- mixing */

/* mixes one voice into output (frames of output_channels floats), and into
the reverb's send (frames of mono) */
static void mix_voice(struct sdl_stream *stream, float *output, float *send, unsigned long frames)
{
	double step, target_step, ramp_step;
	float target_left, target_right, target_room, left, right, room, ramp_left, ramp_right, ramp_room, scale;
	float target_direct_lowpass, target_room_lowpass, direct_lowpass, room_lowpass;
	float ramp_direct_lowpass, ramp_room_lowpass, share;
	float direct_state[2], room_state;
	long width;
	unsigned long frame, ramp_frames;
	/* (a stereo voice panned towards its sound: voice_gains) */
	BOOL positioned = stream->channels == 2 && stream->stereo_positioned;
	/* (through the HRTF: whether it may take the voice, and whether its
	filters run in this mix; how much of the voice they play, at the start,
	the end and a frame's step; its parameters, and the direct path the
	HRTF takes, hrtf_process) */
	BOOL hrtf_on, hrtf_running;
	float hrtf_mix, hrtf_mix_end, ramp_hrtf_mix, hrtf_mix_start;
	struct hrtf_parameters target_hrtf;
	float hrtf_direct[MIX_CHUNK_FRAMES];
	/* port (ChupathingyCE's binaural rendering, on stereo with audio.hrtf
	"on" or "auto"): its share of a 3D voice, its targets, the voice's mono
	direct path, and whether it runs (binaural) */
	float binaural_share, target_binaural = 0.0f, binaural_filters[2][HRTF_MAXIMUM_TAPS], binaural_delays[2];
	float binaural_mono[MIX_CHUNK_FRAMES];
	BOOL binaural_on;
	/* on surround output, the speakers' gains (voice_surround_gains) */
	BOOL surround = output_channels > 2, surround_heard = FALSE;
	float target_surround[2][MAXIMUM_OUTPUT_CHANNELS], surround_gains[2][MAXIMUM_OUTPUT_CHANNELS];
	float ramp_surround[2][MAXIMUM_OUTPUT_CHANNELS], pair[2];
	unsigned long speaker;

	if (stream->paused || stream->flushing || !stream->packet_count || !stream->sample_rate)
		return;
	/* (at most MIX_CHUNK_FRAMES at a time: the HRTF's direct path) */
	if (frames > MIX_CHUNK_FRAMES)
	{
		mix_voice(stream, output, send, MIX_CHUNK_FRAMES);
		mix_voice(stream, output + MIX_CHUNK_FRAMES * output_channels, send + MIX_CHUNK_FRAMES,
			frames - MIX_CHUNK_FRAMES);
		return;
	}
	target_step = (double)(stream->frequency ? stream->frequency : stream->sample_rate) / OUTPUT_RATE *
		doppler_heard(stream);
	if (target_step * OUTPUT_RATE > VOICE_FREQUENCY_MAXIMUM)
		target_step = VOICE_FREQUENCY_MAXIMUM / OUTPUT_RATE;
	if (target_step * OUTPUT_RATE < VOICE_FREQUENCY_MINIMUM)
		target_step = VOICE_FREQUENCY_MINIMUM / OUTPUT_RATE;
	/* (the step ramps from the last mix's to it, as the gains do) */
	step = stream->gains_valid ? stream->current_step : target_step;
	scale = resampler_scale(step > target_step ? step : target_step);
	width = (long)ceilf(RESAMPLER_ZERO_CROSSINGS / scale);
	/* a voice that ran dry and stopped (below) starts over once it has
	frames again: stream_process starts over only a stream with no packets,
	and the next can come before the finished ones are completed */
	if (stream->silence > (unsigned long)(2 * width))
	{
		if (!voice_has_frames(stream))
			return;
		resampler_reset(stream);
		stream->gains_valid = FALSE;
		step = target_step;
		scale = resampler_scale(step);
		width = (long)ceilf(RESAMPLER_ZERO_CROSSINGS / scale);
	}
	voice_gains(stream, &target_left, &target_right, pair, &target_room, &target_direct_lowpass,
		&target_room_lowpass, &hrtf_on, &target_hrtf);
	/* (port: the binaural rendering takes its share of a 3D voice from the
	pan, and from the Xbox HRTF while the two cross as audio.hrtf changes) */
	binaural_share = hrtf_share(stream);
	if (!stream->gains_valid)
		hrtf_reset(stream);
	if (binaural_share > 0.0f)
	{
		target_binaural = hrtf_target(stream, binaural_share, binaural_filters, binaural_delays);
		target_left *= 1.0f - binaural_share;
		target_right *= 1.0f - binaural_share;
		target_hrtf.gain *= 1.0f - binaural_share;
	}
	/* (a voice turned all the way down needs none of it) */
	binaural_on = target_binaural > 0.0f || stream->current_hrtf > 0.0f;
	/* how much of the voice the HRTF plays: towards all of it while it is
	chosen (hrtf_enabled), none otherwise, a frame's share of the crossfade
	each frame (hrtf_mix_step), however many frames the mixes have; a voice
	starting is at once where it is going */
	hrtf_mix = hrtf_on ? stream->hrtf_mix : 0.0f;
	hrtf_mix_end = hrtf_on && hrtf_enabled ? 1.0f : 0.0f;
	if (!stream->gains_valid)
		hrtf_mix = hrtf_mix_end;
	ramp_hrtf_mix = hrtf_mix_end > hrtf_mix ? 1.0f / HRTF_FADE_FRAMES :
		hrtf_mix_end < hrtf_mix ? -1.0f / HRTF_FADE_FRAMES : 0.0f;
	hrtf_running = hrtf_on && (hrtf_mix > 0.0f || hrtf_mix_end > 0.0f);
	/* (its filters start from silence, as the crossfade starts from none
	of them) */
	if (hrtf_running && (!stream->gains_valid || !stream->hrtf_on))
	{
		stream->hrtf_current = target_hrtf;
		memset(stream->hrtf_frames, 0, sizeof(stream->hrtf_frames));
		memset(stream->hrtf_bands, 0, sizeof(stream->hrtf_bands));
	}
	stream->hrtf_on = hrtf_running;
	if (surround)
		voice_surround_gains(stream, target_left, target_right, pair, target_surround);
	/* a voice starting is at once at its gains, step and way through the
	HRTF; a change to any of them, from the last mix's, ramps from where
	they are to the new ones over PARAMETER_RAMP_FRAMES, then holds them: a
	ramp the mix's frames end goes on in the next mix, and one changed
	before its end ramps on from where it was, so that a change takes the
	same frames however many the mixes have */
	if (!stream->gains_valid)
	{
		if (surround)
			memcpy(stream->current_surround, target_surround, sizeof(target_surround));
		stream->current_left = target_left;
		stream->current_right = target_right;
		stream->current_room = target_room;
		stream->current_step = target_step;
		stream->current_direct_lowpass = target_direct_lowpass;
		stream->current_room_lowpass = target_room_lowpass;
		stream->direct_lowpass[0] = stream->direct_lowpass[1] = 0.0f;
		stream->room_lowpass = 0.0f;
		stream->ramp_frames = 0;
		stream->gains_valid = TRUE;
	}
	else if (target_left != stream->target_left || target_right != stream->target_right ||
		target_room != stream->target_room || target_step != stream->target_step ||
		target_direct_lowpass != stream->target_direct_lowpass || target_room_lowpass != stream->target_room_lowpass ||
		(surround && memcmp(target_surround, stream->target_surround, sizeof(target_surround))) ||
		(hrtf_running && memcmp(&target_hrtf, &stream->hrtf_target, sizeof(target_hrtf))))
	{
		stream->ramp_frames = PARAMETER_RAMP_FRAMES;
	}
	stream->target_left = target_left;
	stream->target_right = target_right;
	stream->target_room = target_room;
	stream->target_step = target_step;
	stream->target_direct_lowpass = target_direct_lowpass;
	stream->target_room_lowpass = target_room_lowpass;
	if (surround)
		memcpy(stream->target_surround, target_surround, sizeof(target_surround));
	if (hrtf_running)
		stream->hrtf_target = target_hrtf;
	ramp_frames = stream->ramp_frames;
	share = ramp_frames ? 1.0f / (float)ramp_frames : 0.0f;
	step = stream->current_step;
	ramp_step = ramp_frames ? (target_step - step) / (double)ramp_frames : 0.0;
	left = stream->current_left;
	right = stream->current_right;
	room = stream->current_room;
	direct_lowpass = stream->current_direct_lowpass;
	room_lowpass = stream->current_room_lowpass;
	/* the low passes' states, here while the mix writes the output (which
	the compiler cannot tell from them) */
	direct_state[0] = stream->direct_lowpass[0];
	direct_state[1] = stream->direct_lowpass[1];
	room_state = stream->room_lowpass;
	ramp_left = (target_left - left) * share;
	ramp_right = (target_right - right) * share;
	ramp_room = (target_room - room) * share;
	ramp_direct_lowpass = (target_direct_lowpass - direct_lowpass) * share;
	ramp_room_lowpass = (target_room_lowpass - room_lowpass) * share;
	hrtf_mix_start = hrtf_mix;
	if (surround)
	{
		for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
		{
			surround_gains[0][speaker] = stream->current_surround[0][speaker];
			surround_gains[1][speaker] = stream->current_surround[1][speaker];
			ramp_surround[0][speaker] = (target_surround[0][speaker] - surround_gains[0][speaker]) * share;
			ramp_surround[1][speaker] = (target_surround[1][speaker] - surround_gains[1][speaker]) * share;
			/* (a 2D voice may be heard from the center, the backs or the
			LFE alone, its left and right gains 0) */
			if (surround_gains[0][speaker] || surround_gains[1][speaker] || target_surround[0][speaker] ||
				target_surround[1][speaker])
			{
				surround_heard = TRUE;
			}
		}
	}

	for (frame = 0; frame < frames; frame++)
	{
		float sample_left = 0.0f, sample_right = 0.0f;
		long tap;

		/* the frames ahead the low pass reaches; after the last packet,
		silence, until what was taken has played out */
		while ((long)(stream->history_count - stream->center) <= width)
		{
			unsigned long slot = stream->history_count % RESAMPLER_HISTORY;
			float taken[2];

			if (take_frame(stream, taken))
			{
				stream->silence = 0;
			}
			else
			{
				taken[0] = taken[1] = 0.0f;
				stream->silence++;
			}
			stream->history[0][slot] = stream->history[0][slot + RESAMPLER_HISTORY] = taken[0];
			stream->history[1][slot] = stream->history[1][slot + RESAMPLER_HISTORY] = taken[1];
			stream->history_count++;
		}
		if (stream->silence > (unsigned long)(2 * width))
			break;

		if (!binaural_on && !left && !right && !room && !ramp_left && !ramp_right && !ramp_room && !surround_heard)
		{
			/* a voice turned all the way down (out of earshot), and not
			sending to the reverb, only moves on */
		}
		else if (resampler_mode == RESAMPLER_LINEAR)
		{
			/* (the frame the output is at and the next, side by side
			however the ring wraps) */
			const float *left_frames = stream->history[0] + stream->center % RESAMPLER_HISTORY;
			const float *right_frames = stream->history[1] + stream->center % RESAMPLER_HISTORY;
			float fraction = (float)stream->phase;

			sample_left = left_frames[0] + (left_frames[1] - left_frames[0]) * fraction;
			sample_right = right_frames[0] + (right_frames[1] - right_frames[0]) * fraction;
		}
		else if (resampler_mode == RESAMPLER_XBOX)
		{
			/* the Xbox's low pass (direct_phases), never narrowed */
			double position = stream->phase * RESAMPLER_TABLE_STEPS;
			unsigned long row = (unsigned long)position;
			float blend = (float)(position - (double)row);
			unsigned long first = (stream->center + 1 - DIRECT_ZERO_CROSSINGS) % RESAMPLER_HISTORY;

			sample_left = direct_dot(stream->history[0] + first, direct_phases[row], direct_deltas[row], blend);
			sample_right = stream->channels == 1 ? sample_left :
				direct_dot(stream->history[1] + first, direct_phases[row], direct_deltas[row], blend);
		}
		else if (scale == 1.0f)
		{
			double position = stream->phase * RESAMPLER_TABLE_STEPS;
			unsigned long row = (unsigned long)position;
			float blend = (float)(position - (double)row);
			unsigned long first = (stream->center + 1 - RESAMPLER_ZERO_CROSSINGS) % RESAMPLER_HISTORY;

			/* (a mono voice's two channels are the same: take_frame) */
			if (stream->channels == 1)
			{
				sample_left = sample_right = resampler_dot(stream->history[0] + first, resampler_phases[row],
					resampler_deltas[row], blend);
			}
			else
			{
				resampler_dot2(stream->history[0] + first, stream->history[1] + first, resampler_phases[row],
					resampler_deltas[row], blend, &sample_left, &sample_right);
			}
		}
		else
		{
			unsigned long first = (stream->center + 1 - (unsigned long)width) % RESAMPLER_HISTORY;
			const float *left_frames = stream->history[0] + first, *right_frames = stream->history[1] + first;

			for (tap = 1 - width; tap <= width; tap++)
			{
				float weight = scale * resampler_weight(fabsf((float)tap - (float)stream->phase) * scale * RESAMPLER_TABLE_STEPS);

				sample_left += left_frames[tap + width - 1] * weight;
				if (stream->channels == 2)
					sample_right += right_frames[tap + width - 1] * weight;
			}
			if (stream->channels == 1)
				sample_right = sample_left;
		}
		/* the room send, with its own low pass, through the Xbox's resampler
		(send_phases), not the mixer's: the reverb folds the send's top
		octave onto its bottom one, and the Xbox's let through images and
		aliases there that the mixer's takes out (recorded, the cave's
		reverberation of a click was 4 dB louder below 500 Hz at 48 kHz, 3.6
		dB at 44.1 kHz) */
		if (room || ramp_room)
		{
			float mono = send_frame(stream);

			room_state = flush_denormal(mono + room_lowpass * (room_state - mono));
			send[frame] += room_state * room;
		}
		/* the direct path, muffled, or as it is; a mono voice's mix bins or pan
		split it across the speakers */
		if (direct_lowpass || ramp_direct_lowpass)
		{
			sample_left = flush_denormal(sample_left + direct_lowpass * (direct_state[0] - sample_left));
			sample_right = flush_denormal(sample_right + direct_lowpass * (direct_state[1] - sample_right));
		}
		direct_state[0] = sample_left;
		direct_state[1] = sample_right;
		if (binaural_on)
			binaural_mono[frame] = stream->channels == 1 ? sample_left : 0.5f * (sample_left + sample_right);
		if (surround)
		{
			/* the first row's gains take the left (a mono voice's only)
			channel, or a positioned voice's middle; the second the right, or
			its difference; through the HRTF (hrtf_process, after), the pan's
			share while the choice changes */
			float *speakers = output + frame * output_channels;
			float panned = hrtf_running ? 1.0f - hrtf_mix : 1.0f;
			float first = (positioned ? 0.5f * (sample_left + sample_right) : sample_left) * panned;
			float second = (positioned ? 0.5f * (sample_left - sample_right) :
				stream->channels == 2 ? sample_right : 0.0f) * panned;

			if (hrtf_running)
			{
				hrtf_direct[frame] = sample_left;
				hrtf_mix = hrtf_mix_step(hrtf_mix, ramp_hrtf_mix, hrtf_mix_end);
			}
			for (speaker = 0; speaker < output_channels; speaker++)
			{
				speakers[speaker] += first * surround_gains[0][speaker] + second * surround_gains[1][speaker];
				surround_gains[0][speaker] += ramp_surround[0][speaker];
				surround_gains[1][speaker] += ramp_surround[1][speaker];
			}
		}
		else if (hrtf_running)
		{
			/* through the HRTF (hrtf_process, after), crossfaded with the
			pan while the choice changes */
			float panned = 1.0f - hrtf_mix;

			hrtf_direct[frame] = sample_left;
			if (panned > 0.0f)
			{
				output[frame * 2] += sample_left * left * panned;
				output[frame * 2 + 1] += sample_left * right * panned;
			}
			hrtf_mix = hrtf_mix_step(hrtf_mix, ramp_hrtf_mix, hrtf_mix_end);
		}
		else if (stream->channels == 1)
		{
			output[frame * 2] += sample_left * left;
			output[frame * 2 + 1] += sample_left * right;
		}
		else if (positioned)
		{
			/* the channels' middle panned, and their difference kept as
			wide as the far ear's gain: the sound comes from where it is,
			still stereo */
			float middle = 0.5f * (sample_left + sample_right);
			float side = 0.5f * (sample_left - sample_right);
			float far_gain = left < right ? left : right;

			output[frame * 2] += middle * left + side * far_gain;
			output[frame * 2 + 1] += middle * right - side * far_gain;
		}
		else
		{
			output[frame * 2] += sample_left * left;
			output[frame * 2 + 1] += sample_right * right;
		}
		left += ramp_left;
		right += ramp_right;
		room += ramp_room;
		direct_lowpass += ramp_direct_lowpass;
		room_lowpass += ramp_room_lowpass;
		stream->phase += step;
		step += ramp_step;
		while (stream->phase >= 1.0)
		{
			stream->phase -= 1.0;
			stream->center++;
		}
		/* (at the ramp's end, exactly the new ones, held) */
		if (frame + 1 == ramp_frames)
		{
			left = target_left;
			right = target_right;
			room = target_room;
			direct_lowpass = target_direct_lowpass;
			room_lowpass = target_room_lowpass;
			step = target_step;
			ramp_left = ramp_right = ramp_room = ramp_direct_lowpass = ramp_room_lowpass = 0.0f;
			ramp_step = 0.0;
			if (surround)
			{
				memcpy(surround_gains, target_surround, sizeof(surround_gains));
				memset(ramp_surround, 0, sizeof(ramp_surround));
			}
		}
	}
	stream->direct_lowpass[0] = direct_state[0];
	stream->direct_lowpass[1] = direct_state[1];
	stream->room_lowpass = room_state;
	/* (the frames mixed: all of them, or those before the voice ran dry) */
	if (hrtf_running)
	{
		hrtf_process(stream, hrtf_direct, output, frame, ramp_frames, &target_hrtf, hrtf_mix_start, ramp_hrtf_mix,
			hrtf_mix_end);
	}
	stream->hrtf_mix = hrtf_running ? hrtf_mix : hrtf_mix_end;
	/* (port: the binaural rendering's, after the mix as the Xbox HRTF's) */
	if (binaural_on && frame)
		hrtf_binaural_mix(stream, binaural_mono, output, frame, target_binaural, binaural_filters, binaural_delays);
	else if (!binaural_on && stream->hrtf_valid)
		hrtf_reset(stream);
	/* where the ramp got to, and the frames of it left */
	stream->current_left = left;
	stream->current_right = right;
	stream->current_room = room;
	stream->current_step = step;
	stream->current_direct_lowpass = direct_lowpass;
	stream->current_room_lowpass = room_lowpass;
	if (surround)
		memcpy(stream->current_surround, surround_gains, sizeof(surround_gains));
	stream->ramp_frames = ramp_frames > frame ? ramp_frames - frame : 0;
}

/* The game cuts a voice off (Flush) to play the next sound on its channel:
each shot of a weapon fired faster than its sound lasts takes the last's
channel (sound classes allow one weapon_fire sound an object). Stopped dead,
a waveform drops from wherever it was to nothing, a click, and a rifle's
shots crackled. The packets must go back to the game at once (it refills the
channel, and may reuse their memory), so what the voice would play next is
mixed now, CUT_FADE_FRAMES of it at its gains, faded out along half a cosine,
and played over the next mixes beside the new sound (mix_cut). What is left
of a cut still playing is kept under it. The fade is the voice's own output
only while that is its frames at its gains through its low passes (whose
state mix_voice carries on into it): processing of a voice that holds what
it played (an HRTF's filters, a delay) must take the fade through itself
too, or the cut jumps from the one to the other and clicks again. */
static void voice_cut(struct sdl_stream *stream)
{
	static float output[CUT_FADE_FRAMES * MAXIMUM_OUTPUT_CHANNELS], send[CUT_FADE_FRAMES];
	unsigned long frame, channel, left = CUT_FADE_FRAMES - stream->cut_played;

	if (stream->paused || !stream->packet_count || !stream->gains_valid)
		return;
	memset(output, 0, sizeof(output));
	memset(send, 0, sizeof(send));
	mix_voice(stream, output, send, CUT_FADE_FRAMES);
	for (frame = 0; frame < CUT_FADE_FRAMES; frame++)
	{
		float fade = 0.5f + 0.5f * (float)cos(3.14159265358979 * (double)(frame + 1) / CUT_FADE_FRAMES);

		/* (what is left of the earlier cut is read only where it is: past
		it is whatever it was, a NaN perhaps, which no factor of 0 hides) */
		for (channel = 0; channel < output_channels; channel++)
		{
			stream->cut[frame * output_channels + channel] = output[frame * output_channels + channel] * fade +
				(frame < left ? stream->cut[(stream->cut_played + frame) * output_channels + channel] : 0.0f);
		}
		stream->cut_send[frame] = send[frame] * fade +
			(frame < left ? stream->cut_send[stream->cut_played + frame] : 0.0f);
	}
	stream->cut_played = 0;
}

/* a cut voice's fade out (voice_cut), into the mix */
static void mix_cut(struct sdl_stream *stream, float *output, float *send, unsigned long frames)
{
	unsigned long frame, channel;

	for (frame = 0; frame < frames && stream->cut_played < CUT_FADE_FRAMES; frame++, stream->cut_played++)
	{
		for (channel = 0; channel < output_channels; channel++)
			output[frame * output_channels + channel] += stream->cut[stream->cut_played * output_channels + channel];
		send[frame] += stream->cut_send[stream->cut_played];
	}
}

#include "dsound_xbox_reverb.inl"

#include "dsound_xbox_lfe.inl"

/* ---------- limiter

The game sets its mix bins' headroom to 0 (sound_dsound_xbox.c), so the voices
sum at their full level, as on the Xbox, and a pile of loud ones goes over
full scale. Clipping each sample, or bending it near full scale, distorts the
sound: dialogue over gunfire crackled. Instead the whole mix is turned down
for as long as it would go over, both channels alike. The output is delayed
LIMITER_LOOKAHEAD - 1 frames (1.3 ms), so that the gain comes down smoothly
before each peak: the smallest gain the frames ahead need, averaged over the
last LIMITER_LOOKAHEAD frames, is never more than a peak needs when it plays.
The gain comes back up over LIMITER_RELEASE_SECONDS. */

#define LIMITER_CEILING 1.0f /* the measured Xbox output reaches full scale */
#define LIMITER_LOOKAHEAD 64
#define LIMITER_RELEASE_SECONDS 0.1f

static struct
{
	/* the frames the output is delayed by, and the gain each needs */
	float delay[LIMITER_LOOKAHEAD][MAXIMUM_OUTPUT_CHANNELS];
	float needed[LIMITER_LOOKAHEAD];
	/* how many of them are under 1: none while nothing is too loud, when the
	smallest is 1 without looking */
	unsigned long limiting;
	/* the gain held down to what the frames ahead need, coming back up */
	float held;
	/* its last LIMITER_LOOKAHEAD values, and their sum */
	float history[LIMITER_LOOKAHEAD];
	double history_sum;
	unsigned long position;
	BOOL initialized;
} limiter;

static void limit(float *output, unsigned long frames)
{
	float release = 1.0f - expf(-1.0f / (LIMITER_RELEASE_SECONDS * OUTPUT_RATE));
	unsigned long frame, index, channel;

	if (!limiter.initialized)
	{
		for (index = 0; index < LIMITER_LOOKAHEAD; index++)
		{
			limiter.needed[index] = 1.0f;
			limiter.history[index] = 1.0f;
		}
		limiter.held = 1.0f;
		limiter.history_sum = LIMITER_LOOKAHEAD;
		limiter.initialized = TRUE;
	}
	for (frame = 0; frame < frames; frame++)
	{
		float *sample = output + frame * output_channels;
		unsigned long position = limiter.position;
		unsigned long oldest = (position + 1) % LIMITER_LOOKAHEAD;
		float peak = 0.0f, lowest, gain;

		/* the new frame takes the slot of the oldest, which has played */
		for (channel = 0; channel < output_channels; channel++)
		{
			if (fabsf(sample[channel]) > peak)
				peak = fabsf(sample[channel]);
			limiter.delay[position][channel] = sample[channel];
		}
		limiter.limiting -= limiter.needed[position] < 1.0f;
		limiter.needed[position] = peak > LIMITER_CEILING ? LIMITER_CEILING / peak : 1.0f;
		limiter.limiting += limiter.needed[position] < 1.0f;
		lowest = 1.0f;
		if (limiter.limiting)
		{
			lowest = limiter.needed[0];
			for (index = 1; index < LIMITER_LOOKAHEAD; index++)
			{
				if (limiter.needed[index] < lowest)
					lowest = limiter.needed[index];
			}
		}
		if (lowest < limiter.held)
			limiter.held = lowest;
		else
			limiter.held += (lowest - limiter.held) * release;
		limiter.history_sum += limiter.held - limiter.history[position];
		limiter.history[position] = limiter.held;
		gain = (float)(limiter.history_sum / LIMITER_LOOKAHEAD);
		/* the frame LIMITER_LOOKAHEAD - 1 frames old plays */
		for (channel = 0; channel < output_channels; channel++)
			sample[channel] = limiter.delay[oldest][channel] * gain;
		limiter.position = oldest;
	}
}

/* The buffers' front pair, under the output limiter. Interpolate the two
samples around the cursor, wrapping at the loop boundary. Unlike stream
packets these samples stay alive until SetBufferData or Release. */
static void mix_buffers(float *output, unsigned long frames)
{
	struct sdl_buffer *buffer;

	for (buffer = buffers; buffer; buffer = buffer->next)
	{
		unsigned long frame, end;
		double step = buffer->rate / OUTPUT_RATE;
		float target = buffer->volume * master_volume;

		if (!buffer->playing || !buffer->samples || !buffer->frames)
			continue;
		end = buffer->looping ? buffer->loop_end : buffer->frames;
		for (frame = 0; frame < frames; frame++)
		{
			unsigned long index = (unsigned long)buffer->position, next = index + 1, channel;
			float fraction = (float)(buffer->position - index);

			if (next >= end)
				next = buffer->looping ? buffer->loop_start : index;
			/* About 10 ms to settle after starting or changing volume. */
			buffer->current_volume = flush_denormal(buffer->current_volume +
				(target - buffer->current_volume) * (1.0f / 120.0f));
			for (channel = 0; channel < 2; channel++)
			{
				unsigned long source_channel = buffer->channels == 1 ? 0 : channel;
				float a = buffer->samples[index * buffer->channels + source_channel];
				float b = buffer->samples[next * buffer->channels + source_channel];

				output[frame * output_channels + channel] +=
					(a + (b - a) * fraction) * buffer->current_volume * (1.0f / 32768.0f);
			}
			buffer->position += step;
			if (buffer->position >= end)
			{
				if (!buffer->looping)
				{
					buffer->playing = FALSE;
					buffer->position = 0;
					buffer->current_volume = 0;
					break;
				}
				buffer->position = buffer->loop_start +
					fmod(buffer->position - buffer->loop_start, end - buffer->loop_start);
			}
		}
	}
}

/* ---------- recording

What the mixer plays, for the audio probe (port/linux/game/audio_probe.c,
audio_probe.h): <prefix>-final.wav is the output as it goes to the device,
<prefix>-dry.wav the voices' mix before the reverb, <prefix>-reverb.wav the
reverb's output (the reflections and the late reverberation, at the reverb's
level), each 32-bit float at OUTPUT_RATE, as many channels as the output's
(stereo, 5.1 or 7.1). The final mix comes through the limiter, LIMITER_LOOKAHEAD - 1 frames behind the other two. The mixer
puts what it mixes in a ring; the game's thread writes it out
(audio_capture_update), so the audio thread never waits on a file. */

#define CAPTURE_BUSES 3
#define CAPTURE_RING_FRAMES (OUTPUT_RATE * 4)

static const char *const capture_bus_names[CAPTURE_BUSES] = { "final", "dry", "reverb" };

static pthread_mutex_t capture_lock = PTHREAD_MUTEX_INITIALIZER;
static struct
{
	volatile BOOL on;
	/* CAPTURE_BUSES rings of CAPTURE_RING_FRAMES frames */
	float *ring;
	/* frames put in the rings and taken out, from the start */
	unsigned long put, taken;
	/* frames mixed since the start (put, and any the full rings lost) */
	unsigned long frames;
	unsigned long lost;
	/* the output's channels when the recording started */
	unsigned long channels;
	FILE *files[CAPTURE_BUSES];
	unsigned long file_frames;
	/* the mixer's (mix) */
	float dry[MIX_CHUNK_FRAMES * MAXIMUM_OUTPUT_CHANNELS];
	float reverb[MIX_CHUNK_FRAMES * MAXIMUM_OUTPUT_CHANNELS];
} capture;

static void capture_push(const float *output, unsigned long frames)
{
	const float *buses[CAPTURE_BUSES];
	unsigned long bus, frame, channel;

	buses[0] = output;
	buses[1] = capture.dry;
	buses[2] = capture.reverb;
	pthread_mutex_lock(&capture_lock);
	/* (stopped since this mix began) */
	if (!capture.on)
	{
		pthread_mutex_unlock(&capture_lock);
		return;
	}
	if (capture.put - capture.taken + frames > CAPTURE_RING_FRAMES)
	{
		capture.lost += frames;
	}
	else
	{
		for (frame = 0; frame < frames; frame++)
		{
			unsigned long slot = (capture.put + frame) % CAPTURE_RING_FRAMES;

			for (bus = 0; bus < CAPTURE_BUSES; bus++)
			{
				float *ring = capture.ring + (bus * CAPTURE_RING_FRAMES + slot) * capture.channels;

				for (channel = 0; channel < capture.channels; channel++)
					ring[channel] = buses[bus][frame * capture.channels + channel];
			}
		}
		capture.put += frames;
	}
	capture.frames += frames;
	pthread_mutex_unlock(&capture_lock);
}

static void capture_write_u32(FILE *file, unsigned long value)
{
	unsigned char bytes[4];

	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
	bytes[2] = (unsigned char)(value >> 16);
	bytes[3] = (unsigned char)(value >> 24);
	fwrite(bytes, 1, 4, file);
}

static void capture_write_u16(FILE *file, unsigned int value)
{
	unsigned char bytes[2];

	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
	fwrite(bytes, 1, 2, file);
}

/* a WAVE header of 32-bit float frames */
static void capture_write_header(FILE *file, unsigned long frames)
{
	unsigned long data_bytes = frames * capture.channels * sizeof(float);

	fwrite("RIFF", 1, 4, file);
	capture_write_u32(file, 4 + 8 + 18 + 8 + data_bytes);
	fwrite("WAVEfmt ", 1, 8, file);
	capture_write_u32(file, 18);
	capture_write_u16(file, 3); /* WAVE_FORMAT_IEEE_FLOAT */
	capture_write_u16(file, capture.channels);
	capture_write_u32(file, OUTPUT_RATE);
	capture_write_u32(file, OUTPUT_RATE * capture.channels * sizeof(float));
	capture_write_u16(file, capture.channels * sizeof(float));
	capture_write_u16(file, 32);
	capture_write_u16(file, 0);
	fwrite("data", 1, 4, file);
	capture_write_u32(file, data_bytes);
}

int audio_capture_start(char const *path_prefix)
{
	unsigned long bus;

	if (capture.on)
		return 0;
	capture.channels = output_channels;
	capture.ring = calloc((size_t)CAPTURE_BUSES * CAPTURE_RING_FRAMES * capture.channels, sizeof(float));
	if (!capture.ring)
		return 0;
	for (bus = 0; bus < CAPTURE_BUSES; bus++)
	{
		char name[512], path[1024];

		/* (the probe's path is the game's, an Xbox path) */
		snprintf(name, sizeof(name), "%s-%s.wav", path_prefix, capture_bus_names[bus]);
		platform_translate_path(name, path, sizeof(path));
		capture.files[bus] = fopen(path, "wb");
		if (!capture.files[bus])
		{
			platform_log("audio probe: cannot write %s", path);
			while (bus-- > 0)
				fclose(capture.files[bus]);
			free(capture.ring);
			capture.ring = NULL;
			return 0;
		}
		capture_write_header(capture.files[bus], 0);
	}
	pthread_mutex_lock(&capture_lock);
	capture.put = capture.taken = capture.frames = capture.lost = 0;
	capture.file_frames = 0;
	capture.on = TRUE;
	pthread_mutex_unlock(&capture_lock);
	platform_log("audio probe: recording the mixer (audio.volume %.2f, reverb %s, %s resampling, the final mix %d "
		"frames behind the buses)", master_volume, reverb_enabled ? "on" : "off",
		resampler_mode == RESAMPLER_LINEAR ? "linear" : resampler_mode == RESAMPLER_SINC ? "sinc" : "xbox",
		LIMITER_LOOKAHEAD - 1);
	return 1;
}

unsigned long audio_capture_frames(void)
{
	unsigned long frames;

	pthread_mutex_lock(&capture_lock);
	frames = capture.frames;
	pthread_mutex_unlock(&capture_lock);
	return frames;
}

unsigned long audio_capture_rate(void)
{
	return OUTPUT_RATE;
}

char const *audio_capture_platform(void)
{
	return "pc";
}

void audio_capture_update(void)
{
	unsigned long put, bus;

	if (!capture.ring)
		return;
	pthread_mutex_lock(&capture_lock);
	put = capture.put;
	pthread_mutex_unlock(&capture_lock);
	/* (the frames between taken and put are the mixer's no longer) */
	while (capture.taken != put)
	{
		unsigned long slot = capture.taken % CAPTURE_RING_FRAMES;
		unsigned long count = put - capture.taken;

		if (count > CAPTURE_RING_FRAMES - slot)
			count = CAPTURE_RING_FRAMES - slot;
		for (bus = 0; bus < CAPTURE_BUSES; bus++)
			fwrite(capture.ring + (bus * CAPTURE_RING_FRAMES + slot) * capture.channels, sizeof(float) * capture.channels,
				count, capture.files[bus]);
		capture.file_frames += count;
		pthread_mutex_lock(&capture_lock);
		capture.taken += count;
		pthread_mutex_unlock(&capture_lock);
	}
}

void audio_capture_stop(void)
{
	unsigned long bus;

	if (!capture.on)
		return;
	pthread_mutex_lock(&capture_lock);
	capture.on = FALSE;
	pthread_mutex_unlock(&capture_lock);
	audio_capture_update();
	for (bus = 0; bus < CAPTURE_BUSES; bus++)
	{
		fseek(capture.files[bus], 0, SEEK_SET);
		capture_write_header(capture.files[bus], capture.file_frames);
		fclose(capture.files[bus]);
		capture.files[bus] = NULL;
	}
	if (capture.lost)
		platform_log("audio probe: the recording lost %lu frames (the game's thread was held up); what follows is "
			"that much early", capture.lost);
	platform_log("audio probe: recorded %lu frames", capture.file_frames);
	free(capture.ring);
	capture.ring = NULL;
}


/* hrtf_level moved towards audio.hrtf's binaural rendering (hrtf_update):
port, ChupathingyCE's */
static void hrtf_fade(unsigned long frames)
{
	float target = hrtf_wanted && binaural.loaded && output_channels == 2 ? 1.0f : 0.0f;
	float step = (float)frames / (HRTF_FADE_SECONDS * OUTPUT_RATE);

	hrtf_level = clamp_real(target, hrtf_level - step, hrtf_level + step);
}

/* a mix of at most MIX_PASS_FRAMES (mix) */
static void mix_chunk(float *output, unsigned long frames)
{
	struct sdl_stream *stream;
	DSI3DL2LISTENER properties;
	BOOL enabled, changed = FALSE;
	BOOL capturing = capture.on;
	unsigned long serial = 0;

	memset(output, 0, frames * output_channels * sizeof(float));
	memset(reverb.send, 0, frames * sizeof(float));
	hrtf_fade(frames);
	pthread_mutex_lock(&mixer_lock);
	enabled = reverb_enabled;
	/* (port, ChupathingyCE's: a voice at a time, the game let in between
	them, mixer_lock_wanted; and each one's cut fade, mix_cut) */
	for (stream = streams; stream; stream = mix_cursor)
	{
		mix_voice(stream, output, reverb.send, frames);
		mix_cut(stream, output, reverb.send, frames);
		mix_cursor = stream->next;
		if (mix_cursor && __atomic_load_n(&mixer_lock_wanted, __ATOMIC_RELAXED))
		{
			pthread_mutex_unlock(&mixer_lock);
			sched_yield();
			pthread_mutex_lock(&mixer_lock);
		}
	}
	mix_cursor = NULL;
	mix_buffers(output, frames);
	/* a new environment, once the fade to the last is over */
	if (reverb.serial != environment_serial && !reverb.fade)
	{
		properties = environment;
		serial = environment_serial;
		changed = TRUE;
	}
	pthread_mutex_unlock(&mixer_lock);
	if (changed)
		reverb_update(&properties, serial);
	if (capturing)
		memcpy(capture.dry, output, frames * capture.channels * sizeof(float));
	/* the reverb runs while something reverberates, and stops once it is
	turned off and faded out, or quiet */
	if (!reverb.silent || (enabled && reverb_sent(reverb.send, frames)))
	{
		reverb_process(reverb.send, output, frames, enabled ? 1.0f : 0.0f);
		if ((!enabled && reverb.level <= 0.0f) || reverb.quiet > REVERB_QUIET_FRAMES)
			reverb_clear();
	}
	if (capturing)
	{
		unsigned long index;

		/* (at the output's level, as the final mix: below) */
		for (index = 0; index < frames * capture.channels; index++)
		{
			capture.reverb[index] = 0.5f * (output[index] - capture.dry[index]);
			capture.dry[index] *= 0.5f;
		}
	}
	/* OpenH1's Xbox captures put the final game mix at half the voice sum. */
	{
		unsigned long sample;
		for (sample = 0; sample < frames * output_channels; sample++)
			output[sample] *= 0.5f;
	}
	lfe_lowpass(output, frames);
	/* the players' voices (voice_audio.c), dry, under the limiter; on
	surround output from the front pair */
	if (output_channels > 2)
	{
		float voices[MIX_CHUNK_FRAMES * OUTPUT_CHANNELS];
		unsigned long frame;

		memset(voices, 0, frames * OUTPUT_CHANNELS * sizeof(float));
		voice_audio_mix(voices, frames);
		for (frame = 0; frame < frames; frame++)
		{
			output[frame * output_channels] += voices[frame * 2];
			output[frame * output_channels + 1] += voices[frame * 2 + 1];
		}
	}
	else
	{
		voice_audio_mix(output, frames);
	}
	limit(output, frames);
	if (capturing)
		capture_push(output, frames);
#ifdef HALO_CAPTURE
	/* (port, ChupathingyCE's: the clips' sound, when they are on:
	capture_clips.c) */
	capture_clip_audio(output, frames, output_channels);
#endif
}

/* frames of output_channels floats of the mix into output (at most
MIX_CHUNK_FRAMES), MIX_PASS_FRAMES at a time (port: ChupathingyCE's pass,
which the game's thread waits out at most) */
static void mix(float *output, unsigned long frames)
{
	while (frames > MIX_PASS_FRAMES)
	{
		mix_chunk(output, MIX_PASS_FRAMES);
		output += MIX_PASS_FRAMES * output_channels;
		frames -= MIX_PASS_FRAMES;
	}
	if (frames)
		mix_chunk(output, frames);
}

/* ---------- output */

static SDL_AudioStream *audio_stream;
static BOOL audio_started = FALSE;
static BOOL audio_silent_clock = FALSE;
/* the playback devices' names (for Audio Setup's Output Device: the
menus' copies of them), at most maximum; 0 without SDL's audio
(ChupathingyCE's, for its settings_rows.c) */
int platform_audio_device_names(char const **names, int maximum)
{
#ifdef HALO_BROWSER
	(void)names;
	(void)maximum;
	return 0;
#else
	static char stored[32][256];
	SDL_AudioDeviceID *devices;
	int count = 0, index, written = 0;

	if (!config_boolean("audio.enabled") || !platform_sdl_initialize())
		return 0;
	devices = SDL_GetAudioPlaybackDevices(&count);
	for (index = 0; devices && index < count && written < maximum && written < 32; index++)
	{
		char const *name = SDL_GetAudioDeviceName(devices[index]);

		if (!name || !name[0])
			continue;
		snprintf(stored[written], sizeof(stored[written]), "%s", name);
		names[written] = stored[written];
		written++;
	}
	SDL_free(devices);
	return written;
#endif
}

/* the device it plays on (audio.output_device), and when it was looked at */
static char audio_device_name[PLATFORM_AUDIO_DEVICE_NAME_SIZE];
static unsigned long audio_device_read_at = (unsigned long)-1;

/* audio.output_device ("default": the system's; none on Android). port (this
fork): OpenCE's setting, kept over ChupathingyCE's audio.device, which chose
the output the same way (sdl_platform.c's platform_audio_device finds it) */
static const char *audio_device_setting(void)
{
	const char *name = config_string("audio.output_device");

	return name && name[0] ? name : "default";
}

#ifdef HALO_BROWSER
static _Atomic unsigned long web_audio_callback_count;
static _Atomic unsigned long web_audio_late_callback_count;
static _Atomic unsigned long web_audio_last_callback_ms;
static _Atomic unsigned long web_audio_maximum_callback_gap_ms;

static void web_audio_record_callback(int additional_amount)
{
	unsigned long now = (unsigned long)emscripten_get_now();
	unsigned long previous = atomic_exchange_explicit(
		&web_audio_last_callback_ms, now, memory_order_relaxed);
	unsigned long frames = additional_amount > 0 ?
		(unsigned long)additional_amount / (OUTPUT_CHANNELS * sizeof(float)) : 0;
	unsigned long expected = frames ? (frames * 1000) / OUTPUT_RATE : 0;
	unsigned long late_threshold = expected * 2;

	if (late_threshold < 50)
		late_threshold = 50;
	atomic_fetch_add_explicit(&web_audio_callback_count, 1, memory_order_relaxed);
	if (previous)
	{
		unsigned long gap = now - previous;
		unsigned long maximum = atomic_load_explicit(
			&web_audio_maximum_callback_gap_ms, memory_order_relaxed);

		if (gap > late_threshold)
			atomic_fetch_add_explicit(&web_audio_late_callback_count, 1, memory_order_relaxed);
		while (gap > maximum && !atomic_compare_exchange_weak_explicit(
			&web_audio_maximum_callback_gap_ms, &maximum, gap,
			memory_order_relaxed, memory_order_relaxed))
		{
		}
	}
}

EMSCRIPTEN_KEEPALIVE unsigned long platform_web_audio_callback_count(void)
{
	return atomic_load_explicit(&web_audio_callback_count, memory_order_relaxed);
}

EMSCRIPTEN_KEEPALIVE unsigned long platform_web_audio_late_callback_count(void)
{
	return atomic_load_explicit(&web_audio_late_callback_count, memory_order_relaxed);
}

EMSCRIPTEN_KEEPALIVE unsigned long platform_web_audio_maximum_callback_gap_ms(void)
{
	return atomic_load_explicit(&web_audio_maximum_callback_gap_ms, memory_order_relaxed);
}
#endif

static void SDLCALL audio_callback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
	float buffer[MIX_CHUNK_FRAMES * MAXIMUM_OUTPUT_CHANNELS];

	(void)userdata;
	(void)total_amount;
#ifdef HALO_BROWSER
	web_audio_record_callback(additional_amount);
#endif
	while (additional_amount > 0)
	{
		unsigned long frames = (unsigned long)additional_amount / (output_channels * sizeof(float));

		if (frames > MIX_CHUNK_FRAMES)
			frames = MIX_CHUNK_FRAMES;
		if (!frames)
			frames = 1;
		mix(buffer, frames);
		SDL_PutAudioStreamData(stream, buffer, (int)(frames * output_channels * sizeof(float)));
		additional_amount -= (int)(frames * output_channels * sizeof(float));
	}
}

/* without a device, drain voices in real time */
static void *silent_clock_thread(void *parameter)
{
	float buffer[480 * MAXIMUM_OUTPUT_CHANNELS];
	struct timespec next;

	(void)parameter;
	clock_gettime(CLOCK_MONOTONIC, &next);
	/* (until the game's end asks the platform layer's threads to stop,
	thread_stop.h) */
	while (!thread_stop_requested())
	{
		/* (10 ms, in chunks of MIX_CHUNK_FRAMES at most) */
		mix(buffer, 240);
		mix(buffer, 240);
		next.tv_nsec += 10000000L;
		if (next.tv_nsec >= 1000000000L)
		{
			next.tv_nsec -= 1000000000L;
			next.tv_sec++;
		}
		platform_sleep_until(&next);
	}
	return NULL;
}

static void audio_silent_clock_start(void)
{
	if (audio_silent_clock)
		return;
	audio_silent_clock = TRUE;
	thread_stop_create("silent audio clock", silent_clock_thread, NULL);
}

/* whether the output's name says it is headphones (a headset, earbuds),
for audio.hrtf = "auto": SDL does not say what is plugged in, but the
systems name a headphone output so (macOS "External Headphones", AirPods,
PipeWire's and Windows' "Headphones", a USB or Bluetooth headset) */
static BOOL audio_output_is_headphones(char *name, unsigned long name_size)
{
	static const char *const words[] = { "headphone", "headset", "airpods", "earbuds", "earphone", "buds", "beats" };
	char lowered[256];
	const char *device_name;
	unsigned long index;

	name[0] = '\0';
	if (!audio_stream)
		return FALSE;
	device_name = SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(audio_stream));
	if (!device_name)
		return FALSE;
	snprintf(name, name_size, "%s", device_name);
	for (index = 0; device_name[index] && index + 1 < sizeof(lowered); index++)
		lowered[index] = (char)(device_name[index] >= 'A' && device_name[index] <= 'Z' ? device_name[index] + 32 :
			device_name[index]);
	lowered[index] = '\0';
	for (index = 0; index < sizeof(words) / sizeof(words[0]); index++)
	{
		if (strstr(lowered, words[index]))
			return TRUE;
	}
	return FALSE;
}

/* audio.hrtf read again (the game's thread): "xbox" (or OpenCE's true) for
the measured Xbox HRTF on every output (hrtf_enabled: dsound_xbox_hrtf.inl);
port (ChupathingyCE's): "on" for the binaural rendering on stereo, or "auto"
for it when the output is headphones, checked again as the output changes,
its responses loaded the first time they are wanted, here, not on the
mixer's thread; "off" (or false) for neither, 3D voices panned */
static BOOL hrtf_automatic = FALSE;

static void hrtf_update(void)
{
	static int logged = -1;
	const char *setting = config_string("audio.hrtf");
	char name[256] = "";
	BOOL wanted, xbox;

	if (!setting)
		setting = "xbox";
	xbox = !strcmp(setting, "xbox") || !strcmp(setting, "true");
	hrtf_automatic = !strcmp(setting, "auto");
	if (!strcmp(setting, "on"))
		wanted = TRUE;
	else if (!strcmp(setting, "auto"))
		wanted = audio_output_is_headphones(name, sizeof(name));
	else
		wanted = FALSE;
	if (xbox != hrtf_enabled)
		platform_log("audio: the Xbox HRTF (audio.hrtf \"%s\") %s", setting, xbox ? "on" : "off");
	mixer_lock_take();
	hrtf_enabled = xbox;
	pthread_mutex_unlock(&mixer_lock);
	if (wanted && !hrtf_load())
	{
		if (logged != 2)
			platform_log("audio: the HRTF data is missing; 3D sounds are panned");
		logged = 2;
		wanted = FALSE;
	}
	if (wanted && output_channels != 2)
		wanted = FALSE;
	if ((int)wanted != logged && logged != 2)
	{
		platform_log("audio: binaural 3D sound (audio.hrtf \"%s\") %s%s%s%s", setting, wanted ? "on" : "off",
			name[0] ? " (output \"" : "", name, name[0] ? "\")" : "");
		logged = (int)wanted;
	}
	hrtf_wanted = wanted;
}

/* audio.channels: "stereo", "5.1", "7.1", or "auto": 5.1 when the device
has six (or seven) channels, 7.1 when it has eight, else stereo. A device
of some other count is an interface's outputs more likely than speakers
around the room, so it plays stereo. Android and the browser play stereo */
static unsigned long choose_output_channels(SDL_AudioDeviceID device)
{
#if defined(HALO_ANDROID) || defined(HALO_WEB) || defined(HALO_ARM64_GUEST)
	(void)device;
	return 2;
#else
	const char *setting = config_string("audio.channels");
	SDL_AudioSpec format;

	if (!strcmp(setting, "stereo"))
		return 2;
	if (!strcmp(setting, "5.1"))
		return 6;
	if (!strcmp(setting, "7.1"))
		return 8;
	if (strcmp(setting, "auto"))
		platform_log("audio.channels \"%s\" is not \"auto\", \"stereo\", \"5.1\" or \"7.1\"; using auto", setting);
	if (!SDL_GetAudioDeviceFormat(device, &format, NULL) &&
		!SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &format, NULL))
	{
		return 2;
	}
	platform_log("audio: the output has %d channels", format.channels);
	if (format.channels == 6 || format.channels == 7)
		return 6;
	if (format.channels == 8)
		return 8;
	return 2;
#endif
}

#ifdef HALO_CONSOLE_AUDIO_OUTPUT
/* the port's: plays channels of float samples at rate from mix, called on
an output thread of its own for each buffer it fills; FALSE if it cannot */
int console_audio_start(int rate, int channels, void (*mix)(float *output, unsigned long frames));
#endif

static void audio_start(void)
{
	SDL_AudioSpec spec;
	long buffer_frames;
	char frames[16];

	if (audio_started)
		return;
	audio_started = TRUE;
	configured_master_volume = (float)config_real("audio.volume");
	master_volume = master_muted ? 0.0f : configured_master_volume;
	reverb_enabled = config_boolean("audio.reverb");
	stereo_doppler_enabled = config_boolean("audio.stereo_doppler");
	resampler_mode = resampler_mode_read();
	doppler_enabled = config_boolean("audio.doppler");
	hrtf_initialize();
	resampler_initialize();
	resampler_phases_initialize();
	reverb_initialize();

	if (config_boolean("audio.enabled") && platform_sdl_initialize())
	{
		snprintf(audio_device_name, sizeof(audio_device_name), "%s", audio_device_setting());
		output_channels = choose_output_channels(platform_audio_device(FALSE, audio_device_name));
		platform_log("sound: %s", output_channels == 8 ? "7.1 surround" : output_channels == 6 ? "5.1 surround" : "stereo");
		spec.format = SDL_AUDIO_F32;
		spec.channels = (int)output_channels;
		spec.freq = OUTPUT_RATE;
		/* audio.buffer_frames: the device's buffer (port_config.c) */
		buffer_frames = config_integer("audio.buffer_frames");
		if (buffer_frames < 64)
			buffer_frames = 64;
		if (buffer_frames > 8192)
			buffer_frames = 8192;
		snprintf(frames, sizeof(frames), "%ld", buffer_frames);
		/* (not in the browser: SDL's Emscripten backend doubles its default
		frames for the main thread's scheduling, and a smaller buffer there
		broke up the sound in some browsers) */
#if !defined(HALO_BROWSER) && !defined(HALO_WEB)
		SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, frames);
#endif
		audio_device_read_at = config_changes();
		audio_stream = SDL_OpenAudioDeviceStream(platform_audio_device(FALSE, audio_device_name), &spec,
			audio_callback, NULL);
		if (!audio_stream && strcmp(audio_device_name, "default"))
			audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_callback, NULL);
		if (audio_stream)
		{
			SDL_ResumeAudioStreamDevice(audio_stream);
			return;
		}
#ifdef HALO_CONSOLE_AUDIO_OUTPUT
		/* the console's own output, where SDL has none (the Steam Link's:
		its HDMI audio is the firmware's own SDL's) */
		output_channels = 2;
		if (console_audio_start(OUTPUT_RATE, (int)output_channels, mix))
		{
			platform_log("sound: the console's own output");
			return;
		}
#endif
		platform_log("cannot open an audio device (%s); sound is silent", SDL_GetError());
		output_channels = 2;
	}
	audio_silent_clock_start();
}

/* (the event thread, each frame: sdl_platform.c) audio.output_device
changed (Settings > Audio): the sound goes on on the new device, else the
system's default. audio.hrtf = "auto" sees the new output within a second
(DirectSoundDoWork's hrtf_update, on the game's thread) */
void dsound_sdl_output_device_check(void)
{
	SDL_AudioSpec spec;
	SDL_AudioStream *stream;

	if (!audio_stream || audio_device_read_at == config_changes())
		return;
	audio_device_read_at = config_changes();
	if (!strcmp(audio_device_name, audio_device_setting()))
		return;
	snprintf(audio_device_name, sizeof(audio_device_name), "%s", audio_device_setting());
	spec.format = SDL_AUDIO_F32;
	/* (the game's mix bins are set for the channels the sound started with;
	SDL turns them into the new device's) */
	spec.channels = (int)output_channels;
	spec.freq = OUTPUT_RATE;
	SDL_DestroyAudioStream(audio_stream);
	stream = SDL_OpenAudioDeviceStream(platform_audio_device(FALSE, audio_device_name), &spec, audio_callback, NULL);
	if (!stream)
		stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_callback, NULL);
	audio_stream = stream;
	if (audio_stream)
	{
		SDL_ResumeAudioStreamDevice(audio_stream);
		platform_log("audio: playing on %s", audio_device_name);
	}
	else
	{
		/* (none at all: the voices drained in real time, as at the start) */
		platform_log("audio: cannot open an audio device (%s); sound is silent", SDL_GetError());
		audio_silent_clock_start();
	}
}

/* ---------- completion */

static void packet_release(struct voice_packet *entry)
{
	free(entry->samples);
	entry->samples = NULL;
}

/* completes the head packet; called with the lock held, which the game's
callback runs without */
static void stream_complete_head(struct sdl_stream *stream, DWORD status, DWORD completed_size)
{
	struct voice_packet *entry = &stream->packets[stream->packet_head];
	XMEDIAPACKET packet = entry->packet;

	/* (one not played to its end, flushed: the cursor was its place, and the
	mixer may take the next while the lock is let go below) */
	if (!entry->finished)
		stream->cursor = 0;
	packet_release(entry);
	entry->finished = FALSE;
	stream->packet_head = (stream->packet_head + 1) % MAXIMUM_STREAM_PACKETS;
	stream->packet_count--;
	if (packet.pdwCompletedSize)
		*packet.pdwCompletedSize = completed_size;
	if (packet.pdwStatus)
		*packet.pdwStatus = status;
	if (stream->callback)
	{
		pthread_mutex_unlock(&mixer_lock);
		stream->callback(stream->context, packet.pContext, status);
		mixer_lock_take();
	}
	else if (packet.hCompletionEvent)
	{
		SetEvent(packet.hCompletionEvent);
	}
}

/* (on the game's thread each frame: port, ChupathingyCE's: with the mixer
mixing, the lock is not waited for but tried again the next frame, a
stream's finished packets told a frame later - waited for, it was up to an
eighth of the Switch's game thread in a busy match; a fifth frame in a row
waits) */
static void streams_complete_finished(void)
{
	static int missed;
	struct sdl_stream *stream;

	if (pthread_mutex_trylock(&mixer_lock) != 0)
	{
		if (++missed < 5)
			return;
		mixer_lock_take();
	}
	missed = 0;
	for (stream = streams; stream; stream = stream->next)
	{
		while (stream->packet_count && stream->packets[stream->packet_head].finished)
			stream_complete_head(stream, XMEDIAPACKET_STATUS_SUCCESS, stream->packets[stream->packet_head].packet.dwMaxSize);
	}
	pthread_mutex_unlock(&mixer_lock);
}

/* ---------- stream interface */

static struct sdl_stream *stream_from_interface(void *stream)
{
	return (struct sdl_stream *)stream;
}

static ULONG STDMETHODCALLTYPE stream_add_reference(IDirectSoundStream *object)
{
	struct sdl_stream *stream = stream_from_interface(object);
	ULONG count;

	mixer_lock_take();
	count = ++stream->reference_count;
	pthread_mutex_unlock(&mixer_lock);
	return count;
}

static HRESULT STDMETHODCALLTYPE stream_flush(IDirectSoundStream *object);

static ULONG STDMETHODCALLTYPE stream_release(IDirectSoundStream *object)
{
	struct sdl_stream *stream = stream_from_interface(object);
	struct sdl_stream **link;
	ULONG count;

	mixer_lock_take();
	count = --stream->reference_count;
	pthread_mutex_unlock(&mixer_lock);
	if (count)
		return count;

	stream_flush(object);
	mixer_lock_take();
	for (link = &streams; *link; link = &(*link)->next)
	{
		if (*link == stream)
		{
			*link = stream->next;
			/* (port: the mixer, between voices, goes on from the next) */
			if (mix_cursor == stream)
				mix_cursor = stream->next;
			break;
		}
	}
	pthread_mutex_unlock(&mixer_lock);
	free(stream);
	return 0;
}

static HRESULT STDMETHODCALLTYPE stream_get_info(IDirectSoundStream *object, LPXMEDIAINFO information)
{
	struct sdl_stream *stream = stream_from_interface(object);

	memset(information, 0, sizeof(*information));
	information->dwFlags = XMO_STREAMF_FIXED_SAMPLE_SIZE | XMO_STREAMF_INPUT_ASYNC;
	information->dwInputSize = stream->adpcm ? XBOX_ADPCM_BLOCK_BYTES * stream->channels : 2 * stream->channels;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_get_status(IDirectSoundStream *object, LPDWORD status)
{
	struct sdl_stream *stream = stream_from_interface(object);

	mixer_lock_take();
	*status = stream->packet_count < MAXIMUM_STREAM_PACKETS ? XMO_STATUSF_ACCEPT_INPUT_DATA : 0;
	pthread_mutex_unlock(&mixer_lock);
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_process(IDirectSoundStream *object, LPCXMEDIAPACKET input, LPCXMEDIAPACKET output)
{
	struct sdl_stream *stream = stream_from_interface(object);
	struct voice_packet *entry;
	unsigned long frames = 0;
	short *samples;

	(void)output;
	if (!input)
		return E_INVALIDARG;
	/* decode outside the lock */
	samples = stream->adpcm ?
		decode_adpcm(input->pvBuffer, input->dwMaxSize, stream->channels, &frames) :
		decode_pcm(input->pvBuffer, input->dwMaxSize, stream->channels, &frames);
	mixer_lock_take();
	if (stream->packet_count == MAXIMUM_STREAM_PACKETS)
	{
		pthread_mutex_unlock(&mixer_lock);
		free(samples);
		return E_OUTOFMEMORY;
	}
	entry = &stream->packets[(stream->packet_head + stream->packet_count) % MAXIMUM_STREAM_PACKETS];
	entry->packet = *input;
	entry->samples = samples;
	entry->frames = samples ? frames : 0;
	entry->finished = FALSE;
	if (input->pdwStatus)
		*input->pdwStatus = XMEDIAPACKET_STATUS_PENDING;
	if (input->pdwCompletedSize)
		*input->pdwCompletedSize = 0;
	if (!stream->packet_count)
	{
		/* a stream that ran dry starts over */
		stream->cursor = 0;
		resampler_reset(stream);
		stream->gains_valid = FALSE;
	}
	stream->packet_count++;
	pthread_mutex_unlock(&mixer_lock);
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_discontinuity(IDirectSoundStream *object)
{
	(void)object;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_flush(IDirectSoundStream *object)
{
	struct sdl_stream *stream = stream_from_interface(object);

	mixer_lock_take();
	if (stream->flushing)
	{
		pthread_mutex_unlock(&mixer_lock);
		return S_OK;
	}
	voice_cut(stream);
	/* Completion callbacks release the lock. Only the captured fade may
	play then, not the packets still awaiting cancellation. */
	stream->flushing = TRUE;
	while (stream->packet_count)
	{
		struct voice_packet *head = &stream->packets[stream->packet_head];

		/* Cancellation is synchronous. SUCCESS would let the game's
		completion callback refill the stream we are stopping. */
		stream_complete_head(stream, XMEDIAPACKET_STATUS_FLUSHED,
			head->finished ? head->packet.dwMaxSize : 0);
	}
	stream->flushing = FALSE;
	stream->cursor = 0;
	resampler_reset(stream);
	/* (the next sound on the channel says where it is) */
	stream->stereo_positioned = FALSE;
	pthread_mutex_unlock(&mixer_lock);
	return S_OK;
}

/* A stereo voice of a sound in the world (`positioned`) is panned towards
it, `pan` from -1 (left) to 1 (right), and muffled and reverberated as a 3D
voice is (its I3DL2 source, which the game sets as a 3D channel's), its room
send rolling off from `minimum_distance` with `distance`. The game fades its
volume with distance itself, by `distance_fade`. Not positioned, it plays as
the Xbox played every stereo sound, unpanned and dry. It is shifted as a 3D
voice is by `approach`, how fast it comes towards the listener in world units
a second (doppler_ratio). (sound_manager.c,
update_channels: sound_dsound_xbox.c calls this.) */
void dsound_sdl_stream_set_stereo_position(IDirectSoundStream *object, BOOL positioned, float pan,
	float distance, float minimum_distance, float distance_fade, float approach)
{
	struct sdl_stream *stream = stream_from_interface(object);

	mixer_lock_take();
	stream->stereo_positioned = positioned && stream->channels == 2;
	stream->stereo_pan = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
	stream->stereo_distance = distance > 0.0f ? distance : 0.0f;
	stream->stereo_distance_fade = distance_fade < 0.0f ? 0.0f : (distance_fade > 1.0f ? 1.0f : distance_fade);
	stream->stereo_approach = approach == approach ? approach : 0.0f;
	/* (as the game gives a 3D channel: no maximum) */
	stream->minimum_distance = minimum_distance > 0.0f ? minimum_distance : 0.0f;
	stream->maximum_distance = 3.4e38f;
	pthread_mutex_unlock(&mixer_lock);
}

static IDirectSoundStreamVtbl stream_vtable =
{
	stream_add_reference,
	stream_release,
	stream_get_info,
	stream_get_status,
	stream_process,
	stream_discontinuity,
	stream_flush,
};

/* ---------- the DirectSound object */

struct sdl_direct_sound
{
	ULONG reference_count;
};

static struct sdl_direct_sound direct_sound = { 0 };

HRESULT WINAPI DirectSoundCreate(LPGUID device_id, LPDIRECTSOUND *result, LPUNKNOWN outer)
{
	(void)device_id;
	(void)outer;
	audio_start();
	direct_sound.reference_count++;
	*result = (LPDIRECTSOUND)&direct_sound;
	return DS_OK;
}

ULONG WINAPI IDirectSound_Release(LPDIRECTSOUND sound)
{
	(void)sound;
	return direct_sound.reference_count ? --direct_sound.reference_count : 0;
}

VOID WINAPI DirectSoundDoWork(void)
{
	static unsigned long volume_read_at = (unsigned long)-1;

	/* (audio settings read again when the
	settings change: on the game's thread, not the mixer's, whose lock the
	config's file I/O would hold) */
	if (volume_read_at != config_changes())
	{
		float volume = (float)config_real("audio.volume");
		BOOL room = config_boolean("audio.reverb");
		BOOL stereo_doppler = config_boolean("audio.stereo_doppler");
		BOOL doppler = config_boolean("audio.doppler");
		int resampler = resampler_mode_read();

		volume_read_at = config_changes();
		mixer_lock_take();
		configured_master_volume = volume;
		/* (port: a muted page stays muted, platform_audio_set_muted) */
		master_volume = master_muted ? 0.0f : configured_master_volume;
		reverb_enabled = room;
		stereo_doppler_enabled = stereo_doppler;
		doppler_enabled = doppler;
		resampler_mode = resampler;
		pthread_mutex_unlock(&mixer_lock);
		/* (audio.hrtf: hrtf_enabled, and the binaural rendering's) */
		hrtf_update();
	}
	else if (hrtf_automatic)
	{
		static unsigned long checked_at;
		unsigned long now = (unsigned long)SDL_GetTicks();

		/* (port: the system's output may have moved to headphones, or off
		them) */
		if (now - checked_at > 1000)
		{
			checked_at = now;
			hrtf_update();
		}
	}
	streams_complete_finished();
}

VOID WINAPI DirectSoundUseFullHRTF(void)
{
	/* The measured full HRTF is the default on every output. The user can
	choose otherwise with audio.hrtf; there is no reduced HRTF bank. */
}

HRESULT WINAPI IDirectSound_GetCaps(LPDIRECTSOUND sound, LPDSCAPS caps)
{
	(void)sound;
	memset(caps, 0, sizeof(*caps));
	caps->dwFree2DBuffers = 64;
	caps->dwFree3DBuffers = 64;
	caps->dwFreeBufferSGEs = 2047;
	caps->dwMemoryAllocated = 0;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_GetSpeakerConfig(LPDIRECTSOUND sound, LPDWORD speaker_config)
{
	(void)sound;
	*speaker_config = output_channels > 2 ? DSSPEAKER_ENABLE_AC3 : DSSPEAKER_STEREO;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_DownloadEffectsImage(LPDIRECTSOUND sound, LPCVOID image, DWORD image_size,
	LPCDSEFFECTIMAGELOC image_location, LPDSEFFECTIMAGEDESC *image_description)
{
	(void)sound;
	/* Halo's environment effect is implemented by dsound_xbox_reverb.inl;
	this backend does not execute uploaded MCPX DSP machine code. */
	(void)image;
	(void)image_size;
	(void)image_location;
	if (image_description)
		*image_description = NULL;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CommitDeferredSettings(LPDIRECTSOUND sound) { (void)sound; return DS_OK; }
HRESULT WINAPI IDirectSound_SetMixBinHeadroom(LPDIRECTSOUND sound, DWORD mix_bin_mask, DWORD headroom) { (void)sound; (void)mix_bin_mask; (void)headroom; return DS_OK; }
HRESULT WINAPI IDirectSound_SetI3DL2Listener(LPDIRECTSOUND sound, LPCDSI3DL2LISTENER listener_properties, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_lock_take();
	environment = *listener_properties;
	environment_serial++;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetDistanceFactor(LPDIRECTSOUND sound, FLOAT factor, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_lock_take();
	listener.distance_factor = factor > 0.0f ? factor : 1.0f;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetRolloffFactor(LPDIRECTSOUND sound, FLOAT factor, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_lock_take();
	listener.rolloff_factor = factor >= 0.0f ? factor : 1.0f;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetPosition(LPDIRECTSOUND sound, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_lock_take();
	listener.position[0] = x;
	listener.position[1] = y;
	listener.position[2] = z;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetVelocity(LPDIRECTSOUND sound, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_lock_take();
	listener.velocity[0] = x;
	listener.velocity[1] = y;
	listener.velocity[2] = z;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

static void normalize3(float *vector)
{
	float length = sqrtf(dot3(vector, vector));

	if (length > 1.0e-6f)
	{
		vector[0] /= length;
		vector[1] /= length;
		vector[2] /= length;
	}
}

HRESULT WINAPI IDirectSound_SetOrientation(LPDIRECTSOUND sound, FLOAT x_front, FLOAT y_front, FLOAT z_front,
	FLOAT x_top, FLOAT y_top, FLOAT z_top, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_lock_take();
	listener.front[0] = x_front;
	listener.front[1] = y_front;
	listener.front[2] = z_front;
	listener.top[0] = x_top;
	listener.top[1] = y_top;
	listener.top[2] = z_top;
	normalize3(listener.front);
	normalize3(listener.top);
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CreateSoundStream(LPDIRECTSOUND sound, LPCDSSTREAMDESC description,
	LPDIRECTSOUNDSTREAM *result, LPUNKNOWN outer)
{
	struct sdl_stream *stream = calloc(1, sizeof(*stream));
	const WAVEFORMATEX *format = description->lpwfxFormat;

	(void)sound;
	(void)outer;
	if (!stream)
		return E_OUTOFMEMORY;
	stream->object.lpVtbl = &stream_vtable;
	stream->reference_count = 1;
	stream->callback = description->lpfnCallback;
	stream->context = description->lpvContext;
	stream->adpcm = format && format->wFormatTag == WAVE_FORMAT_XBOX_ADPCM;
	stream->channels = format && format->nChannels == 2 ? 2 : 1;
	stream->sample_rate = format ? format->nSamplesPerSec : 0;
	stream->frequency = stream->sample_rate;
	/* (no cut playing: voice_cut) */
	stream->cut_played = CUT_FADE_FRAMES;
	stream->volume = 1.0f;
	/* DirectSound's default mix bins: a mono voice to both fronts, a
	stereo voice's channels to the front left and right */
	stream->mix_left = 1.0f;
	stream->mix_right = 1.0f;
	stream->surround_bins[0][0] = 1.0f;
	stream->surround_bins[stream->channels - 1][1] = 1.0f;
	stream->has_3d = (description->dwFlags & DSSTREAMCAPS_CTRL3D) != 0;
	stream->mode = DS3DMODE_NORMAL;
	stream->minimum_distance = DS3D_DEFAULTMINDISTANCE;
	stream->maximum_distance = DS3D_DEFAULTMAXDISTANCE;
	/* (port: DirectSound's defaults, no cone, facing +z) */
	stream->cone_inside = stream->cone_outside = 360;
	stream->cone_orientation[2] = 1.0f;
	resampler_reset(stream);
	mixer_lock_take();
	stream->next = streams;
	streams = stream;
	pthread_mutex_unlock(&mixer_lock);
	*result = &stream->object;
	return DS_OK;
}

/* January-era DirectSound exports the game declares itself
(sound_dsound_xbox.c); the XDK 3911 headers no longer carry them */

void __stdcall DirectSoundStopStream(LPDIRECTSOUNDSTREAM stream)
{
	stream_flush(stream);
}

unsigned long __stdcall DirectSoundGetStreamVoiceStatus(LPDIRECTSOUNDSTREAM stream)
{
	struct sdl_stream *record = stream_from_interface(stream);
	unsigned long active;

	mixer_lock_take();
	active = record->packet_count != 0;
	pthread_mutex_unlock(&mixer_lock);
	return active;
}

#define STREAM_SETTER(body) \
	struct sdl_stream *record = stream_from_interface(stream); \
	mixer_lock_take(); \
	body; \
	pthread_mutex_unlock(&mixer_lock); \
	return DS_OK;

HRESULT WINAPI IDirectSoundStream_SetFrequency(LPDIRECTSOUNDSTREAM stream, DWORD frequency)
{
	STREAM_SETTER(record->frequency = frequency ? frequency : record->sample_rate)
}

HRESULT WINAPI IDirectSoundStream_SetVolume(LPDIRECTSOUNDSTREAM stream, LONG volume)
{
	STREAM_SETTER(record->volume = gain_from_millibels(volume))
}

/* the speaker bins of a mask, for 5.1, in the order of its set bits: a stereo
voice sends its left channel to the first, third, ... and its right to the
second, fourth, ... (the game's stereo voices on AC-3 take front left, front
right, back left, back right). The crosstalk, reverb and effect sends are not
speakers. volumes is NULL for 0 dB. */
static void stream_set_surround_bins(struct sdl_stream *record, DWORD mix_bin_mask, const LONG *volumes, BOOL clear)
{
	unsigned long bit, index = 0;

	if (clear)
		memset(record->surround_bins, 0, sizeof(record->surround_bins));
	for (bit = 0; bit < 32; bit++)
	{
		if (!(mix_bin_mask & (1UL << bit)))
			continue;
		if ((1UL << bit) & DSMIXBIN_SPEAKER_MASK)
		{
			record->surround_bins[record->channels == 2 ? index & 1 : 0][bit] =
				volumes ? gain_from_millibels(volumes[index]) : 1.0f;
		}
		index++;
	}
}

HRESULT WINAPI IDirectSoundStream_SetMixBins(LPDIRECTSOUNDSTREAM stream, DWORD mix_bin_mask)
{
	STREAM_SETTER(
		record->mix_left = (mix_bin_mask & DSMIXBIN_FRONT_LEFT) ? 1.0f : 0.0f;
		record->mix_right = (mix_bin_mask & DSMIXBIN_FRONT_RIGHT) ? 1.0f : 0.0f;
		stream_set_surround_bins(record, mix_bin_mask, NULL, TRUE))
}

/* volumes come in the order of the set bits of the mask */
HRESULT WINAPI IDirectSoundStream_SetMixBinVolumes(LPDIRECTSOUNDSTREAM stream, DWORD mix_bin_mask, const LONG *volumes)
{
	struct sdl_stream *record = stream_from_interface(stream);
	unsigned long bit, index = 0;

	mixer_lock_take();
	for (bit = 0; bit < 32; bit++)
	{
		if (!(mix_bin_mask & (1UL << bit)))
			continue;
		if ((1UL << bit) == DSMIXBIN_FRONT_LEFT)
			record->mix_left = gain_from_millibels(volumes[index]);
		else if ((1UL << bit) == DSMIXBIN_FRONT_RIGHT)
			record->mix_right = gain_from_millibels(volumes[index]);
		index++;
	}
	stream_set_surround_bins(record, mix_bin_mask, volumes, FALSE);
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundStream_SetMode(LPDIRECTSOUNDSTREAM stream, DWORD mode, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->mode = mode)
}

HRESULT WINAPI IDirectSoundStream_SetPosition(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->position[0] = x; record->position[1] = y; record->position[2] = z)
}

HRESULT WINAPI IDirectSoundStream_SetMinDistance(LPDIRECTSOUNDSTREAM stream, FLOAT distance, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->minimum_distance = distance)
}

HRESULT WINAPI IDirectSoundStream_SetMaxDistance(LPDIRECTSOUNDSTREAM stream, FLOAT distance, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->maximum_distance = distance)
}

HRESULT WINAPI IDirectSoundStream_SetI3DL2Source(LPDIRECTSOUNDSTREAM stream, LPCDSI3DL2BUFFER source, DWORD apply)
{
	LONG direct, direct_hf, room, room_hf;

	(void)apply;
	/* the direct path: at low frequencies obstruction and occlusion take
	their LF ratio of their high frequency levels, which (with the source's
	own) muffle it above the HF reference */
	direct = source->lDirect +
		(LONG)(source->Obstruction.lHFLevel * source->Obstruction.flLFRatio) +
		(LONG)(source->Occlusion.lHFLevel * source->Occlusion.flLFRatio);
	if (direct > 0)
		direct = 0;
	direct_hf = source->lDirect + source->lDirectHF + source->Obstruction.lHFLevel + source->Occlusion.lHFLevel;
	/* the room send: occlusion lowers and muffles it as it does the direct
	path; obstruction does not lower it, but its muffling (its high
	frequency level over its LF ratio's part) reaches it too. The Xbox
	muffles a voice once, before its mix bins, the reverb's among them: an
	Xbox recording of a voice obstructed by -1200 (LF ratio 0) in a hall is
	the recording of it occluded by -1200, sample for sample (the
	difference 38 dB down, the noise's), the reverb's tail as muffled as
	the direct sound */
	room = source->lRoom + (LONG)(source->Occlusion.lHFLevel * source->Occlusion.flLFRatio);
	/* the source's own Room HF does nothing: recorded on an Xbox (the test
	bed's ninth run), the cave's reverb of a voice at a Room HF of -1204
	was as loud at 10 kHz as at 250 Hz, and one at a Room and Room HF of
	-1204 (the game's for most of its 3D sounds) or -2000 was 12 or 20 dB
	down at every frequency, to 0.1 dB; the listener's Room HF (each
	environment's) and occlusion's muffling reach the send as before. Taken
	as the I3DL2 level it is, it muffled the reverberation of nearly every
	3D sound by 12 dB more at the HF reference */
	room_hf = source->lRoom + source->Occlusion.lHFLevel +
		(source->Obstruction.lHFLevel - (LONG)(source->Obstruction.lHFLevel * source->Obstruction.flLFRatio));
	{
		STREAM_SETTER(
			record->direct = direct;
			record->direct_hf = direct_hf;
			record->room = room;
			record->room_hf = room_hf;
			record->room_rolloff_factor = source->flRoomRolloffFactor)
	}
}

HRESULT WINAPI IDirectSoundStream_Pause(LPDIRECTSOUNDSTREAM stream, DWORD pause)
{
	STREAM_SETTER(record->paused = pause == DSSTREAMPAUSE_PAUSE)
}

HRESULT WINAPI IDirectSoundStream_SetVelocity(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->velocity[0] = x; record->velocity[1] = y; record->velocity[2] = z)
}

/* (port, ChupathingyCE's: the sound cone, voice_cone_gain) */
HRESULT WINAPI IDirectSoundStream_SetConeAngles(LPDIRECTSOUNDSTREAM stream, DWORD inside, DWORD outside, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->cone_inside = inside; record->cone_outside = outside)
}
HRESULT WINAPI IDirectSoundStream_SetConeOrientation(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->cone_orientation[0] = x; record->cone_orientation[1] = y; record->cone_orientation[2] = z)
}
HRESULT WINAPI IDirectSoundStream_SetConeOutsideVolume(LPDIRECTSOUNDSTREAM stream, LONG volume, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->cone_outside_volume = volume)
}

/* ---------- buffers */

HRESULT WINAPI DirectSoundCreateBuffer(LPCDSBUFFERDESC description, LPDIRECTSOUNDBUFFER *result)
{
	struct sdl_buffer *buffer;
	const WAVEFORMATEX *format;

	if (!result)
		return DSERR_INVALIDCALL;
	*result = NULL;
	if (!description || !description->lpwfxFormat)
		return DSERR_INVALIDCALL;
	format = description->lpwfxFormat;
	if ((format->nChannels != 1 && format->nChannels != 2) || !format->nSamplesPerSec ||
		(format->wFormatTag != WAVE_FORMAT_XBOX_ADPCM &&
			(format->wFormatTag != WAVE_FORMAT_PCM || format->wBitsPerSample != 16)))
		return DSERR_UNSUPPORTED;
	buffer = calloc(1, sizeof(*buffer));
	if (!buffer)
		return E_OUTOFMEMORY;
	buffer->reference_count = 1;
	buffer->channels = format->nChannels;
	buffer->rate = format->nSamplesPerSec;
	buffer->adpcm = format->wFormatTag == WAVE_FORMAT_XBOX_ADPCM;
	buffer->block_bytes = buffer->channels * (buffer->adpcm ? XBOX_ADPCM_BLOCK_BYTES : 2);
	buffer->block_frames = buffer->adpcm ? XBOX_ADPCM_BLOCK_SAMPLES : 1;
	buffer->volume = (float)config_real("audio.effects_volume");
	pthread_mutex_lock(&mixer_lock);
	buffer->next = buffers;
	buffers = buffer;
	pthread_mutex_unlock(&mixer_lock);
	*result = (LPDIRECTSOUNDBUFFER)buffer;
	audio_start();
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CreateSoundBuffer(LPDIRECTSOUND sound, LPCDSBUFFERDESC description,
	LPDIRECTSOUNDBUFFER *result, LPUNKNOWN outer)
{
	(void)sound;
	(void)outer;
	return DirectSoundCreateBuffer(description, result);
}

ULONG WINAPI IDirectSoundBuffer_Release(LPDIRECTSOUNDBUFFER buffer)
{
	struct sdl_buffer *record = (struct sdl_buffer *)buffer, **link;
	ULONG count;

	if (!record)
		return 0;
	pthread_mutex_lock(&mixer_lock);
	count = --record->reference_count;
	if (!count)
	{
		for (link = &buffers; *link && *link != record; link = &(*link)->next)
			;
		if (*link)
			*link = record->next;
		free(record->samples);
		free(record);
	}
	pthread_mutex_unlock(&mixer_lock);
	return count;
}

HRESULT WINAPI IDirectSoundBuffer_SetBufferData(LPDIRECTSOUNDBUFFER buffer, LPVOID data, DWORD size)
{
	struct sdl_buffer *record = (struct sdl_buffer *)buffer;
	short *samples = NULL;
	unsigned long frames = 0;

	if (!record || (!data && size) || size % record->block_bytes ||
		size / record->block_bytes > (size_t)-1 / (record->block_frames * record->channels * sizeof(short)))
		return DSERR_INVALIDCALL;
	if (size)
	{
		samples = record->adpcm ? decode_adpcm(data, size, record->channels, &frames) :
			decode_pcm(data, size, record->channels, &frames);
		if (!samples)
			return E_OUTOFMEMORY;
	}
	pthread_mutex_lock(&mixer_lock);
	free(record->samples);
	record->samples = samples;
	record->frames = record->loop_end = frames;
	record->loop_start = 0;
	record->position = 0;
	record->playing = FALSE;
	record->current_volume = 0;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_Play(LPDIRECTSOUNDBUFFER buffer, DWORD reserved1, DWORD reserved2, DWORD flags)
{
	struct sdl_buffer *record = (struct sdl_buffer *)buffer;

	(void)reserved1;
	(void)reserved2;
	if (!record)
		return DSERR_INVALIDCALL;
	pthread_mutex_lock(&mixer_lock);
	record->looping = (flags & DSBPLAY_LOOPING) != 0;
	if (record->looping && record->position >= record->loop_end)
		record->position = record->loop_start;
	record->playing = record->frames != 0;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_Stop(LPDIRECTSOUNDBUFFER buffer)
{
	struct sdl_buffer *record = (struct sdl_buffer *)buffer;

	if (!record)
		return DSERR_INVALIDCALL;
	pthread_mutex_lock(&mixer_lock);
	record->playing = FALSE;
	record->current_volume = 0;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_SetCurrentPosition(LPDIRECTSOUNDBUFFER buffer, DWORD play_cursor)
{
	struct sdl_buffer *record = (struct sdl_buffer *)buffer;
	unsigned long frame;

	if (!record || play_cursor % record->block_bytes)
		return DSERR_INVALIDCALL;
	pthread_mutex_lock(&mixer_lock);
	if (play_cursor / record->block_bytes > record->frames / record->block_frames)
	{
		pthread_mutex_unlock(&mixer_lock);
		return DSERR_INVALIDCALL;
	}
	frame = play_cursor / record->block_bytes * record->block_frames;
	if (frame >= record->frames && (frame || record->frames))
	{
		pthread_mutex_unlock(&mixer_lock);
		return DSERR_INVALIDCALL;
	}
	record->position = record->looping && frame >= record->loop_end ? record->loop_start : frame;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_SetLoopRegion(LPDIRECTSOUNDBUFFER buffer, DWORD loop_start, DWORD loop_length)
{
	struct sdl_buffer *record = (struct sdl_buffer *)buffer;
	unsigned long start, length;

	if (!record || loop_start % record->block_bytes || loop_length % record->block_bytes || !loop_length)
		return DSERR_INVALIDCALL;
	pthread_mutex_lock(&mixer_lock);
	if (loop_start / record->block_bytes > record->frames / record->block_frames ||
		loop_length / record->block_bytes > record->frames / record->block_frames)
	{
		pthread_mutex_unlock(&mixer_lock);
		return DSERR_INVALIDCALL;
	}
	start = loop_start / record->block_bytes * record->block_frames;
	length = loop_length / record->block_bytes * record->block_frames;
	if (start >= record->frames || length > record->frames - start)
	{
		pthread_mutex_unlock(&mixer_lock);
		return DSERR_INVALIDCALL;
	}
	record->loop_start = start;
	record->loop_end = start + length;
	if (record->looping && record->position >= record->loop_end)
		record->position = start;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_SetPitch(LPDIRECTSOUNDBUFFER buffer, LONG pitch)
{
	struct sdl_buffer *record = (struct sdl_buffer *)buffer;
	double rate;

	if (!record)
		return DSERR_INVALIDCALL;
	/* Xbox pitch is absolute: zero means 48 kHz, with 4096 units per octave
	(xboxdevwiki.net/APU, Pitch calculation). It is not a multiplier of the
	buffer's original sample rate. Bound the exponent before applying the
	same frequency limits as the game's stream voices. */
	if (pitch < -32768) pitch = -32768;
	if (pitch > 32767) pitch = 32767;
	rate = 48000.0 * pow(2.0, (double)pitch / 4096.0);
	if (rate < DSBFREQUENCY_MIN) rate = DSBFREQUENCY_MIN;
	if (rate > DSBFREQUENCY_MAX) rate = DSBFREQUENCY_MAX;
	pthread_mutex_lock(&mixer_lock);
	record->rate = rate;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_SetVolume(LPDIRECTSOUNDBUFFER buffer, LONG volume)
{
	struct sdl_buffer *record = (struct sdl_buffer *)buffer;
	float gain;

	if (!record)
		return DSERR_INVALIDCALL;
	if (volume > 0) volume = 0;
	gain = gain_from_millibels(volume) * (float)config_real("audio.effects_volume");
	pthread_mutex_lock(&mixer_lock);
	record->volume = gain;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

/* ---------- voice chat's voices (port/linux/src/voice.c)

Each speaker heard in voice chat plays through a voice of its own here,
beside the game's: mono 16-bit PCM at the output rate (no resampling), fed
20 ms at a time as it is decoded. A voice heard from where its speaker is
(a proximity voice) is a 3D voice placed at the speaker, so it takes the
mixer's distance rolloff, its pan or Headphone 3D (audio.hrtf) and the
reverb's send like the game's own sounds. Any other plays in the middle, at
its gain. With audio.voice_positional off a proximity voice is panned and
faded by distance alone (dsound_voice_place). Called on the game's thread,
as the game's streams are. */

#ifdef HALO_PORT_VOICE_AUDIO

void *dsound_voice_open(void);
void dsound_voice_close(void *voice);
int dsound_voice_submit(void *voice, const short *samples, unsigned long frames);
unsigned long dsound_voice_queued(void *voice);
void dsound_voice_place(void *voice, int mode, const float *position, float minimum_distance, float maximum_distance,
	float gain);

void *dsound_voice_open(void)
{
	WAVEFORMATEX format;
	DSSTREAMDESC description;
	LPDIRECTSOUNDSTREAM stream = NULL;
	struct sdl_stream *record;

	memset(&format, 0, sizeof(format));
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = 1;
	format.nSamplesPerSec = OUTPUT_RATE;
	format.wBitsPerSample = 16;
	format.nBlockAlign = 2;
	format.nAvgBytesPerSec = OUTPUT_RATE * 2;
	memset(&description, 0, sizeof(description));
	description.dwFlags = DSSTREAMCAPS_CTRL3D;
	description.lpwfxFormat = &format;
	if (IDirectSound_CreateSoundStream(NULL, &description, &stream, NULL) != DS_OK || !stream)
		return NULL;
	record = stream_from_interface(stream);
	mixer_lock_take();
	record->mode = DS3DMODE_DISABLE;
	pthread_mutex_unlock(&mixer_lock);
	return stream;
}

void dsound_voice_close(void *voice)
{
	if (voice)
		stream_release((IDirectSoundStream *)voice);
}

int dsound_voice_submit(void *voice, const short *samples, unsigned long frames)
{
	XMEDIAPACKET packet;

	if (!voice || !samples || !frames)
		return 0;
	memset(&packet, 0, sizeof(packet));
	packet.pvBuffer = (LPVOID)samples;
	packet.dwMaxSize = frames * 2;
	return stream_process((IDirectSoundStream *)voice, &packet, NULL) == S_OK;
}

/* the frames waiting to play */
unsigned long dsound_voice_queued(void *voice)
{
	struct sdl_stream *stream = stream_from_interface(voice);
	unsigned long position, frames = 0;
	BOOL first = TRUE;

	if (!voice)
		return 0;
	mixer_lock_take();
	for (position = 0; position < stream->packet_count; position++)
	{
		struct voice_packet *packet = &stream->packets[(stream->packet_head + position) % MAXIMUM_STREAM_PACKETS];

		if (packet->finished)
			continue;
		frames += packet->frames - (first && stream->cursor < packet->frames ? stream->cursor : 0);
		first = FALSE;
	}
	pthread_mutex_unlock(&mixer_lock);
	return frames;
}

/* where a voice plays from: mode 0 in the middle; 1 a 3D voice at position
(the game's world units, x y z) between the distances; 2 from position by
gain and pan alone (OpenCE's way: full within the minimum distance, then
down to a fifth at the maximum, panned by the side it is on); and its gain */
void dsound_voice_place(void *voice, int mode, const float *position, float minimum_distance, float maximum_distance,
	float gain)
{
	struct sdl_stream *stream = stream_from_interface(voice);

	if (!voice)
		return;
	mixer_lock_take();
	stream->volume = gain < 0.0f ? 0.0f : gain;
	stream->mix_left = stream->mix_right = 1.0f;
	if (mode && position)
	{
		/* (DirectSound's space: y up, as the game sets it, sound_dsound_xbox.c) */
		stream->position[0] = position[0];
		stream->position[1] = position[2];
		stream->position[2] = position[1];
		stream->minimum_distance = minimum_distance > 0.0f ? minimum_distance : 1.0f;
		stream->maximum_distance = maximum_distance > stream->minimum_distance ? maximum_distance :
			stream->minimum_distance;
		stream->direct = stream->direct_hf = 0;
		stream->room = stream->room_hf = 0;
	}
	if (mode == 1 && position)
	{
		stream->mode = DS3DMODE_NORMAL;
	}
	else if (mode == 2 && position)
	{
		float side, up, ahead, distance, fade, pan;

		stream->mode = DS3DMODE_NORMAL;
		voice_direction(stream, &side, &up, &ahead);
		stream->mode = DS3DMODE_DISABLE;
		distance = sqrtf(side * side + up * up + ahead * ahead);
		fade = distance <= stream->minimum_distance ? 1.0f :
			1.0f - 0.8f * clamp_unit((distance - stream->minimum_distance) /
				(stream->maximum_distance - stream->minimum_distance + 1.0e-3f));
		pan = distance > 1.0e-3f ? 0.8f * side / distance : 0.0f;
		stream->mix_left = fade * sqrtf(0.5f * (1.0f - pan)) * 1.41421356f;
		stream->mix_right = fade * sqrtf(0.5f * (1.0f + pan)) * 1.41421356f;
	}
	else
	{
		stream->mode = DS3DMODE_DISABLE;
	}
	pthread_mutex_unlock(&mixer_lock);
}

#endif
