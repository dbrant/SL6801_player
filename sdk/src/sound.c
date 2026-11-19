/*
 * Sound effects on /dev/audio0: a small tone synthesizer running in its own task.
 *
 * The UI task must not touch /dev/audio0: opening, writing to or closing it from LVGL timers or
 * events hangs or reboots the player, often but not always. So the sound task owns the device,
 * as the music player's task does: it opens it (with the video player's amplifier callback),
 * keeps the ring full with blocking writes, and on stop plays the ring out and closes it.
 * fw_sound_play hands requests to the task through a small single-producer queue, and
 * fw_sound_close only signals the task; a UI timer joins it once it is done (which then does not
 * block) and frees the state. The state lives on the firmware heap, not the 40 KB LVGL heap.
 */
#include "sdk.h"

#define AUDIO_DEV "/dev/audio0"
#define RATE 44100
#define PERIOD 256          /* frames per write */
#define PERIODS 8           /* ring size: about 46 ms */
#define TASK_PRIORITY 6     /* same as the video player's audio task */
#define TASK_STACK 0x800
#define VOICES 4
#define REQUESTS 8          /* power of two */
#define INC_PER_HZ 97392u   /* phase increment for 1 Hz: 2^32 / 44100 */
#define ATTACK 64           /* samples of fade-in, avoids clicks */
#define HEAP_RESERVE 0x8000 /* leave this much of the firmware heap for the firmware */
#define DRAIN_TIMEOUT_MS 300 /* the ring holds about 46 ms */
#define REAP_MS 20

typedef struct {
	const fw_tone_t *tone;
	uint8_t left; /* tones left, including the current one; 0 = idle */
	uint32_t pos, len;
	uint32_t phase, inc;
	int32_t dinc;
	uint32_t amp;
	uint32_t started;
} voice_t;

typedef struct {
	const fw_tone_t *tones;
	uint8_t count;
} request_t;

struct fw_sound {
	void *dev;
	void *gain;
	void *task;
	void *stop;
	volatile bool done; /* set by the task as its last action */
	uint8_t reap_ticks;
	request_t req[REQUESTS];
	volatile uint8_t req_head; /* written by fw_sound_play */
	volatile uint8_t req_tail; /* written by the task */
	uint32_t serial, lfsr;
	voice_t voice[VOICES];
	int16_t buf[PERIOD * 2] __attribute__((aligned(4))); /* for the ROM's word copies */
};

static void start_tone(voice_t *v)
{
	const fw_tone_t *t = v->tone;
	v->len = (uint32_t)t->ms * (RATE / 100) / 10;
	if (!v->len)
		v->len = 1;
	v->pos = 0;
	v->inc = t->f0 * INC_PER_HZ;
	v->dinc = ((int32_t)(t->f1 * INC_PER_HZ) - (int32_t)v->inc) / (int32_t)v->len;
	v->amp = (uint32_t)t->vol * 48;
}

static void start_voice(fw_sound_t *s, const fw_tone_t *tones, uint8_t count)
{
	voice_t *v = &s->voice[0];
	for (int k = 0; k < VOICES; k++) { /* an idle voice, else the oldest */
		voice_t *c = &s->voice[k];
		if (!c->left) {
			v = c;
			break;
		}
		if (c->started < v->started)
			v = c;
	}
	v->tone = tones;
	v->left = count;
	v->phase = 0;
	v->started = ++s->serial;
	start_tone(v);
}

static void take_requests(fw_sound_t *s)
{
	while (s->req_tail != s->req_head) {
		__sync_synchronize();
		request_t *r = &s->req[s->req_tail];
		start_voice(s, r->tones, r->count);
		__sync_synchronize();
		s->req_tail = (uint8_t)((s->req_tail + 1) & (REQUESTS - 1));
	}
}

static int16_t clamp16(int32_t x)
{
	return (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
}

/* Starts queued sounds, mixes all voices into the first PERIOD samples of s->buf, then expands
 * them to stereo in place. */
static void render(fw_sound_t *s)
{
	take_requests(s);
	int16_t *out = s->buf;
	memset(out, 0, sizeof(s->buf));
	for (int k = 0; k < VOICES; k++) {
		voice_t *v = &s->voice[k];
		for (int i = 0; i < PERIOD && v->left; i++) {
			if (v->pos >= v->len) {
				if (--v->left == 0)
					break;
				v->tone++;
				start_tone(v);
			}
			uint32_t env = v->amp * (v->len - v->pos) / v->len;
			if (v->pos < ATTACK)
				env = env * v->pos / ATTACK;
			uint32_t prev = v->phase;
			v->phase += v->inc;
			v->inc += (uint32_t)v->dinc;
			v->pos++;
			int32_t smp;
			switch (v->tone->wave) {
			case FW_WAVE_TRIANGLE: {
				int32_t t = (int32_t)(v->phase >> 16);
				int32_t tri = t < 32768 ? t * 2 - 32768 : (65535 - t) * 2 - 32768;
				smp = tri * (int32_t)env >> 15;
				break;
			}
			case FW_WAVE_NOISE:
				if (v->phase < prev) { /* one step per period of f */
					uint32_t x = s->lfsr;
					x ^= x << 13;
					x ^= x >> 17;
					x ^= x << 5;
					s->lfsr = x;
				}
				smp = (s->lfsr & 1) ? (int32_t)env : -(int32_t)env;
				break;
			default:
				smp = (v->phase & 0x80000000u) ? (int32_t)env : -(int32_t)env;
				break;
			}
			out[i] = clamp16(out[i] + smp);
		}
	}
	rom_pcm_mono_to_stereo(out, out, PERIOD);
}

/* Plays the ring out, then closes the device. */
static void close_device(fw_sound_t *s)
{
	/* Overwrite the whole ring with silence: closing the device plays one more period of it. */
	memset(s->buf, 0, sizeof(s->buf));
	for (int i = 0; i < PERIODS; i++)
		fw_dev_write(s->dev, s->buf, PERIOD, 0);
	/* Wait until the DMA interrupt has played the ring dry (see FW_AUDIO_HW). */
	for (int ms = 0; ms < DRAIN_TIMEOUT_MS; ms += 5) {
		const uint8_t *hw = FW_AUDIO_HW;
		if (!fw_is_ram(hw) || hw[FW_AUDIO_HW_STATE] != FW_AUDIO_PLAYING)
			break;
		fw_os_sleep(5);
	}
	fw_dev_close(&s->dev);
}

/* Owns the device from open to close; see the top of this file. */
static void sound_task(void *arg)
{
	fw_sound_t *s = arg;
	static const uint32_t params[10] = {2, 2, RATE, PERIOD, PERIODS, 0, 0, 0, FW_AUDIO_PA_CB, 0};
	s->dev = fw_dev_open(AUDIO_DEV, 0, params);
	if (s->dev) {
		while (fw_sem_take(&s->stop, 0) == -1) {
			render(s);
			if (s->gain)
				fw_gain_apply(s->gain, fw_audio_volume() & 0xFF, s->buf, PERIOD);
			fw_dev_write(s->dev, s->buf, PERIOD, 0); /* waits while the ring is full */
		}
		close_device(s);
	}
	s->done = true;
}

static void release(fw_sound_t *s)
{
	if (s->gain)
		fw_gain_free(s->gain);
	if (s->stop)
		fw_sem_delete(&s->stop);
	fw_os_free(s);
}

fw_sound_t *fw_sound_open(void)
{
	if (fw_dev_busy(AUDIO_DEV) || fw_os_heap_free() < sizeof(fw_sound_t) + HEAP_RESERVE)
		return NULL;
	fw_sound_t *s = fw_os_calloc(1, sizeof(*s));
	if (!fw_is_ram(s))
		return NULL;
	s->lfsr = 0x2545F491u;
	s->gain = fw_gain_create(0, RATE, 2, 2);
	if (!fw_is_ram(s->gain))
		s->gain = NULL;
	s->stop = fw_sem_create();
	if (!fw_is_ram(s->stop)) {
		s->stop = NULL;
		release(s);
		return NULL;
	}
	s->task = fw_task_create(sound_task, s, "sfx", TASK_PRIORITY, TASK_STACK);
	if (!fw_is_ram(s->task)) {
		release(s);
		return NULL;
	}
	return s;
}

void fw_sound_play(fw_sound_t *s, const fw_tone_t *tones, uint8_t count)
{
	if (!s || !count)
		return;
	uint8_t next = (uint8_t)((s->req_head + 1) & (REQUESTS - 1));
	if (next == s->req_tail)
		return; /* queue full: drop */
	s->req[s->req_head].tones = tones;
	s->req[s->req_head].count = count;
	__sync_synchronize();
	s->req_head = next;
}

/* UI timer: finishes closing once the task is done. It joins one tick after seeing done, when the
 * task has long returned, so the join does not block. */
static void reap(lv_timer_t *t)
{
	fw_sound_t *s = t->user_data;
	if (!s->done || s->reap_ticks++ == 0)
		return;
	fw_task_join(s->task);
	release(s);
	lv_timer_del(t);
}

void fw_sound_close(fw_sound_t *s)
{
	if (!s)
		return;
	fw_sem_give(&s->stop);
	lv_timer_create(reap, REAP_MS, s); /* if this fails, s and the task stay allocated */
}
