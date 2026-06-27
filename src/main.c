#include <dirent.h>
#include <gdk/gdkx.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <gtk/gtkflowbox.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vte/vte.h>

static GtkWidget *cpu_drawing_area;
static double cpu_usage = 0.0;
static GtkWidget *ram_drawing_area;
static double ram_usage = 0.0;

double get_cpu_usage() {
  static long long prev_idle = 0, prev_total = 0;
  static int first = 1;

  FILE *fp = fopen("/proc/stat", "r");
  if (!fp)
    return 0;

  char line[256];
  fgets(line, sizeof(line), fp);
  fclose(fp);

  long long user, nice, system, idle, iowait, irq, softirq, steal;

  sscanf(line, "cpu %lld %lld %lld %lld %lld %lld %lld %lld", &user, &nice,
         &system, &idle, &iowait, &irq, &softirq, &steal);

  long long idle_time = idle + iowait;
  long long total =
      user + nice + system + idle + iowait + irq + softirq + steal;

  if (first) {
    prev_idle = idle_time;
    prev_total = total;
    first = 0;
    return 0;
  }

  long long diff_idle = idle_time - prev_idle;
  long long diff_total = total - prev_total;

  prev_idle = idle_time;
  prev_total = total;

  if (diff_total == 0)
    return 0;

  return (double)(diff_total - diff_idle) * 100.0 / diff_total;
}

double get_ram_usage() {
  FILE *fp = fopen("/proc/meminfo", "r");
  if (!fp)
    return 0;

  char line[256];
  long total = 0, free_mem = 0, buffers = 0, cached = 0;

  while (fgets(line, sizeof(line), fp)) {
    sscanf(line, "MemTotal: %ld", &total);
    sscanf(line, "MemFree: %ld", &free_mem);
    sscanf(line, "Buffers: %ld", &buffers);
    sscanf(line, "Cached: %ld", &cached);
  }

  fclose(fp);

  long used = total - (free_mem + buffers + cached);
  if (total == 0)
    return 0;

  return (used * 100.0) / total;
}

static gboolean on_cpu_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {

  GtkAllocation a;
  gtk_widget_get_allocation(widget, &a);

  int w = a.width;
  int h = a.height;

  double cx = w / 2.0;
  double cy = h / 2.0;
  double r = (w < h ? w : h) / 3.0;

  cairo_set_source_rgb(cr, 0, 0, 0);
  cairo_paint(cr);

  double angle = (cpu_usage / 100.0) * 2 * M_PI;

  /* base ring */
  cairo_set_line_width(cr, 8);
  cairo_set_source_rgba(cr, 0, 1, 0, 0.15);
  cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
  cairo_stroke(cr);

  /* active arc */
  cairo_set_source_rgb(cr, 0, 1, 0);
  cairo_arc(cr, cx, cy, r, -M_PI / 2, -M_PI / 2 + angle);
  cairo_stroke(cr);

  /* ===== ONLY NUMBER INSIDE CIRCLE ===== */
  char buf[32];
  snprintf(buf, sizeof(buf), "%.0f%%", cpu_usage);

  cairo_set_source_rgb(cr, 0, 1, 0);
  cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_BOLD);

  cairo_set_font_size(cr, 26);
  cairo_text_extents_t ext;
  cairo_text_extents(cr, buf, &ext);

  cairo_move_to(cr, cx - ext.width / 2, cy + ext.height / 2);
  cairo_show_text(cr, buf);

  /* ===== CPU LABEL OUTSIDE CIRCLE ===== */
  cairo_set_font_size(cr, 14);
  cairo_text_extents(cr, "CPU", &ext);

  cairo_move_to(cr, cx - ext.width / 2, cy - r - 10);
  cairo_show_text(cr, "CPU");

  return FALSE;
}

static gboolean update_cpu(gpointer data) {
  cpu_usage = get_cpu_usage();
  gtk_widget_queue_draw(cpu_drawing_area);
  return TRUE;
}
gboolean on_ram_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
  GtkAllocation a;
  gtk_widget_get_allocation(widget, &a);

  int w = a.width;
  int h = a.height;

  double cx = w / 2.0;
  double cy = h / 2.0;
  double r = (w < h ? w : h) / 3.0;

  cairo_set_source_rgb(cr, 0, 0, 0);
  cairo_paint(cr);

  double angle = (ram_usage / 100.0) * 2 * M_PI;

  // base ring
  cairo_set_line_width(cr, 10);
  cairo_set_source_rgba(cr, 0, 1, 0.4, 0.15);
  cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
  cairo_stroke(cr);

  // active ring glow
  cairo_set_source_rgba(cr, 0, 1, 0.5, 0.9);
  cairo_arc(cr, cx, cy, r, -M_PI / 2, -M_PI / 2 + angle);
  cairo_stroke(cr);

  // ===== RAM LABEL (TOP) =====
  cairo_set_source_rgb(cr, 0, 1, 0.6);
  cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 14);

  cairo_move_to(cr, cx - 20, cy - r - 10);
  cairo_show_text(cr, "RAM");

  // ===== CENTER NUMBER FIXED =====
  char buf[32];
  snprintf(buf, sizeof(buf), "%.0f%%", ram_usage);

  cairo_text_extents_t ext;

  cairo_set_font_size(cr, 26);
  cairo_set_source_rgb(cr, 0.2, 1.0, 0.6);

  cairo_text_extents(cr, buf, &ext);

  /* ===== PERFECT CENTER FIX ===== */
  double x = cx - (ext.width / 2 + ext.x_bearing);
  double y = cy - (ext.height / 2 + ext.y_bearing);

  cairo_move_to(cr, x, y);
  cairo_show_text(cr, buf);

  return FALSE;
}

gboolean update_ram(gpointer data) {
  ram_usage = get_ram_usage();
  gtk_widget_queue_draw(ram_drawing_area);
  return TRUE;
}

static GtkWidget *net_drawing_area;
gboolean fm_active = FALSE;
static GtkWidget *flow;
static char current_path[1024] = ".";
static gboolean show_hidden = FALSE;

static char *get_icon(const char *fullpath) {
  GFile *file = g_file_new_for_path(fullpath);

  GFileInfo *info =
      g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                        G_FILE_QUERY_INFO_NONE, NULL, NULL);

  const char *content_type = NULL;

  if (info)
    content_type = g_file_info_get_content_type(info);

  GIcon *icon = content_type ? g_content_type_get_icon(content_type) : NULL;

  char *result = g_strdup("text-x-generic");

  if (G_IS_THEMED_ICON(icon)) {
    const char *const *names = g_themed_icon_get_names(G_THEMED_ICON(icon));
    if (names && names[0])
      result = g_strdup(names[0]);
  }

  if (icon)
    g_object_unref(icon);
  if (info)
    g_object_unref(info);
  g_object_unref(file);

  return result; // MUST free later
}

/* ---------------- SHORT NAME ---------------- */
static void short_name(const char *name, char *out, size_t size) {
  int max = 5;

  if (strlen(name) <= max)
    snprintf(out, size, "%s", name);
  else
    snprintf(out, size, "%.*s...", max, name);
}

/* ---------------- LOAD DIRECTORY (FORWARD DECL) ---------------- */
static void load_dir(const char *path);

/* ---------------- OPEN ITEM ---------------- */
static void open_item(const char *path) {
  struct stat st;

  if (stat(path, &st) != 0)
    return;

  if (S_ISDIR(st.st_mode)) {
    load_dir(path);
  } else {
    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "xdg-open \"%s\" &", path);
    system(cmd);
  }
}

static void clear_flowbox() {
  GList *children = gtk_container_get_children(GTK_CONTAINER(flow));

  for (GList *i = children; i != NULL; i = i->next)
    gtk_widget_destroy(GTK_WIDGET(i->data));

  g_list_free(children);
}

/* ---------------- ITEM STRUCT ---------------- */
typedef struct {
  char name[256];
  char full[1024];
  int is_dir;
} Item;

/* folders first */
static int sort_items(const void *a, const void *b) {
  const Item *ia = a;
  const Item *ib = b;

  if (ia->is_dir != ib->is_dir)
    return ib->is_dir - ia->is_dir;

  return strcasecmp(ia->name, ib->name);
}

/* ---------------- LOAD DIRECTORY (FIXED) ---------------- */
static void load_dir(const char *path) {

  char resolved[1024];

  if (realpath(path, resolved) == NULL)
    return;

  DIR *dir = opendir(resolved);
  if (!dir)
    return;

  strncpy(current_path, resolved, sizeof(current_path));
  current_path[sizeof(current_path) - 1] = '\0';

  clear_flowbox();

  Item items[4096];
  int count = 0;

  struct dirent *entry;

  while ((entry = readdir(dir)) != NULL) {

    if (!show_hidden && entry->d_name[0] == '.')
      continue;

    if (count >= 4095)
      break;

    char full[1024];
    snprintf(full, sizeof(full), "%s/%s", resolved, entry->d_name);

    struct stat st;
    if (stat(full, &st) != 0)
      continue;

    strcpy(items[count].name, entry->d_name);
    strcpy(items[count].full, full);
    items[count].is_dir = S_ISDIR(st.st_mode);

    count++;
  }

  closedir(dir);

  qsort(items, count, sizeof(Item), sort_items);

  for (int i = 0; i < count; i++) {

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    GtkWidget *icon = gtk_image_new_from_icon_name(get_icon(items[i].full),
                                                   GTK_ICON_SIZE_DIALOG);

    char shortn[64];
    short_name(items[i].name, shortn, sizeof(shortn));

    GtkWidget *label = gtk_label_new(shortn);

    gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

    GtkWidget *child = gtk_flow_box_child_new();
    gtk_widget_set_can_focus(child, TRUE);
    gtk_container_add(GTK_CONTAINER(child), box);

    gtk_widget_set_can_focus(box, FALSE);
    gtk_widget_set_focus_on_click(child, TRUE);

    g_object_set_data_full(G_OBJECT(child), "fullpath", g_strdup(items[i].full),
                           g_free);

    gtk_flow_box_insert(GTK_FLOW_BOX(flow), child, -1);
  }

  if (count > 0) {
    GtkFlowBoxChild *first =
        gtk_flow_box_get_child_at_index(GTK_FLOW_BOX(flow), 0);

    if (first) {
      gtk_flow_box_select_child(GTK_FLOW_BOX(flow), first);
      gtk_widget_grab_focus(GTK_WIDGET(first));
    }
  }

  gtk_widget_show_all(flow);
}

/* ---------------- OPEN SELECTED (FIXED) ---------------- */
static void open_selected() {

  GList *selected = gtk_flow_box_get_selected_children(GTK_FLOW_BOX(flow));
  if (!selected)
    return;

  GtkFlowBoxChild *child = GTK_FLOW_BOX_CHILD(selected->data);

  const char *path = g_object_get_data(G_OBJECT(child), "fullpath");

  if (path)
    open_item(path);

  g_list_free(selected);
}

/* ---------------- BACK (FIXED SAFE) ---------------- */
static void go_back() {

  char tmp[1024];

  if (realpath(current_path, tmp) == NULL)
    return;

  if (strcmp(tmp, "/") == 0) {
    load_dir("/");
    return;
  }

  char *last = strrchr(tmp, '/');

  if (last) {
    if (last == tmp) {
      load_dir("/");
    } else {
      *last = '\0';
      load_dir(tmp);
    }
  }
}

gboolean shift_active = FALSE;
gboolean caps_active = FALSE;
gboolean ctrl_active = FALSE;
gboolean alt_active = FALSE;

#define GRAPH_POINTS 200

static double rx_graph[GRAPH_POINTS];
static double tx_graph[GRAPH_POINTS];

static unsigned long long last_rx = 0;
static unsigned long long last_tx = 0;

static int get_net_stats(unsigned long long *rx, unsigned long long *tx) {
  FILE *f = fopen("/proc/net/dev", "r");
  if (!f)
    return 0;

  char line[512];

  *rx = 0;
  *tx = 0;

  // skip first 2 header lines
  fgets(line, sizeof(line), f);
  fgets(line, sizeof(line), f);

  while (fgets(line, sizeof(line), f)) {
    char iface[64];
    unsigned long long rbytes, tbytes;

    sscanf(line, "%63[^:]: %llu %*u %*u %*u %*u %*u %*u %*u %llu", iface,
           &rbytes, &tbytes);

    // sum all interfaces
    *rx += rbytes;
    *tx += tbytes;
  }

  fclose(f);
  return 1;
}

static void shift_graph(double *graph, double value) {
  memmove(graph, graph + 1, sizeof(double) * (GRAPH_POINTS - 1));
  graph[GRAPH_POINTS - 1] = value;
}

static gboolean update_network(gpointer data) {
  unsigned long long rx, tx;

  if (!get_net_stats(&rx, &tx))
    return TRUE;

  if (last_rx == 0) {
    last_rx = rx;
    last_tx = tx;
    return TRUE;
  }

  double rx_speed = (double)(rx - last_rx) / 1024.0;
  double tx_speed = (double)(tx - last_tx) / 1024.0;

  last_rx = rx;
  last_tx = tx;

  shift_graph(rx_graph, rx_speed);
  shift_graph(tx_graph, tx_speed);

  gtk_widget_queue_draw(net_drawing_area);

  return TRUE;
}

static void draw_graph(cairo_t *cr, double *graph, int x, int y, int w, int h) {
  double max = 1;

  for (int i = 0; i < GRAPH_POINTS; i++)
    if (graph[i] > max)
      max = graph[i];

  cairo_move_to(cr, x, y + h);

  for (int i = 0; i < GRAPH_POINTS; i++) {
    double px = x + ((double)i / GRAPH_POINTS) * w;
    double py = y + h - (graph[i] / max) * h;
    cairo_line_to(cr, px, py);
  }

  cairo_stroke(cr);
}

static gboolean on_net_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
  GtkAllocation a;
  gtk_widget_get_allocation(widget, &a);

  int w = a.width;
  int h = a.height;

  /* ===== BACKGROUND (glass + gradient) ===== */
  /* ===== CLEAN DARK GLASS BACKGROUND ===== */
  cairo_pattern_t *bg = cairo_pattern_create_linear(0, 0, 0, h);

  /* deep black top */
  cairo_pattern_add_color_stop_rgba(bg, 0.0, 0.02, 0.03, 0.05, 0.98);

  /* subtle green tint middle (very light) */
  cairo_pattern_add_color_stop_rgba(bg, 0.5, 0.02, 0.08, 0.06, 0.92);

  /* dark bottom */
  cairo_pattern_add_color_stop_rgba(bg, 1.0, 0.01, 0.02, 0.03, 0.98);

  cairo_rectangle(cr, 0, 0, w, h);
  cairo_set_source(cr, bg);
  cairo_fill(cr);
  cairo_pattern_destroy(bg);

  /* ===== SOFT VIGNETTE GLOW (edges dark) ===== */
  cairo_pattern_t *v =
      cairo_pattern_create_radial(w / 2, h / 2, 50, w / 2, h / 2, w);

  cairo_pattern_add_color_stop_rgba(v, 0.0, 0, 0, 0, 0.0);
  cairo_pattern_add_color_stop_rgba(v, 1.0, 0, 0, 0, 0.65);

  cairo_rectangle(cr, 0, 0, w, h);
  cairo_set_source(cr, v);
  cairo_fill(cr);
  cairo_pattern_destroy(v);

  /* ===== BORDER GLOW ===== */
  cairo_set_source_rgba(cr, 0, 1, 0.5, 0.35);
  cairo_set_line_width(cr, 2);
  cairo_rectangle(cr, 1, 1, w - 2, h - 2);
  cairo_stroke(cr);

  /* ===== TITLE ===== */
  cairo_set_source_rgb(cr, 0, 1, 0.7);
  cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 14);

  cairo_move_to(cr, 15, 22);
  cairo_show_text(cr, "NETWORK ANALYZER");

  /* ===== RX / TX TEXT (NEON CARDS STYLE) ===== */
  char buf[128];

  cairo_set_font_size(cr, 12);

  cairo_set_source_rgb(cr, 0.2, 1, 0.6);
  snprintf(buf, sizeof(buf), "RX  ↓  %.1f KB/s", rx_graph[GRAPH_POINTS - 1]);
  cairo_move_to(cr, 15, 45);
  cairo_show_text(cr, buf);

  cairo_set_source_rgb(cr, 0.3, 0.8, 1.0);
  snprintf(buf, sizeof(buf), "TX  ↑  %.1f KB/s", tx_graph[GRAPH_POINTS - 1]);
  cairo_move_to(cr, 15, 65);
  cairo_show_text(cr, buf);

  /* ===== GRAPH STYLE (GLOW LINE) ===== */
  cairo_set_line_width(cr, 2);

  /* RX graph (green glow) */
  cairo_set_source_rgba(cr, 0, 1, 0.5, 0.9);
  draw_graph(cr, rx_graph, 20, 90, w - 40, (h - 130) / 2);

  /* TX graph (blue glow) */
  cairo_set_source_rgba(cr, 0.2, 0.7, 1.0, 0.9);
  draw_graph(cr, tx_graph, 20, h / 2 + 10, w - 40, (h - 130) / 2);

  /* ===== CENTER SEPARATOR LINE ===== */
  cairo_set_source_rgba(cr, 0, 1, 0.4, 0.2);
  cairo_move_to(cr, 10, h / 2);
  cairo_line_to(cr, w - 10, h / 2);
  cairo_stroke(cr);

  return FALSE;
}

static double dna_phase = 0.0;
GtkWidget *dna_area;

gboolean dna_tick(gpointer data) {
  dna_phase += 0.05;
  gtk_widget_queue_draw(GTK_WIDGET(data));
  return TRUE;
}

gboolean dna_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
  GtkAllocation a;
  gtk_widget_get_allocation(widget, &a);

  int w = a.width;
  int h = a.height;

  /* ===== DARK CLEAN BACKGROUND ===== */
  cairo_set_source_rgb(cr, 0.01, 0.02, 0.02);
  cairo_paint(cr);

  /* ===== SOFT VERTICAL GLOW STRIP (center focus) ===== */
  cairo_pattern_t *g = cairo_pattern_create_linear(0, 0, w, 0);
  cairo_pattern_add_color_stop_rgba(g, 0.0, 0, 0, 0, 0.0);
  cairo_pattern_add_color_stop_rgba(g, 0.5, 0, 1, 0.6, 0.05);
  cairo_pattern_add_color_stop_rgba(g, 1.0, 0, 0, 0, 0.0);

  cairo_rectangle(cr, 0, 0, w, h);
  cairo_set_source(cr, g);
  cairo_fill(cr);
  cairo_pattern_destroy(g);

  /* ===== TITLE ===== */
  cairo_set_source_rgb(cr, 0.0, 1.0, 0.6);
  cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 15);

  cairo_move_to(cr, 15, 25);
  cairo_show_text(cr, "SEQUENCE SCANNER");

  /* ===== CENTER LINE (helix spine) ===== */
  int center = w / 2;

  cairo_set_line_width(cr, 2);
  cairo_set_source_rgba(cr, 0, 1, 0.4, 0.35);

  cairo_move_to(cr, center, 40);
  cairo_line_to(cr, center, h - 20);
  cairo_stroke(cr);

  /* ===== CLEAN HELIX ===== */
  cairo_set_line_width(cr, 1.5);

  for (int y = 40; y < h - 20; y += 10) {
    double t = y * 0.06 + dna_phase;

    double x1 = center + sin(t) * 60;
    double x2 = center + sin(t + M_PI) * 60;

    double glow = (sin(t) + 1.0) / 2.0;

    /* connecting rungs */
    cairo_set_source_rgba(cr, 0, 1, 0.5, 0.25);
    cairo_move_to(cr, x1, y);
    cairo_line_to(cr, x2, y);
    cairo_stroke(cr);

    /* left node */
    cairo_set_source_rgba(cr, 0, 1, 0.6, 0.4 + glow * 0.4);
    cairo_arc(cr, x1, y, 3 + glow * 2, 0, 2 * M_PI);
    cairo_fill(cr);

    /* right node */
    cairo_set_source_rgba(cr, 0, 0.8, 1.0, 0.3 + glow * 0.3);
    cairo_arc(cr, x2, y, 3 + glow * 2, 0, 2 * M_PI);
    cairo_fill(cr);
  }

  /* ===== SCAN LINE (slow clean sweep) ===== */
  double scan = fmod(dna_phase * 60.0, h);

  cairo_set_source_rgba(cr, 0, 1, 0.5, 0.08);
  cairo_rectangle(cr, 0, scan, w, 18);
  cairo_fill(cr);

  return FALSE;
}

/* ================= KEYBOARD FUNCTIONS ================= */

void send_text(const char *text);

void on_shift(GtkWidget *btn, gpointer data) {
  shift_active = !shift_active;
  printf("SHIFT=%d\n", shift_active);
}

void on_caps(GtkWidget *btn, gpointer data) { caps_active = !caps_active; }
void on_ctrl(GtkWidget *btn, gpointer data) { ctrl_active = !ctrl_active; }
void on_alt(GtkWidget *btn, gpointer data) { alt_active = !alt_active; }

void on_button(GtkWidget *btn, gpointer data) {
  const char *txt = (const char *)data;

  if (ctrl_active && strlen(txt) == 1) {
    char c = txt[0];

    if (c >= 'a' && c <= 'z') {
      char ctrl_char = c - 'a' + 1;
      char buf[2] = {ctrl_char, '\0'};

      send_text(buf);
      ctrl_active = FALSE;
      return;
    }
  }

  if (strlen(txt) == 1) {
    char c = txt[0];

    if ((caps_active || shift_active) && c >= 'a' && c <= 'z') {
      c -= 32;
    }

    if (shift_active) {
      switch (c) {
      case '1':
        c = '!';
        break;
      case '2':
        c = '@';
        break;
      case '3':
        c = '#';
        break;
      case '4':
        c = '$';
        break;
      case '5':
        c = '%';
        break;
      case '6':
        c = '^';
        break;
      case '7':
        c = '&';
        break;
      case '8':
        c = '*';
        break;
      case '9':
        c = '(';
        break;
      case '0':
        c = ')';
        break;
      case '-':
        c = '_';
        break;
      case '=':
        c = '+';
        break;
      case '[':
        c = '{';
        break;
      case ']':
        c = '}';
        break;
      case ';':
        c = ':';
        break;
      case '\'':
        c = '"';
        break;
      case ',':
        c = '<';
        break;
      case '.':
        c = '>';
        break;
      case '/':
        c = '?';
        break;
      case '\\':
        c = '|';
        break;
      }
    }

    char buf[2] = {c, '\0'};
    send_text(buf);

    if (shift_active)
      shift_active = FALSE;

    return;
  }

  send_text(txt);
}

VteTerminal *global_vte;
GtkWidget *win;

char cfg_shell[128] = "/bin/zsh";
char cfg_font[128] = "Adwaita Mono";
float cfg_opacity = 0.78;

void on_backspace(GtkWidget *btn, gpointer data);

/* ================= CSS ================= */

const char *css =

    "window { background-color: rgba(0,0,0,0.95); }"

    "frame {"
    "  border-radius: 14px;"
    "  margin: 6px;"
    "  padding: 10px;"
    "  background-color: rgba(0, 20, 10, 0.35);"
    "  border: 1px solid rgba(0, 255, 120, 0.25);"
    "}"

    "label {"
    "  color: #00ff88;"
    "  font-family: monospace;"
    "}"

    "flowbox { background-color: rgba(0,0,0,0.4); }"

    "flowboxchild:selected {"
    "  background-color: rgba(0,255,120,0.15);"
    "  border: 1px solid #00ff88;"
    "  border-radius: 6px;"
    "}"

    "button {"
    "  background-color: rgba(0, 0, 0, 0.95);"
    "  color: #00ffcc;"
    "  border: 1px solid rgba(0,255,200,0.35);"
    "  padding: 4px 4px 4px 4px;"
    "}"

    "button:hover { background-color: rgba(0,255,200,0.25); }"
    "  #keyboard, #kbd_frame {"
    "  background-color: transparent;"
    "  border: none;"
    "}";

/* ================= LOAD CONFIG ================= */

void load_config() {
  FILE *f = fopen("config/config.conf", "r");
  if (!f)
    return;

  char line[256];
  while (fgets(line, sizeof(line), f)) {
    char key[128], val[128];
    if (sscanf(line, "%127[^=]=%127[^\n]", key, val) == 2) {
      if (strcmp(key, "shell") == 0)
        strncpy(cfg_shell, val, sizeof(cfg_shell));
      if (strcmp(key, "font") == 0)
        strncpy(cfg_font, val, sizeof(cfg_font));
      if (strcmp(key, "opacity") == 0)
        cfg_opacity = atof(val);
    }
  }
  fclose(f);
}

/* ================= EXIT ================= */

void on_child_exit(VteTerminal *vte, gint status, gpointer data) {
  gtk_window_close(GTK_WINDOW(win));
}

/* ================= SHELL ================= */

static void spawn_shell(VteTerminal *vte) {
  char *argv_shell[] = {cfg_shell, NULL};

  vte_terminal_spawn_async(vte, VTE_PTY_DEFAULT, NULL, argv_shell, NULL,
                           G_SPAWN_DEFAULT, NULL, NULL, NULL, -1, NULL, NULL,
                           NULL);
}

gboolean focus_terminal(gpointer data) {
  gtk_widget_grab_focus(GTK_WIDGET(global_vte));
  return FALSE;
}

/* ================= SEND TO SHELL ================= */

void send_text(const char *text) {
  vte_terminal_feed_child(global_vte, text, strlen(text));
}

gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {

  /* Shift + Q */
  if ((event->keyval == GDK_KEY_q || event->keyval == GDK_KEY_Q) &&
      (event->state & GDK_SHIFT_MASK)) {
    gtk_window_close(GTK_WINDOW(win));
    return TRUE;
  }

  // /* Ctrl+Shift+C = Copy */
  // if ((event->state & GDK_CONTROL_MASK) && (event->state & GDK_SHIFT_MASK) &&
  //     (event->keyval == GDK_KEY_c || event->keyval == GDK_KEY_C)) {
  //   vte_terminal_copy_clipboard(global_vte);
  //   return TRUE;
  // }

  if ((event->state & GDK_CONTROL_MASK) && (event->state & GDK_SHIFT_MASK) &&
      (event->keyval == GDK_KEY_c || event->keyval == GDK_KEY_C)) {

    vte_terminal_copy_clipboard_format(global_vte, VTE_FORMAT_TEXT);
    return TRUE;
  }

  /* Ctrl+Shift+V = Paste */
  if ((event->state & GDK_CONTROL_MASK) && (event->state & GDK_SHIFT_MASK) &&
      (event->keyval == GDK_KEY_v || event->keyval == GDK_KEY_V)) {
    vte_terminal_paste_clipboard(global_vte);
    return TRUE;
  }

  /* CTRL + F toggle file manager */
  if ((event->keyval == GDK_KEY_f || event->keyval == GDK_KEY_F) &&
      (event->state & GDK_CONTROL_MASK)) {

    fm_active = !fm_active;
    printf("File Manager Active = %d\n", fm_active);
    return TRUE;
  }

  if (!fm_active)
    return FALSE;

  switch (event->keyval) {
  case GDK_KEY_Right:
  case GDK_KEY_Left:
  case GDK_KEY_Down:
  case GDK_KEY_Up: {
    GList *sel = gtk_flow_box_get_selected_children(GTK_FLOW_BOX(flow));
    if (!sel)
      return TRUE;

    GtkFlowBoxChild *child = sel->data;
    g_list_free(sel);

    if (!child)
      return TRUE;

    GList *children = gtk_container_get_children(GTK_CONTAINER(flow));

    int current = gtk_flow_box_child_get_index(child);

    int target = current;

    if (event->keyval == GDK_KEY_Right)
      target++;

    else if (event->keyval == GDK_KEY_Left)
      target--;

    else if (event->keyval == GDK_KEY_Down)
      target += 7;

    else if (event->keyval == GDK_KEY_Up)
      target -= 7;

    GtkFlowBoxChild *new_child =
        gtk_flow_box_get_child_at_index(GTK_FLOW_BOX(flow), target);

    if (new_child)
      child = new_child;
    else
      child = NULL;

    if (child) {
      gtk_flow_box_unselect_all(GTK_FLOW_BOX(flow));
      gtk_flow_box_select_child(GTK_FLOW_BOX(flow), child);
      gtk_widget_grab_focus(GTK_WIDGET(child));
    }

    g_list_free(children);
    return TRUE;
  }

  case GDK_KEY_Return:
  case GDK_KEY_KP_Enter:
    open_selected();
    return TRUE;

  case GDK_KEY_BackSpace: {
    char tmp[1024];
    strcpy(tmp, current_path);

    char *p = strrchr(tmp, '/');

    if (p) {
      if (p == tmp)
        *(p + 1) = 0;
      else
        *p = 0;
    } else {
      strcpy(tmp, ".");
    }

    load_dir(tmp);
    return TRUE;
  }

  case GDK_KEY_h:
  case GDK_KEY_H:
    show_hidden = !show_hidden;
    load_dir(current_path);
    return TRUE;

  case GDK_KEY_r:
  case GDK_KEY_R:
    load_dir(current_path);
    return TRUE;
  }

  return FALSE;
}

static gboolean on_flow_click(GtkWidget *widget, GdkEventButton *event,
                              gpointer data) {
  if (event->type == GDK_2BUTTON_PRESS) {
    GtkFlowBoxChild *child =
        gtk_flow_box_get_child_at_pos(GTK_FLOW_BOX(widget), event->x, event->y);

    if (child) {
      const char *path = g_object_get_data(G_OBJECT(child), "fullpath");

      if (path)
        open_item(path);
    }
    return TRUE;
  }
  return FALSE;
}

/* ================= INPUT ================= */

void on_backspace(GtkWidget *btn, gpointer data) { send_text("\x7f"); }

/* ================= TERMINAL STYLE ================= */

void apply_style(VteTerminal *t) {
  PangoFontDescription *d = pango_font_description_from_string(cfg_font);
  vte_terminal_set_font(t, d);
  vte_terminal_set_cursor_blink_mode(t, VTE_CURSOR_BLINK_ON);
  vte_terminal_set_scrollback_lines(t, 8000);

  pango_font_description_free(d);

  /* ===== DARK + NEON TERMINAL THEME ===== */

  GdkRGBA bg;
  gdk_rgba_parse(&bg, "#05080a"); // dark background

  GdkRGBA fg;
  gdk_rgba_parse(&fg, "#00ffcc"); // neon text

  GdkRGBA palette[16];

  gdk_rgba_parse(&palette[0], "#0d0d0d"); // black
  gdk_rgba_parse(&palette[1], "#ff5555"); // red
  gdk_rgba_parse(&palette[2], "#50fa7b"); // green
  gdk_rgba_parse(&palette[3], "#f1fa8c"); // yellow
  gdk_rgba_parse(&palette[4], "#8be9fd"); // blue
  gdk_rgba_parse(&palette[5], "#ff79c6"); // magenta
  gdk_rgba_parse(&palette[6], "#8be9fd"); // cyan
  gdk_rgba_parse(&palette[7], "#bbbbbb"); // light gray

  /* bright variants */
  gdk_rgba_parse(&palette[8], "#44475a");
  gdk_rgba_parse(&palette[9], "#ff6e6e");
  gdk_rgba_parse(&palette[10], "#69ff94");
  gdk_rgba_parse(&palette[11], "#ffffa5");
  gdk_rgba_parse(&palette[12], "#d6acff");
  gdk_rgba_parse(&palette[13], "#ff92df");
  gdk_rgba_parse(&palette[14], "#a4ffff");
  gdk_rgba_parse(&palette[15], "#ffffff");

  vte_terminal_set_colors(t, &fg, &bg, palette, 16);
}

/* ================= MAIN ================= */

int main(int argc, char *argv[]) {
  gtk_init(&argc, &argv);
  load_config();

  GtkCssProvider *p = gtk_css_provider_new();
  gtk_css_provider_load_from_data(p, css, -1, NULL);

  gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                            GTK_STYLE_PROVIDER(p),
                                            GTK_STYLE_PROVIDER_PRIORITY_USER);

  win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(win), "CYBER GLASS TERM");

  gtk_window_set_icon_from_file(
    GTK_WINDOW(win),
                                "icons/cyber-glass-term.svg",
                                NULL
  );

  gtk_window_set_default_size(GTK_WINDOW(win), 1400, 900);
  gtk_widget_set_opacity(win, cfg_opacity);

  gtk_widget_add_events(win, GDK_KEY_PRESS_MASK);
  g_signal_connect(win, "key-press-event", G_CALLBACK(on_key_press), NULL);

  GdkScreen *screen = gdk_screen_get_default();
  GdkVisual *visual = gdk_screen_get_rgba_visual(screen);

  if (visual != NULL) {
    gtk_widget_set_visual(win, visual);
  }

  g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);

  GtkWidget *grid = gtk_grid_new();
  gtk_widget_set_hexpand(grid, TRUE);
  gtk_widget_set_vexpand(grid, TRUE);
  gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
  gtk_container_add(GTK_CONTAINER(win), grid);

  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);

  /* ================= LEFT PANEL ================= */

  GtkWidget *left_frame = gtk_frame_new("SYSTEM PANEL");
  gtk_widget_set_hexpand(left_frame, TRUE);
  gtk_widget_set_vexpand(left_frame, TRUE);
  GtkWidget *left_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

  GtkWidget *net_card = gtk_frame_new("NETWORK");

  net_drawing_area = gtk_drawing_area_new();

  GtkWidget *cpu_card = gtk_frame_new("CPU");

  cpu_drawing_area = gtk_drawing_area_new();
  gtk_widget_set_hexpand(cpu_drawing_area, TRUE);
  gtk_widget_set_vexpand(cpu_drawing_area, TRUE);

  g_signal_connect(cpu_drawing_area, "draw", G_CALLBACK(on_cpu_draw), NULL);

  gtk_container_add(GTK_CONTAINER(cpu_card), cpu_drawing_area);

  gtk_box_pack_start(GTK_BOX(left_box), cpu_card, TRUE, TRUE, 2);

  g_timeout_add(500, update_cpu, NULL);

  gtk_widget_set_hexpand(net_drawing_area, TRUE);
  gtk_widget_set_vexpand(net_drawing_area, TRUE);

  g_signal_connect(net_drawing_area, "draw", G_CALLBACK(on_net_draw), NULL);

  gtk_container_add(GTK_CONTAINER(net_card), net_drawing_area);

  gtk_box_pack_start(GTK_BOX(left_box), net_card, TRUE, TRUE, 2);

  g_timeout_add(1000, update_network, NULL);

  gtk_container_add(GTK_CONTAINER(left_frame), left_box);
  gtk_grid_attach(GTK_GRID(grid), left_frame, 0, 0, 1, 1);

  /* ================= CENTER TERMINAL ================= */

  GtkWidget *mid_frame = gtk_frame_new("SHELL");
  GtkWidget *term = vte_terminal_new();
  global_vte = VTE_TERMINAL(term);
  gtk_widget_show(term);

  gtk_widget_set_hexpand(mid_frame, TRUE);
  gtk_widget_set_vexpand(mid_frame, TRUE);
  gtk_widget_set_hexpand(term, TRUE);
  gtk_widget_set_vexpand(term, TRUE);
  gtk_container_add(GTK_CONTAINER(mid_frame), term);
  gtk_grid_attach(GTK_GRID(grid), mid_frame, 1, 0, 3, 1);

  // spawn_shell(global_vte);
  // apply_style(global_vte);
  apply_style(global_vte);

  gtk_widget_set_can_focus(GTK_WIDGET(term), TRUE);
  gtk_widget_grab_focus(GTK_WIDGET(term));

  g_signal_connect(global_vte, "child-exited", G_CALLBACK(on_child_exit), NULL);

  /* ================= RIGHT ================= */

  GtkWidget *right_frame = gtk_frame_new("SYSTEM MONITOR");
  gtk_widget_set_hexpand(right_frame, TRUE);
  gtk_widget_set_vexpand(right_frame, TRUE);
  GtkWidget *right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  GtkWidget *dna_card = gtk_frame_new("SCANNER");

  GtkWidget *ram_card = gtk_frame_new("RAM");

  ram_drawing_area = gtk_drawing_area_new();
  gtk_widget_set_hexpand(ram_drawing_area, TRUE);
  gtk_widget_set_vexpand(ram_drawing_area, TRUE);

  g_signal_connect(ram_drawing_area, "draw", G_CALLBACK(on_ram_draw), NULL);

  gtk_container_add(GTK_CONTAINER(ram_card), ram_drawing_area);

  gtk_box_pack_start(GTK_BOX(right_box), ram_card, TRUE, TRUE, 2);

  g_timeout_add(500, update_ram, NULL);

  dna_area = gtk_drawing_area_new();

  gtk_widget_set_hexpand(dna_area, TRUE);
  gtk_widget_set_vexpand(dna_area, TRUE);

  g_signal_connect(dna_area, "draw", G_CALLBACK(dna_draw), NULL);

  gtk_container_add(GTK_CONTAINER(dna_card), dna_area);

  gtk_box_pack_start(GTK_BOX(right_box), dna_card, TRUE, TRUE, 2);

  g_timeout_add(16, dna_tick, dna_area);

  gtk_container_add(GTK_CONTAINER(right_frame), right_box);
  gtk_grid_attach(GTK_GRID(grid), right_frame, 4, 0, 1, 1);

  /* ================= KEYBOARD ================= */

  GtkWidget *back = gtk_button_new_with_label("backspace");
  GtkWidget *kbd = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_size_request(kbd, -1, 220);

  const char *r0[] = {"`", "1", "2", "3", "4", "5", "6",
                      "7", "8", "9", "0", "-", "="};

  GtkWidget *row0 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

  for (int i = 0; i < 13; i++) {
    GtkWidget *b = gtk_button_new_with_label(r0[i]);
    g_signal_connect(b, "clicked", G_CALLBACK(on_button), (gpointer)r0[i]);
    gtk_box_pack_start(GTK_BOX(row0), b, TRUE, TRUE, 2);
  }

  gtk_box_pack_start(GTK_BOX(kbd), row0, FALSE, FALSE, 2);
  gtk_box_pack_start(GTK_BOX(row0), back, TRUE, TRUE, 2);

  const char *r1[] = {"q", "w", "e", "r", "t", "y",  "u",
                      "i", "o", "p", "[", "]", "\\", "|"};

  GtkWidget *row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

  /* TAB before Q */
  GtkWidget *tab = gtk_button_new_with_label("TAB");
  gtk_box_pack_start(GTK_BOX(row1), tab, FALSE, FALSE, 2);

  for (int i = 0; i < 14; i++) {
    GtkWidget *b = gtk_button_new_with_label(r1[i]);
    g_signal_connect(b, "clicked", G_CALLBACK(on_button), (gpointer)r1[i]);
    gtk_box_pack_start(GTK_BOX(row1), b, TRUE, TRUE, 2);
  }

  gtk_box_pack_start(GTK_BOX(kbd), row1, TRUE, FALSE, 2);

  GtkWidget *row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  GtkWidget *caps = gtk_button_new_with_label("CAPS");
  g_signal_connect(caps, "clicked", G_CALLBACK(on_caps), NULL);
  gtk_box_pack_start(GTK_BOX(row2), caps, TRUE, FALSE, 2);

  const char *r2[] = {"a", "s", "d", "f", "g", "h", "j", "k", "l"};

  for (int i = 0; i < 9; i++) {
    GtkWidget *b = gtk_button_new_with_label(r2[i]);
    g_signal_connect(b, "clicked", G_CALLBACK(on_button), (gpointer)r2[i]);
    gtk_box_pack_start(GTK_BOX(row2), b, TRUE, TRUE, 2);
  }

  GtkWidget *enter = gtk_button_new_with_label("ENTER");
  g_signal_connect(enter, "clicked", G_CALLBACK(on_button), "\n");
  gtk_box_pack_start(GTK_BOX(row2), enter, TRUE, TRUE, 2);
  gtk_box_pack_start(GTK_BOX(kbd), row2, TRUE, FALSE, 2);

  GtkWidget *row3 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  GtkWidget *shift1 = gtk_button_new_with_label("SHIFT");
  g_signal_connect(shift1, "clicked", G_CALLBACK(on_shift), NULL);
  gtk_box_pack_start(GTK_BOX(row3), shift1, TRUE, TRUE, 2);

  const char *r3[] = {"z", "x", "c", "v", "b", "n", "m", ",", ".", "/"};

  for (int i = 0; i < 10; i++) {
    GtkWidget *b = gtk_button_new_with_label(r3[i]);
    g_signal_connect(b, "clicked", G_CALLBACK(on_button), (gpointer)r3[i]);
    gtk_box_pack_start(GTK_BOX(row3), b, TRUE, TRUE, 2);
  }

  GtkWidget *shift2 = gtk_button_new_with_label("SHIFT");
  g_signal_connect(shift2, "clicked", G_CALLBACK(on_shift), NULL);
  gtk_box_pack_start(GTK_BOX(row3), shift2, TRUE, TRUE, 2);
  gtk_box_pack_start(GTK_BOX(kbd), row3, TRUE, FALSE, 2);

  GtkWidget *row4 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  GtkWidget *ctrl = gtk_button_new_with_label("CTRL");
  GtkWidget *alt = gtk_button_new_with_label("ALT");
  GtkWidget *space = gtk_button_new_with_label("SPACE");

  g_signal_connect(ctrl, "clicked", G_CALLBACK(on_ctrl), NULL);
  g_signal_connect(alt, "clicked", G_CALLBACK(on_alt), NULL);
  g_signal_connect(tab, "clicked", G_CALLBACK(on_button), "\t");
  g_signal_connect(space, "clicked", G_CALLBACK(on_button), " ");
  g_signal_connect(back, "clicked", G_CALLBACK(on_backspace), NULL);

  gtk_box_pack_start(GTK_BOX(row4), ctrl, TRUE, TRUE, 2);
  gtk_box_pack_start(GTK_BOX(row4), tab, TRUE, TRUE, 2);
  gtk_box_pack_start(GTK_BOX(row4), space, TRUE, TRUE, 2);
  gtk_box_pack_start(GTK_BOX(row4), alt, TRUE, TRUE, 2);

  /* ARROWS */

  GtkWidget *row5 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  GtkWidget *up = gtk_button_new_with_label("↑");
  GtkWidget *left = gtk_button_new_with_label("←");
  GtkWidget *down = gtk_button_new_with_label("↓");
  GtkWidget *right = gtk_button_new_with_label("→");

  g_signal_connect(up, "clicked", G_CALLBACK(on_button), "\x1b[A");
  g_signal_connect(down, "clicked", G_CALLBACK(on_button), "\x1b[B");
  g_signal_connect(right, "clicked", G_CALLBACK(on_button), "\x1b[C");
  g_signal_connect(left, "clicked", G_CALLBACK(on_button), "\x1b[D");

  gtk_box_pack_start(GTK_BOX(row5), left, TRUE, TRUE, 2);
  gtk_box_pack_start(GTK_BOX(row5), up, TRUE, TRUE, 2);
  gtk_box_pack_start(GTK_BOX(row5), down, TRUE, TRUE, 2);
  gtk_box_pack_start(GTK_BOX(row5), right, TRUE, TRUE, 2);
  gtk_box_pack_start(GTK_BOX(kbd), row5, TRUE, FALSE, 2);
  gtk_box_pack_start(GTK_BOX(kbd), row4, TRUE, FALSE, 2);

  GtkWidget *kbd_frame = gtk_frame_new("KEYBOARD &");

  GtkWidget *kbd_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  GtkWidget *file_frame = gtk_frame_new("FILE MANAGER");
  gtk_widget_set_vexpand(file_frame, TRUE);
  gtk_widget_set_hexpand(file_frame, TRUE);
  GtkWidget *fm_controls = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);

  GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_widget_set_vexpand(scroll, TRUE);
  gtk_widget_set_hexpand(scroll, TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  flow = gtk_flow_box_new();
  gtk_widget_set_vexpand(flow, TRUE);
  gtk_widget_set_hexpand(flow, TRUE);
  gtk_widget_set_can_focus(flow, TRUE);
  g_signal_connect(flow, "button-press-event", G_CALLBACK(on_flow_click), NULL);
  gtk_container_add(GTK_CONTAINER(scroll), flow);

  gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_SINGLE);
  gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 10000);
  gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 2);
  gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 2);

  GtkWidget *file_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);

  gtk_box_pack_start(GTK_BOX(file_box), fm_controls, FALSE, FALSE, 2);
  gtk_box_pack_start(GTK_BOX(file_box), scroll, TRUE, TRUE, 2);
  gtk_container_add(GTK_CONTAINER(file_frame), file_box);

  load_dir(".");
  gtk_widget_set_can_focus(flow, TRUE);
  gtk_widget_grab_focus(flow);
  gtk_widget_grab_default(flow);
  gtk_window_set_focus(GTK_WINDOW(win), flow);

  gtk_widget_set_hexpand(file_frame, TRUE);
  gtk_widget_set_vexpand(file_frame, TRUE);
  gtk_box_pack_start(GTK_BOX(kbd_row), file_frame, TRUE, TRUE, 2);
  gtk_box_pack_start(GTK_BOX(kbd_row), kbd, FALSE, FALSE, 2);
  gtk_container_add(GTK_CONTAINER(kbd_frame), kbd_row);
  gtk_widget_set_hexpand(kbd_frame, TRUE);
  gtk_widget_set_vexpand(kbd_frame, TRUE);
  gtk_grid_attach(GTK_GRID(grid), kbd_frame, 0, 2, 5, 1);
  gtk_widget_set_vexpand(kbd_frame, FALSE);
  gtk_widget_set_hexpand(kbd_frame, TRUE);
  gtk_widget_show_all(win);
  spawn_shell(global_vte);

  gtk_widget_queue_resize(term);
  gtk_widget_queue_draw(term);
  gtk_widget_grab_focus(GTK_WIDGET(global_vte));
  gtk_window_set_focus(GTK_WINDOW(win), GTK_WIDGET(global_vte));
  gtk_main();
  return 0;
}
