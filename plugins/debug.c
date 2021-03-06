/**
* Copyright (C) 2012-2013 Analog Devices, Inc.
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
#include <libxml/xpath.h>

#include "../osc.h"
#include "../xml_utils.h"
#include "../iio_utils.h"
#include "../osc_plugin.h"
#include "../config.h"

typedef struct _reg reg;
typedef struct _bgroup bgroup;
typedef struct _option option;

struct _reg
{
	char   *name;        /* name of the register */
	char   *notes;       /* additional notes */
	char   *width;       /* width of the register */
	int    def_val;      /* default value of the register */
	int    bgroup_cnt;   /* total count of bit groups */
	bgroup *bgroup_list; /* list of the bit groups */
};

struct _bgroup
{
	char   *name;        /* name of the bit group */
	int    width;        /* width of the bit group */
	int    offset;       /* register start position of the bit group */
	int    def_val;      /* default value of the bit group */
	char   *access;      /* access type of the bit group */
	char   *description; /* description of the bit group */
	int    options_cnt;  /* total count of the options available for the bit group */
	option *option_list; /* list of the options */
};

struct _option
{
	char *text; /* option description */
	int  value; /* option value */
};

/* GUI widgets */
static GtkWidget *combobox_device_list;
static GtkWidget *spin_btn_reg_addr;
static GtkWidget *spin_btn_reg_value;
static GtkWidget *label_reg_hex_addr;
static GtkWidget *label_reg_hex_value;
static GtkWidget *btn_read_reg;
static GtkWidget *btn_write_reg;
static GtkWidget *label_reg_descrip;
static GtkWidget *label_reg_def_val;
static GtkWidget *label_reg_notes;
static GtkWidget *label_notes_tag;
static GtkWidget *warning_label;
static GtkWidget *reg_autoread;
static GtkWidget *frm_regmaptype;
static GtkWidget *spi_regmap;
static GtkWidget *axicore_regmap;

/* IIO Scan Elements widgets */
static GtkWidget *scanel_read;
static GtkWidget *scanel_write;
static GtkWidget *scanel_value;
static GtkWidget *combobox_debug_scanel;
static GtkWidget *scanel_options;
static gulong debug_scanel_hid;
char *current_elements;

/* Register map widgets */
static GtkWidget *scrollwin_regmap;
static GtkWidget *reg_map_container;
static GtkWidget *hbox_bits_container;
static GtkWidget **lbl_bits;
static GtkWidget **hboxes;
static GtkWidget **vboxes;
static GtkWidget **bit_descrip_list;
static GtkWidget **elem_frames;
static GtkWidget **bit_comboboxes;
static GtkWidget **bit_no_read_lbl;
static GtkWidget **bit_spinbuttons;
static GtkWidget **bit_spin_adjustments;

/* Register map variables */
static int *reg_addr_list;     /* Pointer to the list of addresses of all registers */
static int reg_list_size;      /* Number of register addresses */
static int reg_bit_width;      /* The size in bits that all registers have in common */
static reg soft_reg;           /* Holds all information of a register and of the contained bits  */
static gulong reg_addr_hid;    /* The handler id of the register address spin button */
static gulong reg_val_hid;     /* The handler id of the register value spin button */
static gulong *combo_hid_list; /* Handler ids of the bit options(comboboxes) */
static gulong *spin_hid_list;  /* Handler ids of the bit options(spinbuttons) */

/* XML data pointers */
static xmlDocPtr xml_doc; /* The reference to the xml file */
static xmlNodePtr root; /* The reference to the first element in the xml file */
static xmlXPathObjectPtr register_list; /* List of register references */

static int context_created = 0; /* register data allocation flag */
static int soft_reg_exists = 0; /* keeps track of alloc-free calls */
static int xml_file_opened = 0; /* a open file flag */

/* The path of the directory containing the xml files. */
static char xmls_folder_path[512];

/******************************************************************************/
/**************************** Functions prototypes ****************************/
/******************************************************************************/

static int alloc_widget_arrays(int reg_length);
static void free_widget_arrays(void);
static void fill_soft_register(reg *p_reg, int reg_index);
static void free_soft_register(reg *p_reg);
static int display_reg_info(int pos_reg_addr);
static void block_bit_option_signals(void);
static void unblock_bit_option_signals(void);
static int get_reg_default_val(reg *p_reg);
static int get_option_index(int option_value, bgroup *bit);
static void draw_reg_map(int valid_register);
static void reveal_reg_map(void);
static void hide_reg_map(void);
static void create_reg_map(void);
static void clean_gui_reg_info(void);
static int get_default_reg_width(void);
static int * get_reg_addr_list(void);
static int get_reg_pos(int *regList, int reg_addr);
static int update_regmap(int data);
static void create_device_context(void);
static void destroy_device_context(void);
static void destroy_regmap_widgets(void);

/******************************************************************************/
/******************************** Callbacks ***********************************/
/******************************************************************************/
static void scanel_read_clicked(GtkButton *btn, gpointer data)
{
	char *scanel;
	char *dev_name;
	char *basedir;
	char *buf = NULL, *buf2 = NULL;
	char *start, *end, cal_name[256], tmp[256];
	int dev_num, i;
	GtkListStore *store;

	dev_name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combobox_device_list));
	dev_num = find_type_by_name(dev_name, "iio:device");

	if (dev_num >= 0) {
		scanel = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combobox_debug_scanel));
		basedir = malloc (1024);
		sprintf(basedir,"%siio:device%i", iio_dir, dev_num);

		if (scanel[strlen(scanel) - 1] != ' ') {
			gtk_widget_show(scanel_value);
			gtk_widget_hide(scanel_options);
			read_sysfs_string(scanel, basedir, &buf);
			if (buf) {
				gtk_entry_set_text(GTK_ENTRY(scanel_value), buf);
				free (buf);
			}
		} else {
			gtk_widget_show(scanel_options);
			gtk_widget_hide(scanel_value);

			scanel[strlen(scanel) - 1] = 0;
			start = strstr(current_elements, scanel);
			start = strchr(start, ' ') + 1;
			end = strchr(start, ' ');
			sprintf(cal_name, "%.*s", (int)(end - start), start);
			read_sysfs_string(scanel, basedir, &buf2);
			read_sysfs_string(cal_name, basedir, &buf);
			if (buf && buf2) {
				while(isspace(buf[strlen(buf) - 1]))
					buf[strlen(buf) - 1] = 0;
				store = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(scanel_options)));
				gtk_list_store_clear (store);
				start = buf;
				i = 0;
				while (start[0] != 0) {
					end = strchr(start, ' ');
					if (!end)
						end = buf + strlen(buf);
					sprintf(tmp, "%.*s", (int)(end - start), start);
					gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scanel_options),
						 (const gchar *)tmp);
					start = end + 1;
					if (!strcmp(buf2, tmp)) {
						gtk_combo_box_set_active(GTK_COMBO_BOX(scanel_options), i);
					}
					i++;
				}
				free(buf);
				free(buf2);
			}

		}
		free(basedir);
	}

}

static void scanel_write_clicked(GtkButton *btn, gpointer data)
{
	char *scanel;
	char *dev_name;
	char *basedir;
	const char *buf = NULL;
	int dev_num;

	dev_name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combobox_device_list));
	dev_num = find_type_by_name(dev_name, "iio:device");
	if (dev_num >= 0) {
		scanel = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combobox_debug_scanel));
		basedir = malloc (1024);
		sprintf(basedir,"%siio:device%i", iio_dir, dev_num);

		if (scanel[strlen(scanel) - 1] != ' ')
			buf = gtk_entry_get_text (GTK_ENTRY(scanel_value));
		else {
			buf = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(scanel_options));
			scanel[strlen(scanel) - 1] = 0;
		}
		write_sysfs_string(scanel, basedir, buf);
		free(basedir);
	}

	scanel_read_clicked(btn, data);
}


static void debug_device_list_cb(GtkButton *btn, gpointer data)
{
	char buf[128];
	char *temp_path;
	char *current_device, *elements, *start, *end, *next, *avail;
	GtkListStore *store;
	int ret = 0;
	int i = 0, j;

	destroy_regmap_widgets();
	destroy_device_context();
	clean_gui_reg_info();

	current_device = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combobox_device_list));
	if (g_strcmp0("None\0", current_device)) {
		ret = set_debugfs_paths(current_device);
		if (!ret) {
			/* Check if device has a corresponding xml file */
			find_device_xml_file(xmls_folder_path, current_device, buf);

			/* Attempt to associate AXI Core ADC xml or AXI Core DAC xml to the device */
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(axicore_regmap), false);
			if (!strcmp(buf, "")) {
				if (is_input_device(current_device)) {
					sprintf(buf, "adi_regmap_adc.xml");
					gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(axicore_regmap), true);
				} else if (is_output_device(current_device)) {
					sprintf(buf, "adi_regmap_dac.xml");
					gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(axicore_regmap), true);
				}
			}

			if (!strcmp(buf, "")) {
				gtk_widget_show(frm_regmaptype);
				gtk_widget_show(btn_read_reg);
				gtk_widget_hide(warning_label);
				gtk_widget_set_sensitive(spin_btn_reg_value, true);
				gtk_widget_set_sensitive(label_reg_hex_value,true);
			} else {
				temp_path = malloc(strlen(xmls_folder_path) + strlen(buf) + 2);
				if (!temp_path) {
					printf("Failed to allocate memory with malloc\n");
					return;
				}
				sprintf(temp_path, "%s/%s", xmls_folder_path, buf);
				xml_doc = open_xml_file(temp_path, &root);
				if (xml_doc) {
					xml_file_opened = 1;
					create_device_context();
					g_signal_emit_by_name(spin_btn_reg_addr, "value-changed");
				} else {
					printf("Cannot load the file %s for the %s device\n",
						temp_path, current_device);
				}
				free(temp_path);
				gtk_widget_hide(frm_regmaptype);
				gtk_widget_show(btn_read_reg);
				gtk_widget_hide(warning_label);
				gtk_widget_set_sensitive(spin_btn_reg_value, true);
				gtk_widget_set_sensitive(label_reg_hex_value,true);
			}
		} else {
			int id = getuid();
			if (id != 0)
				gtk_widget_show(warning_label);
			gtk_widget_hide(frm_regmaptype);
			gtk_widget_hide(btn_read_reg);
			gtk_widget_set_sensitive(spin_btn_reg_value, false);
			gtk_widget_set_sensitive(label_reg_hex_value, false);
		}
		find_scan_elements(current_device, &elements, ACCESS_NORM);
		if (!strcmp(&elements[0], ""))
			return;
		scan_elements_sort(&elements);
		scan_elements_insert(&elements, AVAILABLE_TOKEN, NULL);
		gtk_widget_show(scanel_read);
		while(isspace(elements[strlen(elements) - 1]))
			elements[strlen(elements) - 1] = 0;
		current_elements = start = elements;
		if (debug_scanel_hid)
			g_signal_handler_disconnect(G_OBJECT(combobox_debug_scanel),debug_scanel_hid);
		store = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(combobox_debug_scanel)));
		gtk_list_store_clear (store);
		j = 0;
		while (start[0] != 0) {
			end = strchr(start, ' ');
			if (!end)
				end = start + strlen(start);
			if (j) {
				start = end + 1;
				j = 0;
				continue;
			}
			avail = strstr(end + 1, AVAILABLE_TOKEN);
			next = strchr(end + 1, ' ' );
			if (!next)
				next = end + 1 + strlen(end + 1);
			if(avail && avail <= next) {
				sprintf(buf, "%.*s ", (int)(end - start), start);
				j = 1;
			} else {
				sprintf(buf, "%.*s", (int)(end - start), start);
				j = 0;
			}
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combobox_debug_scanel),
				(const gchar *)buf);
			start = end + 1;
			if (!strcmp(buf, "name"))
				gtk_combo_box_set_active(GTK_COMBO_BOX(combobox_debug_scanel), i);
				gtk_entry_set_text(GTK_ENTRY(scanel_value), current_device);
			i++;
		}
		debug_scanel_hid = g_signal_connect(G_OBJECT(combobox_debug_scanel),
			 "changed",G_CALLBACK(scanel_read_clicked), NULL);
		gtk_widget_show(scanel_value);
		gtk_widget_hide(scanel_options);
	} else {
		gtk_widget_hide(frm_regmaptype);
		gtk_widget_hide(btn_read_reg);
		gtk_widget_hide(btn_write_reg);
		gtk_widget_hide(scanel_read);
		gtk_widget_hide(scanel_write);
		gtk_widget_hide(warning_label);
	}
}

static void reg_read_clicked(GtkButton *button, gpointer user_data)
{
	int i;
	unsigned address;
	char buf[16];

	address = (unsigned)gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin_btn_reg_addr));

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(axicore_regmap))) {
		address |= 0x80000000;
	}
	i = read_reg(address);
	if (i >= 0) {
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_btn_reg_value), i);
		if (i == 0)
			g_signal_emit_by_name(spin_btn_reg_value, "value-changed");
		snprintf(buf, sizeof(buf), "0x%04X", i);
		gtk_label_set_text(GTK_LABEL(label_reg_hex_value), buf);
		if (xml_file_opened)
			reveal_reg_map();
	} else {
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_btn_reg_value), 0);
		snprintf(buf, sizeof(buf), "<error>");
		gtk_label_set_text(GTK_LABEL(label_reg_hex_value), buf);
	}
	gtk_widget_show(btn_write_reg);
}

static void reg_write_clicked(GtkButton *button, gpointer user_data)
{
	write_reg((unsigned)gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin_btn_reg_addr)),
			(unsigned)gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin_btn_reg_value)));
}

static void reg_address_value_changed_cb(GtkSpinButton *spinbutton,
					gpointer user_data)
{
	static short block_signal = 0;
	static short prev_addr = -1;
	static short crt_pos = -1;
	int reg_addr;
	int spin_step;
	char buf[10];

	if (!xml_file_opened) {
		reg_addr = gtk_spin_button_get_value(spinbutton);
		snprintf(buf, sizeof(buf), "0x%04X", reg_addr);
		gtk_label_set_text(GTK_LABEL(label_reg_hex_addr), buf);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_btn_reg_value), (gdouble)0);
		gtk_label_set_text(GTK_LABEL(label_reg_hex_value), "<unknown>");
		gtk_widget_hide(btn_write_reg);
		goto reg_autoread;
	}
	gtk_widget_set_sensitive(spin_btn_reg_addr, FALSE);
	if (!block_signal) {
		gtk_widget_hide(btn_write_reg);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_btn_reg_value),
			(gdouble)0);
		gtk_label_set_text(GTK_LABEL(label_reg_hex_value), "<unknown>");
		reg_addr = (int)gtk_spin_button_get_value(spinbutton);
		spin_step = reg_addr - prev_addr;
		/* When user changes address from "up" and "down" arrows (keyboard or mouse),
		   overwrite the spin button with the address of next/previous register */
		if (spin_step == 1){
			if (crt_pos < (reg_list_size - 1))
				crt_pos++;
			goto spin_overwrite;
		} else if (spin_step == -1) {
			if (crt_pos > 0)
				crt_pos--;
			else crt_pos = 0;
			goto spin_overwrite;
		} else {
			goto updt_addr;
		}
	} else {
		block_signal = 0;
		goto restore_widget;
	}

spin_overwrite:
	if (reg_addr_list[crt_pos] != reg_addr)
		block_signal = 1; /* Block next signal emitted by gtk_spin_button_set_value */
	gtk_spin_button_set_value(spinbutton, (gdouble)reg_addr_list[crt_pos]);
	reg_addr = (int)gtk_spin_button_get_value(spinbutton);
	prev_addr = reg_addr;
updt_addr:
	crt_pos = get_reg_pos(reg_addr_list, reg_addr);
	display_reg_info(crt_pos);
	hide_reg_map();
	reg_addr = (int)gtk_spin_button_get_value(spinbutton);
	prev_addr = reg_addr;
	snprintf(buf, sizeof(buf), "0x%04x", reg_addr);
	gtk_label_set_text((GtkLabel *)label_reg_hex_addr, buf);
restore_widget:
	gtk_widget_set_sensitive(spin_btn_reg_addr, TRUE);
	gtk_widget_grab_focus(spin_btn_reg_addr);
	gtk_editable_select_region(GTK_EDITABLE(spinbutton), 0, 0);
reg_autoread:
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(reg_autoread)))
		g_signal_emit_by_name(btn_read_reg, "clicked", NULL);
}

static void reg_value_change_value_cb(GtkSpinButton *btn, gpointer user_data)
{
	unsigned value;
	unsigned new_value;
	char buf[16];

	if (!xml_file_opened) {
		value = gtk_spin_button_get_value(btn);
		snprintf(buf, sizeof(buf), "0x%04X", value);
		gtk_label_set_text(GTK_LABEL(label_reg_hex_value), buf);
		return;
	}
	value = gtk_spin_button_get_value(btn);
	block_bit_option_signals();
	new_value = update_regmap(value);
	unblock_bit_option_signals();
	g_signal_handler_block(btn, reg_val_hid);
	gtk_spin_button_set_value(btn, new_value);
	g_signal_handler_unblock(btn, reg_val_hid);
	snprintf(buf, sizeof(buf), "0x%04X", new_value);
	gtk_label_set_text(GTK_LABEL(label_reg_hex_value), buf);
}

static void spin_or_combo_changed_cb(GtkSpinButton *spinbutton,
					gpointer user_data)
{
	int i;
	int k;
	int opt_val = 0;
	int spin_val = 0;
	int offs = 0;
	int width = 0;
	gchar buf[10];

	for (i = 0; i < soft_reg.bgroup_cnt; i++){
		offs = soft_reg.bgroup_list[i].offset;
		width = soft_reg.bgroup_list[i].width;
		if (GTK_IS_SPIN_BUTTON(spinbutton)){
				if (spinbutton == (GtkSpinButton *)bit_spinbuttons[offs]){
					opt_val = (int)gtk_spin_button_get_value(spinbutton);
					opt_val = opt_val << offs;
					break;
				}
		} else {
			if ((GtkComboBoxText *)spinbutton == (GtkComboBoxText *)bit_comboboxes[offs]){
				k = gtk_combo_box_get_active((GtkComboBox *)spinbutton);
				opt_val = soft_reg.bgroup_list[i].option_list[k].value;
				opt_val = opt_val << offs;
				break;
			}
		}
	}
	spin_val = gtk_spin_button_get_value((GtkSpinButton *)spin_btn_reg_value);
	spin_val = spin_val & ~(((1ul << width) - 1) << offs);
	spin_val = spin_val | opt_val;
	g_signal_handler_block(spin_btn_reg_value, reg_val_hid);
	gtk_spin_button_set_value((GtkSpinButton *)spin_btn_reg_value, spin_val);
	g_signal_handler_unblock(spin_btn_reg_value, reg_val_hid);
	snprintf(buf, sizeof(buf), "0x%04X", spin_val);
	gtk_label_set_text((GtkLabel *)label_reg_hex_value, buf);
}


void debug_panel_destroy_cb(GObject *object, gpointer user_data)
{
	destroy_device_context();
}

/******************************************************************************/
/*************************** Functions definitions ****************************/
/******************************************************************************/

/*
 * Allocate memory for widgets that draw register map.
 */
static int alloc_widget_arrays(int reg_length)
{
	lbl_bits = (GtkWidget **)malloc(sizeof(GtkWidget *) * reg_length);
	hboxes = (GtkWidget **)malloc(sizeof(GtkWidget *) * reg_length);
	vboxes = (GtkWidget **)malloc(sizeof(GtkWidget *) * reg_length);
	bit_descrip_list = (GtkWidget **)malloc(sizeof(GtkWidget *) * reg_length);
	elem_frames = (GtkWidget **)malloc(sizeof(GtkWidget *) * reg_length);
	bit_comboboxes = (GtkWidget **)malloc(sizeof(GtkWidget *) * reg_length);
	bit_no_read_lbl = (GtkWidget **)malloc(sizeof(GtkWidget *) * reg_length);
	bit_spinbuttons = (GtkWidget **)malloc(sizeof(GtkWidget *) * reg_length);
	bit_spin_adjustments = (GtkWidget **)malloc(sizeof(GtkWidget *) * reg_length);
	combo_hid_list = (gulong*)malloc(sizeof(gulong) * reg_length);
	spin_hid_list = (gulong*)malloc(sizeof(gulong) * reg_length);

	return 0;
}

/*
 * Free memory allocated by alloc_widget_arrays().
 */
static void free_widget_arrays(void)
{
	free(lbl_bits);
	free(hboxes);
	free(vboxes);
	free(bit_descrip_list);
	free(elem_frames);
	free(bit_comboboxes);
	free(bit_no_read_lbl);
	free(bit_spinbuttons);
	free(bit_spin_adjustments);
	free(combo_hid_list);
	free(spin_hid_list);
}

/*
 * Allocate memory for a reg structure and fill it with data read from the xml
 * file.
 */
static void fill_soft_register(reg *p_reg, int reg_index)
{
	xmlNodePtr crtnode;
	xmlNodeSetPtr nodeset;
	xmlNodePtr *bit_list;
	xmlNodePtr *opt_list;
	bgroup *p_bit;
	option *p_option;
	int i, j;

	/* Increment this flag every function call */
	soft_reg_exists++;

	/* Get the root that points to the list of the registers from the xml file. */
	nodeset = register_list->nodesetval;

	/* Get the name, notes and width of the register. */
	p_reg->name = read_string_element(xml_doc, nodeset->nodeTab[reg_index], "Description");
	p_reg->notes = read_string_element(xml_doc, nodeset->nodeTab[reg_index], "Notes");
	p_reg->width = read_string_element(xml_doc, nodeset->nodeTab[reg_index], "Width");

	/* Get the "BitFields" reference */
	crtnode = get_child_by_name(nodeset->nodeTab[reg_index], "BitFields");

	/* Get the reference list of the bit groups */
	bit_list = get_children_by_name(crtnode, "BitField", &p_reg->bgroup_cnt);

	/* Allocate space for the found bit fields(bit groups) */
	p_reg->bgroup_list = (bgroup *)malloc(sizeof(bgroup) * p_reg->bgroup_cnt);

	/* Go through all bit groups and get information: description, width, offset, options, etc. */
	for (i = 0; i < p_reg->bgroup_cnt; i++){
		p_bit = &p_reg->bgroup_list[i];
		p_bit->name = read_string_element(xml_doc, bit_list[i], "Description");
		p_bit->width = read_integer_element(xml_doc, bit_list[i], "Width");
		p_bit->offset = read_integer_element(xml_doc, bit_list[i], "RegOffset");
		p_bit->def_val = read_integer_element(xml_doc, bit_list[i], "DefaultValue");
		p_bit->access = read_string_element(xml_doc, bit_list[i], "Access");
		p_bit->description = read_string_element(xml_doc, bit_list[i], "Notes");

		/* Get the "Options" reference if exist. Else go to the next bit group. */
		crtnode = get_child_by_name(bit_list[i], "Options");
		if (crtnode == NULL){
			p_bit->options_cnt = 0;
			p_bit->option_list = NULL;
			continue;
		}
		/* Get the reference list of all options */
		opt_list = get_children_by_name(crtnode, "Option", &p_bit->options_cnt);

		/* Allocate space for the found options */
		p_bit->option_list = (option *)malloc(sizeof(option) * p_bit->options_cnt);
		for (j = 0; j < p_bit->options_cnt; j++) {
			p_option = &p_bit->option_list[j];
			p_option->text = read_string_element(xml_doc, opt_list[j], "Description");
			p_option->value = read_integer_element(xml_doc, opt_list[j], "Value");
		}
		free(opt_list); /* This list options is no longer required */
	}
	free(bit_list); /* List of bit groups is no longer required */

	/* Calculate the register default value using the bits default values */
	p_reg->def_val = get_reg_default_val(p_reg);
}

/*
 * Free the memory allocated by fill_soft_register.
 */
static void free_soft_register(reg *p_reg)
{
	bgroup* p_bit;
	int i, j;

	/* Free memory only when a call of fill_soft_register() already occurred */
	if (soft_reg_exists == 0)
		return;

	soft_reg_exists--;

	/* xmlFree() is used to free all strings allocated by read_string_element() function */
	for (i = 0; i < p_reg->bgroup_cnt; i++){
		p_bit = &p_reg->bgroup_list[i];
		for (j = 0; j < p_bit->options_cnt; j++){
			xmlFree((xmlChar *)p_bit->option_list[j].text);
		}
		free(p_bit->option_list);
		p_bit->def_val = 0;
		p_bit->options_cnt = 0;
		p_bit->width = 0;
		p_bit->offset = 0;
		xmlFree((xmlChar *)p_bit->description);
		xmlFree((xmlChar *)p_bit->access);
		xmlFree((xmlChar *)p_bit->name);
	}
	free(p_reg->bgroup_list);
	p_reg->def_val = 0;
	p_reg->bgroup_cnt = 0;
	xmlFree(p_reg->width);
	xmlFree(p_reg->notes);
	xmlFree(p_reg->name);
}

/*
 * Display all register related information.
 */
static int display_reg_info(int pos_reg_addr)
{
	/* Free the previously used register */
	free_soft_register(&soft_reg);

	/* Display no register information when address is not valid */
	if (pos_reg_addr < 0){
		draw_reg_map(0);
		return 0;
	}
	/* Extract data from xml file and fill a "reg" structure with it */
	fill_soft_register(&soft_reg, pos_reg_addr);

	/* Display the register map using data from the "reg" structure */
	draw_reg_map(1);

	return 0;
}

/*
 * Block comboboxes and spinbuttons handlers.
 */
static void block_bit_option_signals(void)
{
	int i = 0;

	for (; i < reg_bit_width; i++) {
		g_signal_handler_block(bit_comboboxes[i], combo_hid_list[i]);
		g_signal_handler_block(bit_spinbuttons[i], spin_hid_list[i]);
	}
}

/*
 * Unblock comboboxes and spinbuttons handlers.
 */
static void unblock_bit_option_signals(void)
{
	int i = 0;

	for (; i < reg_bit_width; i++) {
		g_signal_handler_unblock(bit_comboboxes[i], combo_hid_list[i]);
		g_signal_handler_unblock(bit_spinbuttons[i], spin_hid_list[i]);
	}
}

/*
 * Read the default values of the bits within a register and calculate the
 * default value of the register.
 */
 static int get_reg_default_val(reg *p_reg)
 {
	 int i = 0;
	 int def_val = 0;
	 bgroup *p_bit;

	 for (; i < p_reg->bgroup_cnt; i++) {
		 p_bit = &p_reg->bgroup_list[i];
		 def_val |= p_bit->def_val << p_bit->offset;
	 }

	 return def_val;
 }

/*
 * Search for a value from the option list and return the position inside the
 * list.
 */
static int get_option_index(int option_value, bgroup *bit)
{
	int i;

	for (i = 0; i < bit->options_cnt; i++)
		if (bit->option_list[i].value == option_value)
			return i;

	return -1;
}

/*
 * Helper function. Remove all elements of a combobox.
 */
static void gtk_combo_box_text_remove_all (GtkWidget *combo_box)
{
	GtkListStore *store;

	store = GTK_LIST_STORE (gtk_combo_box_get_model (GTK_COMBO_BOX (combo_box)));
	gtk_list_store_clear (store);
}

/*
 * Given the number of bit groups, their widths and offsets, the function
 * rearranges the boxes and labels so that the register map will look the same
 * with the one from the datasheet.
 */
static void draw_reg_map(int valid_register)
{
	int i, j;
	reg    *p_reg;
	bgroup *p_bit;
	option *p_option;
	const gchar *label_str;
	char buf[12];
	GtkRequisition r;

	block_bit_option_signals();
	/* Reset all bits to the "reseverd" status and clear all options */
	for (i = (reg_bit_width - 1); i >= 0; i--){
		gtk_widget_reparent(lbl_bits[i], hboxes[i]);
		gtk_label_set_text((GtkLabel *)bit_descrip_list[i], "Reserved");
		gtk_label_set_width_chars((GtkLabel *)bit_descrip_list[i], 13);
		gtk_combo_box_text_remove_all(bit_comboboxes[i]);
		g_object_set(bit_comboboxes[i], "sensitive", TRUE, NULL);
		gtk_widget_hide(bit_comboboxes[i]);
		gtk_widget_hide(bit_spinbuttons[i]);
		gtk_widget_set_tooltip_text(vboxes[i], "");
	}

	/* Dispaly register information */
	if (valid_register){
		snprintf(buf, sizeof(buf), "0x%X", soft_reg.def_val);
		gtk_label_set_text((GtkLabel *)label_reg_descrip, (gchar *)soft_reg.name);
		gtk_label_set_text((GtkLabel *)label_reg_def_val, (gchar *)buf);
		gtk_label_set_text((GtkLabel *)label_reg_notes, (gchar *)soft_reg.notes);
		gtk_widget_show(label_reg_notes);
		gtk_widget_show(label_reg_descrip);
		gtk_widget_show(label_reg_def_val);
		if (strlen(soft_reg.notes) > 0)
			gtk_widget_show(label_notes_tag);
		else
			gtk_widget_hide(label_notes_tag);
	} else {
		gtk_label_set_text((GtkLabel *)label_reg_descrip, (gchar *)"");
		gtk_label_set_text((GtkLabel *)label_reg_def_val, (gchar *)"");
		gtk_label_set_text((GtkLabel *)label_reg_notes, (gchar *)"");
		gtk_widget_show_all(reg_map_container);
		gtk_widget_hide(label_reg_notes);
		gtk_widget_hide(label_notes_tag);
		gtk_widget_hide(label_reg_descrip);
		gtk_widget_hide(label_reg_def_val);
		unblock_bit_option_signals();
		return;
	}
	gtk_widget_show_all(reg_map_container);

	/* Start rearranging boxes and labels to form bit groups */
	p_reg = &soft_reg;
	for (i = 0; i < p_reg->bgroup_cnt; i++){
		p_bit = &p_reg->bgroup_list[i];
		for (j = (p_bit->width - 1); j >= 0 ; j--){
			gtk_widget_reparent(lbl_bits[p_bit->offset + j], hboxes[p_bit->offset]);
			gtk_box_reorder_child((GtkBox *)hboxes[p_bit->offset], lbl_bits[p_bit->offset + j], -1);
			if (j > 0)
				gtk_widget_hide(elem_frames[p_bit->offset + j]);
		}
		/* Set description labels properties */
		gtk_label_set_justify((GtkLabel *)bit_descrip_list[p_bit->offset], GTK_JUSTIFY_CENTER);
		gtk_label_set_text((GtkLabel *)bit_descrip_list[p_bit->offset], p_bit->name);
		gtk_label_set_width_chars((GtkLabel *)bit_descrip_list[p_bit->offset], 13 * p_bit->width);

		/* Add the bit group options */
		for (j = 0; j < p_bit->options_cnt; j++){
			p_option = &p_bit->option_list[j];
			gtk_combo_box_text_append_text((GtkComboBoxText *)(bit_comboboxes[p_bit->offset]), p_option->text);
		}

		/* Bit groups without option get a spin button(except Reserved bits) and bit groups with options get a combo box*/
		if (!xmlStrcmp((xmlChar *)p_bit->name, (xmlChar *)"Reserved to 1")){
			gtk_combo_box_text_append_text((GtkComboBoxText *)(bit_comboboxes[p_bit->offset]), (gchar *)"1");
			gtk_combo_box_set_active((GtkComboBox *)(bit_comboboxes[p_bit->offset]), 0);
			g_object_set(bit_comboboxes[p_bit->offset], "sensitive", FALSE, NULL);
			gtk_widget_show(bit_comboboxes[p_bit->offset]);

		} else if (p_bit->options_cnt > 0){
			/* Set the combo box active value to the default value of the bit group */
			gtk_combo_box_set_active((GtkComboBox *)(bit_comboboxes[p_bit->offset]), get_option_index(p_bit->def_val, p_bit));
			gtk_widget_show(bit_comboboxes[p_bit->offset]);
		} else {
			/* Configure the adjustment according with the bit group size */
			gtk_adjustment_set_upper((GtkAdjustment *)bit_spin_adjustments[p_bit->offset], (1ul << p_bit->width) - 1);
			gtk_adjustment_set_value((GtkAdjustment *)bit_spin_adjustments[p_bit->offset], 0);
			/* Set the spin button value to the default value of the bit group */
			gtk_spin_button_set_value((GtkSpinButton *)bit_spinbuttons[p_bit->offset], p_bit->def_val);

			/* Replace combo box with spin button */
			gtk_widget_hide(bit_comboboxes[p_bit->offset]);
			gtk_widget_show(bit_spinbuttons[p_bit->offset]);
		}

		/* Add tooltips with bit descriptions */
		gtk_widget_set_tooltip_text(vboxes[p_bit->offset], p_bit->description);
	}

	/* Set default value of the combobox to 0 for all bits with "Reserved" tag. Also inactivate the combobox */
	for (i = 0; i < reg_bit_width; i++){
		label_str = gtk_label_get_text((GtkLabel *)bit_descrip_list[i]);
		if (!xmlStrcmp((xmlChar *)label_str, (xmlChar *)"Reserved")){
			gtk_combo_box_text_append_text((GtkComboBoxText *)(bit_comboboxes[i]), (gchar *)"0");
			gtk_combo_box_set_active((GtkComboBox *)(bit_comboboxes[i]), 0);
			g_object_set(bit_comboboxes[i], "sensitive", FALSE, NULL);
			gtk_widget_show(bit_comboboxes[i]);
		}
	}

	/* Set the height of the regmap scrolled window. */
	gtk_widget_size_request(reg_map_container, &r);
	gtk_widget_set_size_request(scrollwin_regmap, -1, r.height);

	unblock_bit_option_signals();
}

/*
 * Show bit options. Hide "Not read" labels.
 */
static void reveal_reg_map(void)
{
	int i = 0;
	reg    *p_reg;
	bgroup *p_bit;
	const gchar *label_str;

	p_reg = &soft_reg;
	for (; i < p_reg->bgroup_cnt; i++){
		p_bit = &p_reg->bgroup_list[i];
		if (!xmlStrcmp((xmlChar *)p_bit->name, (xmlChar *)"Reserved to 1"))
			gtk_widget_show(bit_comboboxes[p_bit->offset]);
		else if (p_bit->options_cnt > 0)
			gtk_widget_show(bit_comboboxes[p_bit->offset]);
		else
			gtk_widget_show(bit_spinbuttons[p_bit->offset]);
		gtk_widget_hide(bit_no_read_lbl[p_bit->offset]);
	}
	for (i = 0; i < reg_bit_width; i++){
		label_str = gtk_label_get_text((GtkLabel *)bit_descrip_list[i]);
		if (!xmlStrcmp((xmlChar *)label_str, (xmlChar *)"Reserved"))
			gtk_widget_show(bit_comboboxes[i]);
			gtk_widget_hide(bit_no_read_lbl[i]);
	}
}

/*
 * Hide bit options. Show "Not read" labels.
 */
static void hide_reg_map(void)
{
	int i = 0;
	reg    *p_reg;
	bgroup *p_bit;
	const gchar *label_str;

	p_reg = &soft_reg;
	for (; i < p_reg->bgroup_cnt; i++){
		p_bit = &p_reg->bgroup_list[i];
		if (!xmlStrcmp((xmlChar *)p_bit->name, (xmlChar *)"Reserved to 1"))
			gtk_widget_hide(bit_comboboxes[p_bit->offset]);
		else if (p_bit->options_cnt > 0)
			gtk_widget_hide(bit_comboboxes[p_bit->offset]);
		else
			gtk_widget_hide(bit_spinbuttons[p_bit->offset]);
		gtk_widget_show(bit_no_read_lbl[p_bit->offset]);
	}
	for (i = 0; i < reg_bit_width; i++){
		label_str = gtk_label_get_text((GtkLabel *)bit_descrip_list[i]);
		if (!xmlStrcmp((xmlChar *)label_str, (xmlChar *)"Reserved"))
			gtk_widget_hide(bit_comboboxes[i]);
	}
}

/*
 * Create all boxes and labels required for displaying the register bit map.
 */
static void create_reg_map(void)
{
	gchar str_bit_field[7];
	short i;

	gtk_label_set_line_wrap((GtkLabel *)label_reg_descrip, TRUE);
	gtk_label_set_line_wrap((GtkLabel *)label_reg_notes, TRUE);
	gtk_label_set_width_chars((GtkLabel *)label_reg_descrip, -1);
	gtk_label_set_width_chars((GtkLabel *)label_reg_notes, 110);

	hbox_bits_container = gtk_hbox_new(FALSE, 0);
	gtk_container_add((GtkContainer *)reg_map_container,
							hbox_bits_container);

	for (i = (reg_bit_width - 1); i >= 0; i--){
		elem_frames[i] = gtk_frame_new("");
		gtk_container_add((GtkContainer *)hbox_bits_container,
								elem_frames[i]);
		vboxes[i] = gtk_vbox_new(FALSE, 0);
		gtk_container_add((GtkContainer *)elem_frames[i], vboxes[i]);

		hboxes[i] = gtk_hbox_new(FALSE, 0);
		gtk_box_pack_start((GtkBox *)vboxes[i], hboxes[i], FALSE,
								FALSE, 0);
		snprintf(str_bit_field, 7, "Bit %d", i);
		lbl_bits[i] = gtk_label_new((gchar *)str_bit_field);
		gtk_widget_set_size_request(lbl_bits[i], 50, -1);
		gtk_box_pack_start((GtkBox *)hboxes[i], lbl_bits[i], TRUE, FALSE,
									0);
		gtk_misc_set_alignment((GtkMisc *)lbl_bits[i], 0.5, 0.0);
		bit_descrip_list[i] = gtk_label_new("Reserved");
		gtk_box_pack_start((GtkBox *)vboxes[i], bit_descrip_list[i], TRUE, TRUE,
									0);
		bit_comboboxes[i] = gtk_combo_box_text_new();
		gtk_widget_set_size_request(bit_comboboxes[i], 50, -1);
		gtk_box_pack_start((GtkBox *)vboxes[i], bit_comboboxes[i],
							FALSE, FALSE, 0);
		gtk_widget_set_no_show_all(bit_comboboxes[i], TRUE);
		bit_spin_adjustments[i] = (GtkWidget *)gtk_adjustment_new(0, 0, 0, 1, 0, 0);

		bit_spinbuttons[i] = gtk_spin_button_new((GtkAdjustment *)bit_spin_adjustments[i], 1, 0);
		gtk_entry_set_alignment((GtkEntry *)bit_spinbuttons[i], 1);
		gtk_box_pack_start((GtkBox *)vboxes[i], bit_spinbuttons[i],
							FALSE, FALSE, 0);
		gtk_widget_set_no_show_all(bit_spinbuttons[i], TRUE);

		bit_no_read_lbl[i] = gtk_label_new("Not Read");
		gtk_widget_set_size_request(bit_no_read_lbl[i], 50, -1);
		gtk_box_pack_start((GtkBox *)vboxes[i], bit_no_read_lbl[i],
							FALSE, FALSE, 0);
		gtk_misc_set_padding((GtkMisc *)bit_no_read_lbl[i], 0, 10);

		gtk_label_set_line_wrap((GtkLabel *)bit_descrip_list[i], TRUE);
		gtk_misc_set_alignment((GtkMisc *)bit_descrip_list[i], 0.5, 0.5);
		gtk_misc_set_padding((GtkMisc *)bit_descrip_list[i], 0, 10);

		combo_hid_list[i] = g_signal_connect(G_OBJECT(bit_comboboxes[i]),
			"changed", G_CALLBACK(spin_or_combo_changed_cb), NULL);
		spin_hid_list[i] = g_signal_connect(G_OBJECT(bit_spinbuttons[i]),
			"changed", G_CALLBACK(spin_or_combo_changed_cb), NULL);
	}
}

static void clean_gui_reg_info(void)
{
	gtk_label_set_text(GTK_LABEL(label_reg_descrip), "");
	gtk_label_set_text(GTK_LABEL(label_reg_def_val), "");
	gtk_label_set_text(GTK_LABEL(label_reg_notes), "");
	gtk_widget_hide(label_notes_tag);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_btn_reg_addr), (gdouble)0);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_btn_reg_value), (gdouble)0);
	gtk_widget_hide(btn_write_reg);
	gtk_label_set_text(GTK_LABEL(label_reg_hex_addr), "0x000");
	gtk_label_set_text(GTK_LABEL(label_reg_hex_value), "<unknown>");
	gtk_widget_set_size_request(scrollwin_regmap, -1, -1);
}

/*
 * Find the bit width of the registers.
 */
static int get_default_reg_width(void)
{

	xmlNodePtr node;
	int default_width;

	node = root->xmlChildrenNode;
	while (node != NULL){
		if (!xmlStrcmp(node->name, (xmlChar *)"Register"))
			break;
	node = node->next;
	}
	if (node == NULL)
		return 0;
	/* Now node is pointing to the first <Register> that holds the <Width> element. */
	default_width = read_integer_element(xml_doc, node, "Width");

	return default_width;
}

/*
 * Get the list of the public registers of a ADI part.
 */
static int * get_reg_addr_list(void)
{
	xmlNodeSetPtr nodeset;
	char *reg_addr;
	int *list;
	int i = 0;

	nodeset = register_list->nodesetval;
	reg_list_size = nodeset->nodeNr;
	list = malloc(sizeof(*list) * reg_list_size);
	if (list == NULL){
		printf("Memory allocation failed\n");
		return NULL;
	}

	for (; i < nodeset->nodeNr; i++){
		reg_addr = read_string_element(xml_doc, nodeset->nodeTab[i], "Address");
		sscanf(reg_addr, "%x", (int *)(list + i));
		xmlFree((xmlChar *)reg_addr);
	}

	return list;
}

/*
 * Find the register position in the given list.
 */
static int get_reg_pos(int *regList, int reg_addr)
{
	int i = 0;

	for (; i < reg_list_size; i++){
		if (regList[i] == reg_addr)
			return i;
	}

	return -1;
}

/*
 * Update bit options using raw data read from the device or provided by user.
 * Check if options exists for the provided data and reconstruct data from only
 * the options that exist.
 * Retrurn - the actual data that can be set.
 */
static int update_regmap(int data)
{
    int i;
    int bg_value; /* bit group value */
    int mask;
    int opt_pos;
    int new_data = 0;
    reg *p_reg;
    bgroup *p_bit;

    p_reg = &soft_reg;
    for (i = 0; i < p_reg->bgroup_cnt; i++){
        p_bit = &p_reg->bgroup_list[i];
        mask = (1ul << p_bit->width) - 1;
        mask = mask << p_bit->offset;
        bg_value = (data & mask) >> p_bit->offset;
        if (p_bit->options_cnt > 0){
		opt_pos = get_option_index(bg_value, p_bit);
		if (opt_pos == -1)
			opt_pos = 0;
		gtk_combo_box_set_active((GtkComboBox *)(bit_comboboxes[p_bit->offset]), opt_pos);
		new_data |= p_bit->option_list[opt_pos].value << p_bit->offset;
        } else {
		if (xmlStrcmp((xmlChar *)p_bit->name, (xmlChar *)"Reserved to 1")) {
			gtk_spin_button_set_value((GtkSpinButton *) bit_spinbuttons[p_bit->offset], bg_value);
			new_data |= bg_value << p_bit->offset;
		} else {
			new_data |= 1ul << p_bit->offset;
		}
        }
    }

    return new_data;
}

/*
 * Create a context for the selected ADI device. Retrieve a list of all
 * registers and their addresses. Find the width that applies to all registers.
 * Allocate memory for all lists of widgets. Create new widgets and draw a
 * register map.
 */
static void create_device_context(void)
{
	if (!context_created) {
		/* Search for all elements called "Register" */
		register_list = retrieve_all_elements(xml_doc, "//Register");
		reg_addr_list = get_reg_addr_list();
		/* Init data */
		reg_bit_width = get_default_reg_width();
		alloc_widget_arrays(reg_bit_width);
		create_reg_map();
		context_created = 1;
	}
}

/*
 * Destroy a context for the selected ADI device. Free memory allocated for
 * all widget lists. Close xml file of the ADI device.
 */
static void destroy_device_context(void)
{
/* Free resources */
	if (context_created == 1) {
		free(reg_addr_list);
		free_widget_arrays();
		free_soft_register(&soft_reg);
		xmlXPathFreeObject(register_list);
		close_xml_file(xml_doc);
		xml_file_opened = 0;
		reg_bit_width = 0;
	}
	context_created = 0;
}

/*
 * Destroy all widgets within the register map.
 */
static void destroy_regmap_widgets(void)
{
	/* Remove all widgets (and their children) that create regmap */
	if (context_created == 1)
		gtk_widget_destroy(hbox_bits_container);
}

/*
 *  Main function
 */
static int debug_init(GtkWidget *notebook)
{
	GtkBuilder *builder;
	GtkWidget *debug_panel;
	GtkWidget *vbox_device_list;
	GtkWidget *vbox_scanel;
	char *devices = NULL, *device;
	int ret;
	DIR *d;

	/* Check the local xmls folder first */
	d = opendir("./xmls");
	if (!d) {
		//set_xml_folder_path(OSC_XML_PATH);
		snprintf(xmls_folder_path, sizeof(xmls_folder_path), "%s", OSC_XML_PATH);
	} else {
		closedir(d);
		//set_xml_folder_path("./xmls");
		snprintf(xmls_folder_path, sizeof(xmls_folder_path), "%s", "./xmls");
	}

	builder = gtk_builder_new();
	if (!gtk_builder_add_from_file(builder, "debug.glade", NULL))
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "debug.glade", NULL);

	debug_panel = GTK_WIDGET(gtk_builder_get_object(builder, "reg_debug_panel"));
	vbox_device_list = GTK_WIDGET(gtk_builder_get_object(builder, "device_list_container"));
	btn_read_reg = GTK_WIDGET(gtk_builder_get_object(builder, "debug_read_reg"));
	btn_write_reg = GTK_WIDGET(gtk_builder_get_object(builder, "debug_write_reg"));
	label_reg_hex_addr = GTK_WIDGET(gtk_builder_get_object(builder, "debug_reg_address_hex"));
	label_reg_hex_value = GTK_WIDGET(gtk_builder_get_object(builder, "debug_reg_value_hex"));
	label_reg_descrip = GTK_WIDGET(gtk_builder_get_object(builder, "labelRegDescripText"));
	label_reg_def_val = GTK_WIDGET(gtk_builder_get_object(builder, "label_reg_def_val_txt"));
	label_reg_notes = GTK_WIDGET(gtk_builder_get_object(builder, "labelRegNotesText"));
	label_notes_tag = GTK_WIDGET(gtk_builder_get_object(builder, "labelRegNotes"));
	spin_btn_reg_addr = GTK_WIDGET(gtk_builder_get_object(builder, "debug_reg_address"));
	spin_btn_reg_value = GTK_WIDGET(gtk_builder_get_object(builder, "debug_reg_value"));
	scrollwin_regmap = GTK_WIDGET(gtk_builder_get_object(builder, "scrolledwindow_regmap"));
	reg_map_container = GTK_WIDGET(gtk_builder_get_object(builder, "regmap_container"));
	warning_label = GTK_WIDGET(gtk_builder_get_object(builder, "label_warning"));
	reg_autoread = GTK_WIDGET(gtk_builder_get_object(builder, "register_autoread"));
	frm_regmaptype = GTK_WIDGET(gtk_builder_get_object(builder, "frame_regmap_type"));
	spi_regmap = GTK_WIDGET(gtk_builder_get_object(builder, "radiobtn_spi_map"));
	axicore_regmap = GTK_WIDGET(gtk_builder_get_object(builder, "radiobtn_axicore_map"));

	vbox_scanel =  GTK_WIDGET(gtk_builder_get_object(builder, "scanel_container"));
	scanel_read = GTK_WIDGET(gtk_builder_get_object(builder, "debug_read_scan"));
	scanel_write = GTK_WIDGET(gtk_builder_get_object(builder, "debug_write_scan"));
	scanel_value = GTK_WIDGET(gtk_builder_get_object(builder, "debug_scanel_value"));
	scanel_options = GTK_WIDGET(gtk_builder_get_object(builder, "debug_scanel_options"));

	/* Create comboboxes for the Device List and for the Scan Elements */
	combobox_device_list = gtk_combo_box_text_new();
	combobox_debug_scanel = gtk_combo_box_text_new();
	/* Put in the correct values */
	gtk_box_pack_start((GtkBox *)vbox_device_list, combobox_device_list,
				TRUE, TRUE, 0);
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combobox_device_list),
				(const gchar *)"None");
	gtk_combo_box_set_active(GTK_COMBO_BOX(combobox_device_list), 0);
	gtk_box_pack_start((GtkBox *)vbox_scanel, combobox_debug_scanel,
				TRUE, TRUE, 0);
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combobox_debug_scanel),
				(const gchar *)"None");
	gtk_combo_box_set_active(GTK_COMBO_BOX(combobox_debug_scanel), 0);

	/* Fill in device list */
	ret = find_iio_names(&devices, "iio:device");
	device=devices;
	for (; ret > 0; ret--) {
		/* Make sure we can access things */
		if (!set_debugfs_paths(devices) || find_scan_elements(devices, NULL, ACCESS_NORM) >= 0) {
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combobox_device_list),
							(const gchar *)devices);
		}
		devices += strlen(devices) + 1;
	}
	free(device);

	/* Connect signals */
	g_signal_connect(G_OBJECT(debug_panel), "destroy",
			G_CALLBACK(debug_panel_destroy_cb), NULL);
	g_signal_connect(G_OBJECT(btn_read_reg), "clicked",
			G_CALLBACK(reg_read_clicked), NULL);
	g_signal_connect(G_OBJECT(btn_write_reg), "clicked",
			G_CALLBACK(reg_write_clicked), NULL);
	reg_addr_hid = g_signal_connect(G_OBJECT(spin_btn_reg_addr),
		"value-changed", G_CALLBACK(reg_address_value_changed_cb), NULL);
	reg_val_hid = g_signal_connect(G_OBJECT(spin_btn_reg_value),
		"value-changed", G_CALLBACK(reg_value_change_value_cb), NULL);
	g_signal_connect(G_OBJECT(combobox_device_list), "changed",
			G_CALLBACK(debug_device_list_cb), NULL);
	g_signal_connect(G_OBJECT(scanel_read), "clicked",
			G_CALLBACK(scanel_read_clicked), NULL);
	g_signal_connect(G_OBJECT(scanel_write), "clicked",
			G_CALLBACK(scanel_write_clicked), NULL);

	/* Show window and hide(or set as inactive) some widgets. */
	gtk_widget_show_all(debug_panel);
	gtk_widget_hide(btn_read_reg);
	gtk_widget_hide(btn_write_reg);

	/* Show the panel */
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), debug_panel, NULL);
	gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(notebook), debug_panel, "Debug");

	return 0;
}

static bool debug_identify(void)
{
	int num, i = 0;
	char *devices=NULL, *device;
	char *elements;

	num = find_iio_names(&devices, "iio:device");
	device=devices;
	for (; num > 0; num--) {
		/* Make sure we can access things */
		if (!set_debugfs_paths(devices) ||
				find_scan_elements(devices, NULL, ACCESS_NORM) >= 0) {
			i++;
			break;
		}
		find_scan_elements(devices, &elements, ACCESS_NORM);
		devices += strlen(devices) + 1;
	}
	free(device);
	if (i)
		return true;

	return false;
}

struct osc_plugin plugin = {
	.name = "Debug",
	.identify = debug_identify,
	.init = debug_init,
};
