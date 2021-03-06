/**
 * Copyright (C) 2013 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <stdio.h>

#include <gtk/gtk.h>
#include <gtkdatabox.h>
#include <gtkdatabox_grid.h>
#include <gtkdatabox_points.h>
#include <gtkdatabox_lines.h>
#include <math.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <malloc.h>

#include "../osc.h"
#include "../iio_widget.h"
#include "../iio_utils.h"
#include "../osc_plugin.h"
#include "../config.h"

static struct iio_widget tx_widgets[50];
static unsigned int num_tx;
static gboolean pid_controller_exists;
static gboolean advanced_controller_exists;
static char crt_device_name[100];

/* Global Widgets */
static GtkWidget *controllers_notebook;
static GtkWidget *gpo[11];
static int gpo_id[11];

/* PID Controller Widgets */
static GtkWidget *controller_type_pid;
static GtkWidget *pwm_pid;
static GtkWidget *direction_pid;
static GtkWidget *ref_speed;
static GtkWidget *kp;
static GtkWidget *ki;
static GtkWidget *kd;

/* Advanced Controller Widgets */
static GtkWidget *command;
static GtkWidget *velocity_p;
static GtkWidget *velocity_i;
static GtkWidget *current_p;
static GtkWidget *current_i;
static GtkWidget *controller_mode;
static GtkWidget *openloop_bias;
static GtkWidget *openloop_scalar;
static GtkWidget *zero_offset;

#define USE_PWM_PERCENT_MODE -1
#define PWM_FULL_SCALE	2047
static int Kxy_NUM_FRAC_BITS = 14;
static int PWM_PERCENT_FLAG = -1;

static int COMMAND_NUM_FRAC_BITS = 8;
static int VELOCITY_P_NUM_FRAC_BITS = 16;
static int VELOCITY_I_NUM_FRAC_BITS = 15;
static int CURRENT_P_NUM_FRAC_BITS = 10;
static int CURRENT_I_NUM_FRAC_BITS = 2;
static int OPEN_LOOP_BIAS_NUM_FRAC_BITS = 14;
static int OPEN_LOOP_SCALAR_NUM_FRAC_BITS = 16;
static int OENCODER_NUM_FRAC_BITS = 14;

static void tx_update_values(void)
{
	iio_update_widgets(tx_widgets, num_tx);
}

void save_widget_value(GtkWidget *widget, struct iio_widget *iio_w)
{
	set_dev_paths(crt_device_name);
	iio_w->save(iio_w);
}

static gboolean change_controller_type_label(GBinding *binding,
	const GValue *source_value, GValue *target_value, gpointer data)
{
	if (g_value_get_boolean(source_value))
		g_value_set_static_string(target_value, "PID Controller");
	else
		g_value_set_static_string(target_value, "Manual PWM");

	return TRUE;
}

static gboolean change_direction_label(GBinding *binding,
	const GValue *source_value, GValue *target_value, gpointer data)
{
	if (g_value_get_boolean(source_value))
		g_value_set_static_string(target_value, "Clockwise");
	else
		g_value_set_static_string(target_value, "Counterclockwise");

	return TRUE;
}

static gint spin_input_cb(GtkSpinButton *btn, gpointer new_value, gpointer data)
{
	gdouble value;
	gdouble fractpart;
	gdouble intpart;
	int32_t intvalue;
	const char *entry_buf;
	int fract_bits = *((int *)data);

	entry_buf = gtk_entry_get_text(GTK_ENTRY(btn));
	if (*((int *)data) == USE_PWM_PERCENT_MODE) {
		value = g_strtod(entry_buf, NULL);
		intvalue = (int32_t)(((value * PWM_FULL_SCALE) / 100.0) + 0.5);
	} else {
		value = g_strtod(entry_buf, NULL);
		fractpart = modf(value, &intpart);
		fractpart = ((1 << fract_bits) * fractpart) + 0.5;
		intvalue = ((int32_t)intpart << fract_bits) | (int32_t)fractpart;
	}
	*((gdouble *)new_value) = (gdouble)intvalue;

	return TRUE;
}

static gboolean spin_output_cb(GtkSpinButton *spin, gpointer data)
{
	GtkAdjustment *adj;
	gchar *text;
	int value;
	gdouble fvalue;
	int fract_bits = *((int *)data);

	adj = gtk_spin_button_get_adjustment(spin);
	value = (int)gtk_adjustment_get_value(adj);
	if (*((int *)data) == USE_PWM_PERCENT_MODE) {
		fvalue = ((float)value / (float)PWM_FULL_SCALE) * 100;
		text = g_strdup_printf("%.2f%%", fvalue);
	} else {
		fvalue = (value >> fract_bits) + (gdouble)(value & ((1 << fract_bits) - 1)) / (gdouble)(1 << fract_bits);
		text = g_strdup_printf("%.5f", fvalue);
	}
	gtk_entry_set_text(GTK_ENTRY(spin), text);
	g_free(text);

	return TRUE;
}

static void gpo_toggled_cb(GtkToggleButton *btn, gpointer data)
{
	int id = *((int *)data);
	int attribute_value;

	if (pid_controller_exists) {
		set_dev_paths("ad-mc-pid-ctrl");
		read_devattr_int("mc_pid_ctrl_gpo", &attribute_value);
		if (gtk_toggle_button_get_active(btn))
			attribute_value |= (1ul << id);
		else
			attribute_value &= ~(1ul << id);
		write_devattr_int("mc_pid_ctrl_gpo", attribute_value);
	}
	if (advanced_controller_exists) {
		set_dev_paths("ad-mc-adv-ctrl");
		read_devattr_int("mc_adv_ctrl_gpo", &attribute_value);
		if (gtk_toggle_button_get_active(btn))
			attribute_value |= (1ul << id);
		else
			attribute_value &= ~(1ul << id);
		write_devattr_int("mc_adv_ctrl_gpo", attribute_value);
	}
}

void create_iio_bindings_for_pid_ctrl(GtkBuilder *builder)
{
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-pid-ctrl", "mc_pid_ctrl_run",
		builder, "checkbutton_run", 0);

	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-pid-ctrl", "mc_pid_ctrl_delta",
		builder, "checkbutton_delta", 0);

	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-pid-ctrl", "mc_pid_ctrl_direction",
		builder, "togglebtn_direction", 0);
	direction_pid = tx_widgets[num_tx - 1].widget;

	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-pid-ctrl", "mc_pid_ctrl_matlab",
		builder, "togglebtn_controller_type", 0);
	controller_type_pid = tx_widgets[num_tx - 1].widget;

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-pid-ctrl", "mc_pid_ctrl_ref_speed",
		builder, "spinbutton_ref_speed", NULL);
	ref_speed = tx_widgets[num_tx - 1].widget;

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-pid-ctrl", "mc_pid_ctrl_kp",
		builder, "spinbutton_kp", NULL);
	kp = tx_widgets[num_tx - 1].widget;

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-pid-ctrl", "mc_pid_ctrl_ki",
		builder, "spinbutton_ki", NULL);
	ki = tx_widgets[num_tx - 1].widget;

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-pid-ctrl", "mc_pid_ctrl_kd",
		builder, "spinbutton_kd", NULL);
	kd = tx_widgets[num_tx - 1].widget;

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-pid-ctrl", "mc_pid_ctrl_pwm",
		builder, "spinbutton_pwm", NULL);
	pwm_pid = tx_widgets[num_tx - 1].widget;

	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-pid-ctrl", "mc_pid_ctrl_sensors",
		"mc_pid_ctrl_sensors_available", builder,
		"comboboxtext_sensors", NULL);
}

void create_iio_bindings_for_advanced_ctrl(GtkBuilder *builder)
{
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-adv-ctrl", "mc_adv_ctrl_run",
		builder, "checkbutton_run_adv", 0);

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-adv-ctrl", "mc_adv_ctrl_command",
		builder, "spinbutton_command", NULL);
	command = tx_widgets[num_tx - 1].widget;

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-adv-ctrl", "mc_adv_ctrl_velocity_p_gain",
		builder, "spinbutton_velocity_p", NULL);
	velocity_p = tx_widgets[num_tx - 1].widget;

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-adv-ctrl", "mc_adv_ctrl_velocity_i_gain",
		builder, "spinbutton_velocity_i", NULL);
	velocity_i = tx_widgets[num_tx - 1].widget;

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-adv-ctrl", "mc_adv_ctrl_current_p_gain",
		builder, "spinbutton_current_p", NULL);
	current_p = tx_widgets[num_tx - 1].widget;

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-adv-ctrl", "mc_adv_ctrl_current_i_gain",
		builder, "spinbutton_current_i", NULL);
	current_i = tx_widgets[num_tx - 1].widget;

	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-adv-ctrl", "mc_adv_ctrl_controller_mode",
		"mc_adv_ctrl_controller_mode_available", builder,
		"combobox_controller_mode", NULL);
	controller_mode = tx_widgets[num_tx - 1].widget;

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-adv-ctrl", "mc_adv_ctrl_open_loop_bias",
		builder, "spinbutton_open_loop_bias", NULL);
	openloop_bias = tx_widgets[num_tx - 1].widget;

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-adv-ctrl", "mc_adv_ctrl_open_loop_scalar",
		builder, "spinbutton_open_loop_scalar", NULL);
	openloop_scalar = tx_widgets[num_tx - 1].widget;

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-adv-ctrl", "mc_adv_ctrl_encoder_zero_offset",
		builder, "spinbutton_zero_offset", NULL);
	zero_offset = tx_widgets[num_tx - 1].widget;
}

static void controllers_notebook_page_switched_cb (GtkNotebook *notebook,
	GtkWidget *page, guint page_num, gpointer user_data)
{
	const gchar *page_name;

	page_name = gtk_notebook_get_tab_label_text(notebook, page);
	if (!strcmp(page_name, "PID"))
		sprintf(crt_device_name, "%s", "ad-mc-pid-ctrl");
	else if (!strcmp(page_name, "Advanced"))
		sprintf(crt_device_name, "%s", "ad-mc-adv-ctrl");
	else
		printf("Notebook page is unknown to the Motor Control Plugin\n");
}

static void pid_controller_init(GtkBuilder *builder)
{
	GtkWidget *box_manpwm_pid_widgets;
	GtkWidget *box_manpwm_pid_lbls;
	GtkWidget *box_controller_pid_widgets;
	GtkWidget *box_controller_pid_lbls;

	box_manpwm_pid_widgets = GTK_WIDGET(gtk_builder_get_object(builder, "vbox_manual_pwm_widgets"));
	box_manpwm_pid_lbls = GTK_WIDGET(gtk_builder_get_object(builder, "vbox_manual_pwm_lbls"));
	box_controller_pid_widgets = GTK_WIDGET(gtk_builder_get_object(builder, "vbox_torque_ctrl_widgets"));
	box_controller_pid_lbls = GTK_WIDGET(gtk_builder_get_object(builder, "vbox_torque_ctrl_lbls"));

	/* Bind the IIO device files to the GUI widgets */
	create_iio_bindings_for_pid_ctrl(builder);

	/* Connect signals. */
	g_signal_connect(G_OBJECT(pwm_pid), "input", G_CALLBACK(spin_input_cb), &PWM_PERCENT_FLAG);
	g_signal_connect(G_OBJECT(pwm_pid), "output", G_CALLBACK(spin_output_cb), &PWM_PERCENT_FLAG);
	g_signal_connect(G_OBJECT(kp), "input", G_CALLBACK(spin_input_cb), &Kxy_NUM_FRAC_BITS);
	g_signal_connect(G_OBJECT(kp), "output", G_CALLBACK(spin_output_cb), &Kxy_NUM_FRAC_BITS);
	g_signal_connect(G_OBJECT(ki), "input", G_CALLBACK(spin_input_cb), &Kxy_NUM_FRAC_BITS);
	g_signal_connect(G_OBJECT(ki), "output", G_CALLBACK(spin_output_cb), &Kxy_NUM_FRAC_BITS);
	g_signal_connect(G_OBJECT(kd), "input", G_CALLBACK(spin_input_cb), &Kxy_NUM_FRAC_BITS);
	g_signal_connect(G_OBJECT(kd), "output", G_CALLBACK(spin_output_cb), &Kxy_NUM_FRAC_BITS);

	/* Bind properties. */

	/* Show widgets listed below when in "PID Controller" state */
	g_object_bind_property(controller_type_pid, "active", box_controller_pid_widgets, "visible", 0);
	g_object_bind_property(controller_type_pid, "active", box_controller_pid_lbls, "visible", 0);
	/* Show widgets listed below when in "Manual PWM" state */
	g_object_bind_property(controller_type_pid, "active", box_manpwm_pid_widgets, "visible", G_BINDING_INVERT_BOOLEAN);
	g_object_bind_property(controller_type_pid, "active", box_manpwm_pid_lbls, "visible", G_BINDING_INVERT_BOOLEAN);
	/* Change between "PID Controller" and "Manual PWM" labels on a toggle button */
	g_object_bind_property_full(controller_type_pid, "active", controller_type_pid, "label", 0, change_controller_type_label, NULL, NULL, NULL);
	/* Change direction label between "CW" and "CCW" */
	g_object_bind_property_full(direction_pid, "active", direction_pid, "label", 0, change_direction_label, NULL, NULL, NULL);
}

static void advanced_controller_init(GtkBuilder *builder)
{
	/* Bind the IIO device files to the GUI widgets */
	create_iio_bindings_for_advanced_ctrl(builder);

	/* Connect signals. */
	g_signal_connect(G_OBJECT(command), "input", G_CALLBACK(spin_input_cb), &COMMAND_NUM_FRAC_BITS);
	g_signal_connect(G_OBJECT(command), "output", G_CALLBACK(spin_output_cb), &COMMAND_NUM_FRAC_BITS);
	g_signal_connect(G_OBJECT(velocity_p), "input", G_CALLBACK(spin_input_cb), &VELOCITY_P_NUM_FRAC_BITS);
	g_signal_connect(G_OBJECT(velocity_p), "output", G_CALLBACK(spin_output_cb), &VELOCITY_P_NUM_FRAC_BITS);
	g_signal_connect(G_OBJECT(velocity_i), "input", G_CALLBACK(spin_input_cb), &VELOCITY_I_NUM_FRAC_BITS);
	g_signal_connect(G_OBJECT(velocity_i), "output", G_CALLBACK(spin_output_cb), &VELOCITY_I_NUM_FRAC_BITS);
	g_signal_connect(G_OBJECT(current_p), "input", G_CALLBACK(spin_input_cb), &CURRENT_P_NUM_FRAC_BITS);
	g_signal_connect(G_OBJECT(current_p), "output", G_CALLBACK(spin_output_cb), &CURRENT_P_NUM_FRAC_BITS);
	g_signal_connect(G_OBJECT(current_i), "input", G_CALLBACK(spin_input_cb), &CURRENT_I_NUM_FRAC_BITS);
	g_signal_connect(G_OBJECT(current_i), "output", G_CALLBACK(spin_output_cb), &CURRENT_I_NUM_FRAC_BITS);
	g_signal_connect(G_OBJECT(openloop_bias), "input", G_CALLBACK(spin_input_cb), &OPEN_LOOP_BIAS_NUM_FRAC_BITS);
	g_signal_connect(G_OBJECT(openloop_bias), "output", G_CALLBACK(spin_output_cb), &OPEN_LOOP_BIAS_NUM_FRAC_BITS);
	g_signal_connect(G_OBJECT(openloop_scalar), "input", G_CALLBACK(spin_input_cb), &OPEN_LOOP_SCALAR_NUM_FRAC_BITS);
	g_signal_connect(G_OBJECT(openloop_scalar), "output", G_CALLBACK(spin_output_cb), &OPEN_LOOP_SCALAR_NUM_FRAC_BITS);
	g_signal_connect(G_OBJECT(zero_offset), "input", G_CALLBACK(spin_input_cb), &OENCODER_NUM_FRAC_BITS);
	g_signal_connect(G_OBJECT(zero_offset), "output", G_CALLBACK(spin_output_cb), &OENCODER_NUM_FRAC_BITS);
}

static int motor_control_init(GtkWidget *notebook)
{
	GtkBuilder *builder;
	GtkWidget *motor_control_panel;
	GtkWidget *pid_page;
	GtkWidget *advanced_page;
	int i;

	builder = gtk_builder_new();
	if (!gtk_builder_add_from_file(builder, "motor_control.glade", NULL))
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "motor_control.glade", NULL);

	motor_control_panel = GTK_WIDGET(gtk_builder_get_object(builder, "tablePanelMotor_Control"));
	controllers_notebook = GTK_WIDGET(gtk_builder_get_object(builder, "notebook_controllers"));

	pid_page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(controllers_notebook), 0);
	advanced_page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(controllers_notebook), 1);

	if (pid_controller_exists)
		pid_controller_init(builder);
	else
		gtk_widget_hide(pid_page);
	if (advanced_controller_exists)
		advanced_controller_init(builder);
	else
		gtk_widget_hide(advanced_page);

	/* Connect signals. */

	/* Signal connections for GPOs */
	char widget_name[25];
	for (i = 0; i < sizeof(gpo)/sizeof(gpo[0]); i++) {
		sprintf(widget_name, "checkbutton_gpo%d", i+1);
		gpo_id[i] = i;
		gpo[i] = GTK_WIDGET(gtk_builder_get_object(builder, widget_name));
		g_signal_connect(G_OBJECT(gpo[i]), "toggled", G_CALLBACK(gpo_toggled_cb), &gpo_id[i]);
	}

	/* Signal connections for the rest of the widgets */
	char signal_name[25];
	for (i = 0; i < num_tx; i++) {
		if (GTK_IS_CHECK_BUTTON(tx_widgets[i].widget))
			sprintf(signal_name, "%s", "toggled");
		else if (GTK_IS_TOGGLE_BUTTON(tx_widgets[i].widget))
			sprintf(signal_name, "%s", "toggled");
		else if (GTK_IS_SPIN_BUTTON(tx_widgets[i].widget))
			sprintf(signal_name, "%s", "value-changed");
		else if (GTK_IS_COMBO_BOX_TEXT(tx_widgets[i].widget))
			sprintf(signal_name, "%s", "changed");

		g_signal_connect(G_OBJECT(tx_widgets[i].widget), signal_name, G_CALLBACK(save_widget_value), &tx_widgets[i]);
	}

	g_signal_connect(G_OBJECT(controllers_notebook), "switch-page",
		G_CALLBACK(controllers_notebook_page_switched_cb), NULL);

	tx_update_values();
	gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(controller_mode), 1);
	gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(controller_mode), 0);

	gint p;
	p = gtk_notebook_get_current_page(GTK_NOTEBOOK(controllers_notebook));
	controllers_notebook_page_switched_cb(GTK_NOTEBOOK(controllers_notebook),
		gtk_notebook_get_nth_page(GTK_NOTEBOOK(controllers_notebook), p), p, NULL);

	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), motor_control_panel, NULL);
	gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(notebook), motor_control_panel, "Motor Control");

	return 0;
}

static bool motor_control_identify(void)
{
	if (!set_dev_paths("ad-mc-pid-ctrl"))
		pid_controller_exists = true;
	else
		pid_controller_exists = false;
	if(!set_dev_paths("ad-mc-adv-ctrl"))
		advanced_controller_exists = true;
	else
		advanced_controller_exists = false;

	return pid_controller_exists || advanced_controller_exists;
}

struct osc_plugin plugin = {
	.name = "Motor Control",
	.identify = motor_control_identify,
	.init = motor_control_init,
};
