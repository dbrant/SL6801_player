/*
 * Microphone capture from /dev/audio1, kept in a ring by its own task.
 *
 * As with the sound module, a task owns the device from open to close (reads block until a
 * period is in). fw_mic_close only signals the task; a UI timer joins it once it is done and
 * frees the state, which lives on the firmware heap.
 */
#include "sdk.h"

#define MIC_DEV "/dev/audio1"
#define PERIOD 128 /* frames per read; FW_MIC_RING is a multiple of it */
#define SOURCE 7
#define TASK_PRIORITY 6
#define TASK_STACK 0x800
#define HEAP_RESERVE 0x8000
#define REAP_MS 20

struct fw_mic {
	void *dev;
	void *task;
	void *stop;
	volatile bool done; /* set by the task as its last action */
	uint8_t reap_ticks;
	volatile uint32_t total; /* frames captured so far */
	int16_t ring[FW_MIC_RING * 2];
};

static void mic_task(void *arg)
{
	fw_mic_t *m = arg;
	static const uint32_t params[10] = {2, 2, FW_MIC_RATE, PERIOD, 2, 0, 0, SOURCE << 8, 0, 0};
	m->dev = fw_dev_open(MIC_DEV, 0, params);
	if (m->dev) {
		while (fw_sem_take(&m->stop, 0) == -1) {
			uint32_t pos = m->total % FW_MIC_RING;
			if (fw_dev_read(m->dev, &m->ring[pos * 2], PERIOD, 0) != PERIOD) {
				fw_os_sleep(10);
				continue;
			}
			__sync_synchronize();
			m->total += PERIOD;
		}
		fw_dev_close(&m->dev);
	}
	m->done = true;
}

static void release(fw_mic_t *m)
{
	if (m->stop)
		fw_sem_delete(&m->stop);
	fw_os_free(m);
}

fw_mic_t *fw_mic_open(void)
{
	if (fw_dev_busy(MIC_DEV) || fw_os_heap_free() < sizeof(fw_mic_t) + HEAP_RESERVE)
		return NULL;
	fw_mic_t *m = fw_os_calloc(1, sizeof(*m));
	if (!fw_is_ram(m))
		return NULL;
	m->stop = fw_sem_create();
	if (!fw_is_ram(m->stop)) {
		m->stop = NULL;
		release(m);
		return NULL;
	}
	m->task = fw_task_create(mic_task, m, "mic", TASK_PRIORITY, TASK_STACK);
	if (!fw_is_ram(m->task)) {
		release(m);
		return NULL;
	}
	return m;
}

/* The task fills the period after the newest one, so the newest half of the ring stays intact
 * while it is copied. */
uint32_t fw_mic_latest(fw_mic_t *m, int16_t *dst, uint32_t frames)
{
	if (!m)
		return 0;
	uint32_t total = m->total;
	__sync_synchronize();
	if (frames > FW_MIC_RING / 2)
		frames = FW_MIC_RING / 2;
	if (frames > total)
		frames = total;
	uint32_t pos = (total - frames) % FW_MIC_RING;
	for (uint32_t i = 0; i < frames; i++) {
		dst[2 * i] = m->ring[2 * pos];
		dst[2 * i + 1] = m->ring[2 * pos + 1];
		pos = (pos + 1) % FW_MIC_RING;
	}
	return frames;
}

/* UI timer: finishes closing once the task is done (see fw_sound_close). */
static void reap(lv_timer_t *t)
{
	fw_mic_t *m = t->user_data;
	if (!m->done || m->reap_ticks++ == 0)
		return;
	fw_task_join(m->task);
	release(m);
	lv_timer_del(t);
}

void fw_mic_close(fw_mic_t *m)
{
	if (!m)
		return;
	fw_sem_give(&m->stop);
	lv_timer_create(reap, REAP_MS, m); /* if this fails, m and the task stay allocated */
}
