/*
 * Optional GTK 3 UI for building an ACARS frame and transmitting it.
 * Build target: acarstrans_gui (see CMakeLists.txt).
 */

#include <glib.h>
#include <gtk/gtk.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "acarstrans.h"
#include "modulator.h"
#include "pkt.h"
#include "transmit.h"
#include "util.h"

typedef struct {
    GtkWidget *win;
    GtkWidget *btn_send;
    GtkWidget *btn_stop;
    GtkWidget *combo_dev;
    GtkWidget *ent_uri;
    GtkWidget *ent_freq;
    GtkWidget *spin_gain;
    GtkWidget *spin_srate;
    GtkWidget *spin_bw;
    GtkWidget *chk_repeat;
    GtkWidget *chk_uplink;
    GtkWidget *ent_mode;
    GtkWidget *ent_arn;
    GtkWidget *ent_ack;
    GtkWidget *ent_label;
    GtkWidget *ent_bi;
    GtkWidget *tv_text;
    GtkWidget *ent_flight;
    GtkWidget *ent_serial;
    volatile gboolean tx_busy;
} UiCtx;

typedef struct {
    UiCtx *ui;
    message_format *mf;
    DEVICE dev;
    char device_arg[128];
    int64_t freq;
    uint32_t gain;
    uint32_t samp_rate;
    uint32_t rf_bw;
    gboolean rf_repeat;
    char ad936_path[256];
} TxJob;

static gboolean on_tx_done(gpointer data) {
    UiCtx *ui = (UiCtx *)data;
    ui->tx_busy = FALSE;
    gtk_widget_set_sensitive(ui->btn_send, TRUE);
    return G_SOURCE_REMOVE;
}

static gpointer tx_thread(gpointer data) {
    TxJob *job = data;
    UiCtx *ui   = job->ui;

    merge_elements(job->mf);
    modulate(job->mf);
    (void)transmit_run(job->mf, job->dev, job->device_arg[0] ? job->device_arg : NULL, job->freq, job->gain,
                       job->samp_rate, job->rf_bw, job->rf_repeat ? true : false,
                       job->ad936_path[0] ? job->ad936_path : NULL);

    free(job->mf);
    free(job);
    g_idle_add(on_tx_done, ui);
    return NULL;
}

static void get_text_view_utf8(GtkWidget *tv, char *dst, size_t dst_len) {
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));
    GtkTextIter s, e;
    gtk_text_buffer_get_start_iter(buf, &s);
    gtk_text_buffer_get_end_iter(buf, &e);
    gchar *txt = gtk_text_buffer_get_text(buf, &s, &e, FALSE);
    if (!txt) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, txt, dst_len - 1);
    dst[dst_len - 1] = '\0';
    g_free(txt);
}

static void on_stop_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    (void)user_data;
    rf_tx_request_stop();
}

static void on_send_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    UiCtx *ui = (UiCtx *)user_data;

    if (ui->tx_busy) {
        return;
    }

    char err[512] = {0};
    message_format *mf = calloc(1, sizeof(*mf));
    if (!mf) {
        return;
    }

    const gboolean uplink = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui->chk_uplink));
    const gchar *mode_txt = gtk_entry_get_text(GTK_ENTRY(ui->ent_mode));
    const gchar *arn      = gtk_entry_get_text(GTK_ENTRY(ui->ent_arn));
    const gchar *ack      = gtk_entry_get_text(GTK_ENTRY(ui->ent_ack));
    const gchar *lab      = gtk_entry_get_text(GTK_ENTRY(ui->ent_label));
    const gchar *bi       = gtk_entry_get_text(GTK_ENTRY(ui->ent_bi));
    const gchar *flight   = gtk_entry_get_text(GTK_ENTRY(ui->ent_flight));
    const gchar *serial   = gtk_entry_get_text(GTK_ENTRY(ui->ent_serial));

    char text_buf[TEXT_MAX_LEN + 1];
    get_text_view_utf8(ui->tv_text, text_buf, sizeof(text_buf));

    const mf_manual_params mp = {.is_uplink    = uplink ? true : false,
                                 .mode         = (mode_txt && mode_txt[0]) ? (uint8_t)mode_txt[0] : (uint8_t)'2',
                                 .arn_input    = arn ? arn : "",
                                 .ack_input    = ack ? ack : "",
                                 .label        = lab ? lab : "",
                                 .bi_input     = bi ? bi : "",
                                 .text         = text_buf,
                                 .flight_id    = uplink ? "" : (flight ? flight : ""),
                                 .serial_input = uplink ? "" : (serial ? serial : "")};

    if (mf_fill_manual(mf, &mp, err, sizeof(err)) != AT_OK) {
        GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(ui->win), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                                                GTK_BUTTONS_CLOSE, "%s", err[0] ? err : "Invalid message fields.");
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
        free(mf);
        return;
    }

    TxJob *job = calloc(1, sizeof(*job));
    if (!job) {
        free(mf);
        return;
    }

    job->ui = ui;
    job->mf = mf;

    const gint dev_ix = gtk_combo_box_get_active(GTK_COMBO_BOX(ui->combo_dev));
    job->dev          = (dev_ix == 1) ? USRP : AD936X;

    const gchar *uri = gtk_entry_get_text(GTK_ENTRY(ui->ent_uri));
    if (uri && uri[0]) {
        strncpy(job->device_arg, uri, sizeof(job->device_arg) - 1);
    }

    const gchar *ftxt = gtk_entry_get_text(GTK_ENTRY(ui->ent_freq));
    char fbuf[96] = {0};
    if (ftxt)
        strncpy(fbuf, ftxt, sizeof(fbuf) - 1);
    if (!fbuf[0] || parse_frequency_i64(fbuf, NULL, &job->freq) != 0) {
        job->freq = 131450000;
    }

    job->gain        = (uint32_t)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ui->spin_gain));
    job->samp_rate   = (uint32_t)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ui->spin_srate));
    job->rf_bw       = (uint32_t)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ui->spin_bw));
    job->rf_repeat   = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui->chk_repeat));
    job->ad936_path[0] = '\0';

    ui->tx_busy = TRUE;
    gtk_widget_set_sensitive(ui->btn_send, FALSE);

    GError *thr_err = NULL;
    GThread *t      = g_thread_try_new("acars_tx", tx_thread, job, &thr_err);
    if (!t) {
        ui->tx_busy = FALSE;
        gtk_widget_set_sensitive(ui->btn_send, TRUE);
        free(job->mf);
        free(job);
        GtkWidget *d =
            gtk_message_dialog_new(GTK_WINDOW(ui->win), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                                   "Could not start TX thread: %s", thr_err ? thr_err->message : "unknown");
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
        if (thr_err)
            g_error_free(thr_err);
        return;
    }
    g_thread_unref(t);
}

static void attach_labeled(GtkGrid *grid, int row, const char *label, GtkWidget *w) {
    GtkWidget *l = gtk_label_new(label);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_grid_attach(grid, l, 0, row, 1, 1);
    gtk_grid_attach(grid, w, 1, row, 1, 1);
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;

    UiCtx *ui = calloc(1, sizeof(*ui));
    GtkWidget *win = gtk_application_window_new(app);
    ui->win        = win;
    gtk_window_set_title(GTK_WINDOW(win), "ACARS 发射端（GUI）");
    gtk_window_set_default_size(GTK_WINDOW(win), 560, 720);

    GtkWidget *vroot = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vroot), 10);

    GtkWidget *subtitle = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(subtitle),
                         "<span weight='bold'>图形界面填写参数发送 · 对应原终端 <tt>acarstrans_exe</tt> 手动模式</span>");
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vroot), subtitle, FALSE, FALSE, 0);

    GtkWidget *nb = gtk_notebook_new();
    gtk_widget_set_vexpand(nb, TRUE);
    gtk_box_pack_start(GTK_BOX(vroot), nb, TRUE, TRUE, 0);

    /* ----- 标签页：射频 ----- */
    GtkWidget *grid_rf = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid_rf), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid_rf), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid_rf), 8);
    int row = 0;

    ui->combo_dev = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui->combo_dev), "AD936x / Pluto (libiio)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui->combo_dev), "USRP (UHD)");
    gtk_combo_box_set_active(GTK_COMBO_BOX(ui->combo_dev), 0);
    attach_labeled(GTK_GRID(grid_rf), row++, "射频前端（Device）", ui->combo_dev);

    ui->ent_uri = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ui->ent_uri), "设备 URI / USRP args，可留空");
    attach_labeled(GTK_GRID(grid_rf), row++, "设备参数（URI / args）", ui->ent_uri);

    ui->ent_freq = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(ui->ent_freq), "131450000");
    attach_labeled(GTK_GRID(grid_rf), row++, "频率（Freq，Hz）", ui->ent_freq);

    ui->spin_gain = gtk_spin_button_new_with_range(0, 47, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->spin_gain), 30);
    attach_labeled(GTK_GRID(grid_rf), row++, "增益（Gain，dB）", ui->spin_gain);

    ui->spin_srate = gtk_spin_button_new_with_range(100000, 20000000, 1200);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->spin_srate), SAMP_RATE);
    attach_labeled(GTK_GRID(grid_rf), row++, "采样率（SampleRate，Hz）", ui->spin_srate);

    ui->spin_bw = gtk_spin_button_new_with_range(0, 20000000, 1200);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->spin_bw), 0);
    attach_labeled(GTK_GRID(grid_rf), row++, "射频带宽（RF BW，0=同采样率）", ui->spin_bw);

    ui->chk_repeat = gtk_check_button_new_with_label("连续发送（Repeat：USRP 循环突发 / AD936x 重复直到停止）");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->chk_repeat), FALSE);
    gtk_grid_attach(GTK_GRID(grid_rf), ui->chk_repeat, 0, row++, 2, 1);

    GtkWidget *sw_rf = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_rf), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sw_rf), grid_rf);
    gtk_widget_show(grid_rf);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), sw_rf, gtk_label_new("射频 / 硬件"));

    /* ----- 标签页：报文 ----- */
    GtkWidget *grid_msg = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid_msg), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid_msg), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid_msg), 8);
    row = 0;

    ui->chk_uplink = gtk_check_button_new_with_label("上行链路（Uplink）");
    gtk_grid_attach(GTK_GRID(grid_msg), ui->chk_uplink, 0, row++, 2, 1);

    ui->ent_mode = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(ui->ent_mode), 1);
    gtk_entry_set_text(GTK_ENTRY(ui->ent_mode), "2");
    attach_labeled(GTK_GRID(grid_msg), row++, "模式（Mode，填 2）", ui->ent_mode);

    ui->ent_arn = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ui->ent_arn), "如 B-1851");
    attach_labeled(GTK_GRID(grid_msg), row++, "飞机地址（Address）", ui->ent_arn);

    ui->ent_ack = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ui->ent_ack), "留空 = NAK");
    attach_labeled(GTK_GRID(grid_msg), row++, "确认字符（ACK）", ui->ent_ack);

    ui->ent_label = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(ui->ent_label), 2);
    gtk_entry_set_placeholder_text(GTK_ENTRY(ui->ent_label), "如 QF");
    attach_labeled(GTK_GRID(grid_msg), row++, "报文标签（Label）", ui->ent_label);

    ui->ent_bi = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ui->ent_bi), "下行填 0-9；上行填 A-Z；留空=NAK");
    attach_labeled(GTK_GRID(grid_msg), row++, "块标识（BI）", ui->ent_bi);

    gtk_grid_attach(GTK_GRID(grid_msg), gtk_label_new("报文正文（TEXT）"), 0, row, 1, 1);
    ui->tv_text = gtk_text_view_new();
    gtk_widget_set_hexpand(ui->tv_text, TRUE);
    gtk_widget_set_vexpand(ui->tv_text, TRUE);
    gtk_widget_set_size_request(ui->tv_text, -1, 140);
    GtkWidget *sw_txt = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_txt), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sw_txt), ui->tv_text);
    gtk_grid_attach(GTK_GRID(grid_msg), sw_txt, 1, row++, 1, 1);

    ui->ent_flight = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(ui->ent_flight), 6);
    gtk_entry_set_placeholder_text(GTK_ENTRY(ui->ent_flight), "下行必填，如 GS7584");
    attach_labeled(GTK_GRID(grid_msg), row++, "航班号（Flight ID）", ui->ent_flight);

    ui->ent_serial = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(ui->ent_serial), 4);
    gtk_entry_set_placeholder_text(GTK_ENTRY(ui->ent_serial), "下行必填，如 M95A");
    gtk_entry_set_text(GTK_ENTRY(ui->ent_serial), "M01A");
    attach_labeled(GTK_GRID(grid_msg), row++, "报文序号（MsgNo）", ui->ent_serial);

    GtkWidget *sw_msg = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_msg), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sw_msg), grid_msg);
    gtk_widget_show(grid_msg);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), sw_msg, gtk_label_new("报文字段（ACARS）"));

    GtkWidget *bbox = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTON_BOX_END);
    gtk_widget_set_margin_top(bbox, 4);

    ui->btn_send = gtk_button_new_with_label("发送");
    ui->btn_stop = gtk_button_new_with_label("停止发送");
    gtk_container_add(GTK_CONTAINER(bbox), ui->btn_stop);
    gtk_container_add(GTK_CONTAINER(bbox), ui->btn_send);
    gtk_box_pack_start(GTK_BOX(vroot), bbox, FALSE, FALSE, 0);

    g_signal_connect(ui->btn_send, "clicked", G_CALLBACK(on_send_clicked), ui);
    g_signal_connect(ui->btn_stop, "clicked", G_CALLBACK(on_stop_clicked), NULL);

    gtk_container_add(GTK_CONTAINER(win), vroot);
    gtk_widget_show_all(win);
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("com.cauc.acarstrans.gui", (GApplicationFlags)0);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    const int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
