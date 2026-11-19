/*
 * "Task monitor" entry in the APP menu: the FreeRTOS tasks with their priority, state, lowest free
 * stack and CPU use over the last second, plus the heap. Up changes the sort column (highlighted),
 * Left/Right page through the list, Back exits.
 *
 * Once a second the UI task takes a snapshot with the ROM kernel's uxTaskGetSystemState. CPU use
 * is each task's run time since the previous snapshot over the total; run times count the
 * hardware timer. Names are copied right away, since a task may be deleted after the snapshot.
 *
 * Dmitry Brant, 2026
 */
#include "sdk.h"

#define MAX_TASKS 40
#define NAME_LEN 8
#define ROWS 7
#define LINE_H 15 /* the UI font's line height */
#define SAMPLE_MS 1000
#define NO_CPU 0xFFFF

enum { COL_NAME, COL_PRIO, COL_STATE, COL_STACK, COL_CPU, COLS };
enum { SORT_PRIO, SORT_CPU, SORT_STACK, SORTS };

/* x, width, right-aligned; the header text */
static const struct {
	uint8_t x, w;
	bool right;
	const char *title;
} columns[COLS] = {
	{2, 50, false, "Task"},
	{50, 18, true, "Pri"},
	{74, 10, false, "S"},
	{82, 26, true, "Stack"},
	{106, 20, true, "CPU"},
};
static const uint8_t sort_column[SORTS] = {COL_PRIO, COL_CPU, COL_STACK};
static const char state_letters[] = "?RBSD"; /* indexed by FW_TASK_* */

typedef struct {
	char name[NAME_LEN + 1];
	uint8_t prio, state;
	uint32_t stack; /* bytes */
	uint16_t cpu;   /* percent, or NO_CPU */
} row_t;

typedef struct {
	fw_overlay_t *ov;
	lv_timer_t *timer;
	lv_obj_t *head[COLS], *col[COLS], *summary, *heap;
	uint8_t sort, page, count;
	bool have_prev;
	uint32_t prev_total;
	uint8_t prev_count;
	void *prev_handle[MAX_TASKS];
	uint32_t prev_run[MAX_TASKS];
	fw_task_status_t st[MAX_TASKS];
	row_t rows[MAX_TASKS];
} app_t;

static int by_prio(const void *a, const void *b)
{
	const row_t *x = a, *y = b;
	return x->prio != y->prio ? y->prio - x->prio : rom_strcmp(x->name, y->name);
}

static int by_cpu(const void *a, const void *b)
{
	const row_t *x = a, *y = b;
	int cx = x->cpu == NO_CPU ? -1 : x->cpu, cy = y->cpu == NO_CPU ? -1 : y->cpu;
	return cx != cy ? cy - cx : by_prio(a, b);
}

static int by_stack(const void *a, const void *b)
{
	const row_t *x = a, *y = b;
	return x->stack != y->stack ? (x->stack < y->stack ? -1 : 1) : by_prio(a, b);
}

static char *put_kb(char *p, uint32_t bytes)
{
	p = fw_put_dec(p, (bytes + 512) / 1024);
	return fw_put_str(p, "K");
}

static void show(app_t *app)
{
	int pages = app->count ? (app->count + ROWS - 1) / ROWS : 1;
	if (app->page >= pages)
		app->page = (uint8_t)(pages - 1);
	char text[COLS][ROWS * 12];
	char *p[COLS];
	for (int c = 0; c < COLS; c++)
		p[c] = text[c];
	for (int i = app->page * ROWS; i < app->count && i < (app->page + 1) * ROWS; i++) {
		const row_t *r = &app->rows[i];
		if (i != app->page * ROWS)
			for (int c = 0; c < COLS; c++)
				*p[c]++ = '\n';
		p[COL_NAME] = fw_put_str(p[COL_NAME], r->name);
		p[COL_PRIO] = fw_put_dec(p[COL_PRIO], r->prio);
		*p[COL_STATE]++ = state_letters[r->state < 5 ? r->state : 0];
		p[COL_STACK] = r->stack < 10000 ? fw_put_dec(p[COL_STACK], r->stack) : put_kb(p[COL_STACK], r->stack);
		p[COL_CPU] = r->cpu == NO_CPU ? fw_put_str(p[COL_CPU], "-") : fw_put_dec(p[COL_CPU], r->cpu);
	}
	for (int c = 0; c < COLS; c++) {
		*p[c] = 0;
		lv_label_set_text(app->col[c], text[c]);
		bool sorted = c == sort_column[app->sort];
		lv_obj_set_style_text_color(app->head[c], sorted ? lv_rgb(0xFF, 0xD0, 0x40) : lv_rgb(0x80, 0xA0, 0xC0), 0);
	}
}

static void show_summary(app_t *app, int busy)
{
	char text[48];
	char *p = fw_put_dec(text, app->count);
	p = fw_put_str(p, " tasks");
	if (busy >= 0) {
		p = fw_put_str(p, ", ");
		p = fw_put_dec(p, (uint32_t)busy);
		p = fw_put_str(p, "% busy");
	}
	int pages = (app->count + ROWS - 1) / ROWS;
	if (pages > 1) {
		p = fw_put_str(p, "  ");
		p = fw_put_dec(p, app->page + 1u);
		p = fw_put_str(p, "/");
		fw_put_dec(p, (uint32_t)pages);
	}
	lv_label_set_text(app->summary, text);
	p = fw_put_str(text, "Heap ");
	p = put_kb(p, FW_HEAP_FREE);
	p = fw_put_str(p, "/");
	p = put_kb(p, FW_HEAP_SIZE);
	p = fw_put_str(p, ", min ");
	put_kb(p, FW_HEAP_MIN_FREE);
	lv_label_set_text(app->heap, text);
}

static void sort_rows(app_t *app)
{
	static int (*const cmp[SORTS])(const void *, const void *) = {by_prio, by_cpu, by_stack};
	rom_qsort(app->rows, app->count, sizeof(row_t), cmp[app->sort]);
}

/* Takes a snapshot, computes CPU use against the previous one, and shows it. */
static void sample(app_t *app)
{
	uint32_t total = 0;
	uint32_t n = rom_task_states(app->st, MAX_TASKS, &total);
	if (n == 0) {
		lv_label_set_text(app->summary, "Too many tasks");
		return;
	}
	uint32_t dt = total - app->prev_total;
	int busy = -1;
	for (uint32_t i = 0; i < n; i++) {
		const fw_task_status_t *s = &app->st[i];
		row_t *r = &app->rows[i];
		int k = 0;
		while (k < NAME_LEN && s->name[k]) {
			r->name[k] = s->name[k];
			k++;
		}
		r->name[k] = 0;
		r->prio = (uint8_t)s->priority;
		r->state = s->state;
		r->stack = s->stack_free * 4u;
		r->cpu = NO_CPU;
		for (int j = 0; app->have_prev && dt && j < app->prev_count; j++)
			if (app->prev_handle[j] == s->handle) {
				uint64_t pct = ((uint64_t)(s->run_time - app->prev_run[j]) * 100 + dt / 2) / dt;
				r->cpu = (uint16_t)(pct > 100 ? 100 : pct);
				break;
			}
		if (r->cpu != NO_CPU && rom_strcmp(r->name, "IDLE") == 0)
			busy = 100 - r->cpu;
	}
	for (uint32_t i = 0; i < n; i++) {
		app->prev_handle[i] = app->st[i].handle;
		app->prev_run[i] = app->st[i].run_time;
	}
	app->prev_count = (uint8_t)n;
	app->prev_total = total;
	app->have_prev = true;
	app->count = (uint8_t)n;
	sort_rows(app);
	show(app);
	show_summary(app, busy);
}

static void tick(lv_timer_t *t)
{
	sample(t->user_data);
}

static void on_delete(void *ctx)
{
	app_t *app = ctx;
	if (app->timer)
		lv_timer_del(app->timer);
	lv_mem_free(app);
}

static void key_cb(lv_event_t *e)
{
	app_t *app = lv_event_get_user_data(e);
	uint32_t key = *(uint32_t *)lv_event_get_param(e);
	int pages = (app->count + ROWS - 1) / ROWS;
	switch (key) {
	case FW_KEY_BACK:
	case FW_KEY_BACK_LONG:
	case LV_KEY_ESC:
		fw_overlay_close(app->ov); /* frees app via on_delete */
		return;
	case FW_KEY_UP:
		app->sort = (uint8_t)((app->sort + 1) % SORTS);
		sort_rows(app);
		break;
	case FW_KEY_LEFT:
	case FW_KEY_LEFT_REPEAT:
		app->page = (uint8_t)(app->page ? app->page - 1 : (pages ? pages - 1 : 0));
		break;
	case FW_KEY_RIGHT:
	case FW_KEY_RIGHT_REPEAT:
		app->page = (uint8_t)(app->page + 1 < pages ? app->page + 1 : 0);
		break;
	default:
		return;
	}
	show(app);
}

static lv_obj_t *column_label(lv_obj_t *root, int c, lv_coord_t y, lv_color_t color)
{
	lv_obj_t *l = fw_label(root, "", color);
	lv_obj_set_width(l, columns[c].w);
	if (columns[c].right)
		lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
	lv_obj_set_pos(l, columns[c].x, y);
	return l;
}

static void open(lv_obj_t *return_focus)
{
	app_t *app = lv_mem_alloc(sizeof(app_t));
	if (!app)
		return;
	memset(app, 0, sizeof(*app));
	app->ov = fw_overlay_open(return_focus, lv_rgb(0, 0, 0), on_delete, app);
	if (!app->ov) {
		lv_mem_free(app);
		return;
	}
	lv_obj_t *root = app->ov->root;
	lv_coord_t h = lv_disp_get_ver_res(NULL);
	for (int c = 0; c < COLS; c++) {
		app->head[c] = column_label(root, c, 0, lv_rgb(0x80, 0xA0, 0xC0));
		lv_label_set_text(app->head[c], columns[c].title);
		app->col[c] = column_label(root, c, LINE_H, lv_rgb(0xFF, 0xFF, 0xFF));
	}
	app->summary = fw_label(root, "", lv_rgb(0xC0, 0xC8, 0xD0));
	lv_obj_set_pos(app->summary, 2, (lv_coord_t)(h - 2 * LINE_H));
	app->heap = fw_label(root, "", lv_rgb(0xC0, 0xC8, 0xD0));
	lv_obj_set_pos(app->heap, 2, (lv_coord_t)(h - LINE_H));
	lv_obj_add_event_cb(root, key_cb, LV_EVENT_KEY, app);

	sample(app);
	app->timer = lv_timer_create(tick, SAMPLE_MS, app);
}

FW_MENU_ITEM(FW_SYMBOL_BARS, "Task monitor", open);
