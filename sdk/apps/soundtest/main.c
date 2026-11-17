/*
 * "Sound test" entry in the APP menu: runs steps of the audio device's lifecycle one at a time
 * from a UI timer and logs each one on screen before running it, so after a hang the last line
 * names the step that hung. Left/Right choose a sequence, Center runs it, Back closes the app when
 * nothing is running. Each result shows the time taken and a value (see run_op).
 * Sequences 1-4 call the device from the UI task, which is known to hang or crash (see the
 * README's Audio section); sequence 5 uses the SDK sound module, which must pass.
 */
#include "sdk.h"

#define RATE 44100
#define PERIOD 256
#define PERIODS 8
#define TICK_MS 300
#define MAX_POLLS 20
#define LOG_LINES 9
#define LOG_SIZE 400

enum {
	OP_OPEN,   /* value: device node */
	OP_WRITE,  /* 16 periods of a beep from the UI task; value: hardware state after */
	OP_DRAIN,  /* waits for the hardware state to leave "playing"; value: state */
	OP_CLOSE,  /* value: hardware state before closing */
	OP_SEM,    /* value: semaphore */
	OP_TASK,   /* task that plays a tone while the device is open; value: task */
	OP_WAIT,   /* about 1 s; value: task loop count */
	OP_GIVE,   /* signals the task to stop; value: loop count */
	OP_JOIN,   /* value: loop count */
	OP_SEMDEL, /* value: result */
	OP_SND_OPEN,  /* fw_sound_open; value: handle */
	OP_SND_PLAY,  /* value: none */
	OP_SND_CLOSE, /* value: none */
	OP_END,
};

static const char *const op_names[] = {
	"open", "write 16", "drain", "close", "sem create", "task create", "wait 1 s", "sem give", "join",
	"sem delete", "sound open", "sound play", "sound close",
};

static const fw_tone_t beep[] = {{660, 990, 150, FW_WAVE_SQUARE, 120}};

typedef struct {
	const char *name;
	uint8_t ops[24];
} sequence_t;

static const sequence_t sequences[] = {
	{"Reopen: device twice, no task",
	 {OP_OPEN, OP_WRITE, OP_DRAIN, OP_CLOSE, OP_OPEN, OP_WRITE, OP_DRAIN, OP_CLOSE, OP_END}},
	{"Task: like the games",
	 {OP_SEM, OP_OPEN, OP_TASK, OP_WAIT, OP_GIVE, OP_JOIN, OP_DRAIN, OP_CLOSE, OP_SEMDEL, OP_END}},
	{"Open/close twice, no sound", {OP_OPEN, OP_CLOSE, OP_OPEN, OP_CLOSE, OP_END}},
	{"Task twice",
	 {OP_SEM, OP_OPEN, OP_TASK, OP_WAIT, OP_GIVE, OP_JOIN, OP_DRAIN, OP_CLOSE, OP_SEMDEL, OP_SEM, OP_OPEN,
	  OP_TASK, OP_WAIT, OP_GIVE, OP_JOIN, OP_DRAIN, OP_CLOSE, OP_SEMDEL, OP_END}},
	{"Sound module 3 times",
	 {OP_SND_OPEN, OP_SND_PLAY, OP_WAIT, OP_SND_CLOSE, OP_WAIT, OP_SND_OPEN, OP_SND_PLAY, OP_WAIT,
	  OP_SND_CLOSE, OP_WAIT, OP_SND_OPEN, OP_SND_PLAY, OP_WAIT, OP_SND_CLOSE, OP_WAIT, OP_END}},
};
#define SEQUENCES (sizeof(sequences) / sizeof(sequences[0]))

/* Shared with the task; on the firmware heap like sound.c's state. */
typedef struct {
	void *stop;
	void *task;
	void *dev;
	volatile uint32_t loops;
	uint32_t phase;
	int16_t buf[PERIOD * 2];
} diag_t;

typedef struct {
	fw_overlay_t *ov;
	lv_obj_t *log;
	lv_timer_t *timer;
	diag_t *d;
	fw_sound_t *snd;
	uint8_t seq, pos;
	bool running, announced;
	uint8_t polls;
	char text[LOG_SIZE];
} app_t;

enum { DONE, AGAIN, FAIL };

static void log_add(app_t *app, const char *s)
{
	size_t len = strlen(app->text), add = strlen(s);
	if (len + add + 1 > LOG_SIZE)
		len = app->text[0] = 0;
	memcpy(app->text + len, s, add + 1);
	int lines = 0;
	for (size_t i = 0; app->text[i]; i++)
		lines += app->text[i] == '\n';
	while (lines >= LOG_LINES) { /* drop the first line */
		size_t cut = 0;
		while (app->text[cut] != '\n')
			cut++;
		len = strlen(app->text);
		memmove(app->text, app->text + cut + 1, len - cut);
		lines--;
	}
	lv_label_set_text(app->log, app->text);
}

static void fill(diag_t *d, bool tone)
{
	for (int i = 0; i < PERIOD; i++) {
		int16_t v = 0;
		if (tone) {
			d->phase += 440u * 97392u;
			v = (d->phase & 0x80000000u) ? 1500 : -1500;
		}
		d->buf[2 * i] = d->buf[2 * i + 1] = v;
	}
}

static void task_entry(void *arg)
{
	diag_t *d = arg;
	while (fw_sem_take(&d->stop, 0) == -1) {
		fill(d, true);
		fw_dev_write(d->dev, d->buf, PERIOD, 0);
		d->loops++;
	}
	fill(d, false);
	for (int i = 0; i < PERIODS; i++)
		fw_dev_write(d->dev, d->buf, PERIOD, 0);
}

static uint8_t hw_state(void)
{
	const uint8_t *hw = FW_AUDIO_HW;
	return fw_is_ram(hw) ? hw[FW_AUDIO_HW_STATE] : 0xFF;
}

static int run_op(app_t *app, uint8_t op, uint32_t *val)
{
	diag_t *d = app->d;
	switch (op) {
	case OP_OPEN: {
		static const uint32_t params[10] = {2, 2, RATE, PERIOD, PERIODS, 0, 0, 0, FW_AUDIO_PA_CB, 0};
		d->dev = fw_dev_open("/dev/audio0", 0, params);
		*val = (uint32_t)(uintptr_t)d->dev;
		return d->dev ? DONE : FAIL;
	}
	case OP_WRITE:
		for (int i = 0; i < 16; i++) {
			fill(d, true);
			fw_dev_write(d->dev, d->buf, PERIOD, 0);
		}
		*val = hw_state();
		return DONE;
	case OP_DRAIN:
		*val = hw_state();
		return *val == FW_AUDIO_PLAYING && ++app->polls < MAX_POLLS ? AGAIN : DONE;
	case OP_CLOSE:
		*val = hw_state();
		fw_dev_close(&d->dev);
		return DONE;
	case OP_SEM:
		d->stop = fw_sem_create();
		*val = (uint32_t)(uintptr_t)d->stop;
		return fw_is_ram(d->stop) ? DONE : FAIL;
	case OP_TASK:
		d->loops = 0;
		d->task = fw_task_create(task_entry, d, "sfxtest", 6, 0x800);
		*val = (uint32_t)(uintptr_t)d->task;
		return fw_is_ram(d->task) ? DONE : FAIL;
	case OP_WAIT:
		*val = d->loops;
		return ++app->polls < 1000 / TICK_MS ? AGAIN : DONE;
	case OP_GIVE:
		*val = d->loops;
		fw_sem_give(&d->stop);
		return DONE;
	case OP_JOIN:
		fw_task_join(d->task);
		d->task = NULL;
		*val = d->loops;
		return DONE;
	case OP_SEMDEL:
		*val = (uint32_t)fw_sem_delete(&d->stop);
		return DONE;
	case OP_SND_OPEN:
		app->snd = fw_sound_open();
		*val = (uint32_t)(uintptr_t)app->snd;
		return app->snd ? DONE : FAIL;
	case OP_SND_PLAY:
		fw_sound_play(app->snd, FW_SFX(beep));
		return DONE;
	case OP_SND_CLOSE:
		fw_sound_close(app->snd);
		app->snd = NULL;
		return DONE;
	}
	return FAIL;
}

static void tick(lv_timer_t *t)
{
	app_t *app = t->user_data;
	if (!app->running)
		return;
	uint8_t op = sequences[app->seq].ops[app->pos];
	char line[40];
	if (!app->announced) {
		char *p = fw_put_str(line, "\n");
		p = fw_put_dec(p, app->pos + 1u);
		p = fw_put_str(p, " ");
		fw_put_str(p, op_names[op]);
		log_add(app, line);
		app->announced = true;
		app->polls = 0;
		return;
	}
	uint32_t ms = lv_tick_get(), val = 0;
	int r = run_op(app, op, &val);
	if (r == AGAIN)
		return;
	ms = lv_tick_get() - ms;
	char *p = fw_put_str(line, r == DONE ? " ok " : " FAIL ");
	p = fw_put_dec(p, ms);
	p = fw_put_str(p, " ");
	if (val > 0xFFFF)
		fw_put_hex(p, val, 6);
	else
		fw_put_dec(p, val);
	log_add(app, line);
	app->announced = false;
	if (r == FAIL || sequences[app->seq].ops[++app->pos] == OP_END) {
		log_add(app, r == FAIL ? "\nStopped." : "\nAll passed.");
		app->running = false;
	}
}

static void show_choice(app_t *app)
{
	char line[64];
	char *p = fw_put_str(line, "\n< ");
	p = fw_put_dec(p, app->seq + 1u);
	p = fw_put_str(p, " > ");
	fw_put_str(p, sequences[app->seq].name);
	log_add(app, line);
}

static void on_delete(void *ctx)
{
	app_t *app = ctx;
	lv_timer_del(app->timer);
	fw_sound_close(app->snd);
	fw_os_free(app->d);
	lv_mem_free(app);
}

static void key_cb(lv_event_t *e)
{
	app_t *app = lv_event_get_user_data(e);
	uint32_t key = *(uint32_t *)lv_event_get_param(e);
	if (app->running)
		return;
	if (key == FW_KEY_BACK || key == LV_KEY_ESC) {
		fw_overlay_close(app->ov);
	} else if (key == FW_KEY_LEFT || key == FW_KEY_RIGHT) {
		app->seq = (uint8_t)((app->seq + (key == FW_KEY_RIGHT ? 1 : SEQUENCES - 1)) % SEQUENCES);
		show_choice(app);
	} else if (key == LV_KEY_ENTER) {
		char line[24];
		fw_put_dec(fw_put_str(line, "\nheap "), fw_os_heap_free());
		log_add(app, line);
		app->pos = 0;
		app->announced = false;
		app->running = true;
	}
}

static void open(lv_obj_t *return_focus)
{
	diag_t *d = fw_os_calloc(1, sizeof(diag_t));
	if (!fw_is_ram(d))
		return;
	app_t *app = lv_mem_alloc(sizeof(app_t));
	if (!app) {
		fw_os_free(d);
		return;
	}
	memset(app, 0, sizeof(*app));
	app->d = d;
	app->ov = fw_overlay_open(return_focus, lv_rgb(0x10, 0x20, 0x30), on_delete, app);
	if (!app->ov) {
		fw_os_free(d);
		lv_mem_free(app);
		return;
	}
	lv_obj_t *root = app->ov->root;
	app->log = fw_label(root, "", lv_rgb(0xFF, 0xFF, 0xFF));
	lv_label_set_long_mode(app->log, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(app->log, lv_disp_get_hor_res(NULL) - 4);
	lv_obj_align(app->log, LV_ALIGN_BOTTOM_LEFT, 2, -2); /* keeps the newest line visible */
	app->timer = lv_timer_create(tick, TICK_MS, app);
	lv_obj_add_event_cb(root, key_cb, LV_EVENT_KEY, app);
	log_add(app, "Sound test. Left/Right: choose, Center: run");
	show_choice(app);
}

FW_MENU_ITEM(FW_SYMBOL_AUDIO, "Sound test", open);
