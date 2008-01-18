/*
 * Copyright © 2007 Gerd Kohlberger <lowfi@chello.at>
 *
 * This file is part of Mousetweaks.
 *
 * Mousetweaks is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Mousetweaks is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <gtk/gtk.h>
#include <glade/glade.h>

#include "mt-main.h"
#include "mt-dbus.h"
#include "mt-common.h"

enum {
    BUTTON_STYLE_TEXT = 0,
    BUTTON_STYLE_ICON,
    BUTTON_STYLE_BOTH
};

static GladeXML *xml = NULL;

void
mt_ctw_set_click_type (gint ct)
{
    GtkWidget *button;
    GSList *group;
    gpointer data;

    button = glade_xml_get_widget (xml, "single");
    group = gtk_radio_button_get_group (GTK_RADIO_BUTTON(button));
    data = g_slist_nth_data (group, ct);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(data), TRUE);
}

void
mt_ctw_update_visibility (MTClosure *mt)
{
    if (mt->dwell_enabled && mt->dwell_show_ctw)
	gtk_widget_show (glade_xml_get_widget (xml, "ctw"));
    else
	gtk_widget_hide (glade_xml_get_widget (xml, "ctw"));
}

void
mt_ctw_update_sensitivity (MTClosure *mt)
{
    gboolean sensitive;

    sensitive = mt->dwell_enabled && mt->dwell_mode == DWELL_MODE_CTW;
    gtk_widget_set_sensitive (glade_xml_get_widget (xml, "box"), sensitive);
}

void
mt_ctw_update_style (gint style)
{
    GtkWidget *icon, *label;
    const gchar *l[] = { "single_l", "double_l", "drag_l", "right_l" };
    const gchar *img[] = { "single_i", "double_i", "drag_i", "right_i" };
    gint i;

    for (i = 0; i < N_CLICK_TYPES; i++) {
	label = glade_xml_get_widget (xml, l[i]);
	icon = glade_xml_get_widget (xml, img[i]);

	switch (style) {
	case BUTTON_STYLE_BOTH:
	    g_object_set (icon, "yalign", 1.0, NULL);
	    gtk_widget_show (icon);
	    g_object_set (label, "yalign", 0.0, NULL);
	    gtk_widget_show (label);
	    break;
	case BUTTON_STYLE_TEXT:
	    gtk_widget_hide (icon);
	    g_object_set (icon, "yalign", 0.5, NULL);
	    gtk_widget_show (label);
	    g_object_set (label, "yalign", 0.5, NULL);
	    break;
	case BUTTON_STYLE_ICON:
	    gtk_widget_show (icon);
	    g_object_set (icon, "yalign", 0.5, NULL);
	    gtk_widget_hide (label);
	    g_object_set (label, "yalign", 0.5, NULL);
	default:
	    break;
	}
    }
}

static void
ctw_button_cb (GtkToggleButton *button, gpointer data)
{
    MTClosure *mt = (MTClosure *) data;

    if (gtk_toggle_button_get_active (button)) {
	GSList *group;

	group = gtk_radio_button_get_group (GTK_RADIO_BUTTON(button));
	mt->dwell_cct = g_slist_index (group, (gconstpointer) button);

	mt_dbus_send_signal (mt->conn, CLICK_TYPE_SIGNAL, mt->dwell_cct);
    }
}

static gboolean
ctw_context_menu (GtkWidget *widget, GdkEventButton *bev, gpointer data)
{
    if (bev->button == 3) {
	gtk_menu_popup (GTK_MENU(glade_xml_get_widget (xml, "popup")),
			0, 0, 0, 0, bev->button, bev->time);
	return TRUE;
    }

    return FALSE;
}

static void
ctw_menu_toggled (GtkCheckMenuItem *item, gpointer data)
{
    MTClosure *mt = (MTClosure *) data;
    GSList *group;
    gint index;

    if (!gtk_check_menu_item_get_active (item))
	return;

    group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM(item));
    index = g_slist_index (group, (gconstpointer) item);
    gconf_client_set_int (mt->client, OPT_STYLE, index, NULL);
}

static gboolean
ctw_delete_cb (GtkWidget *win, GdkEvent *ev, gpointer data)
{
    MTClosure *mt = (MTClosure *) data;

    gconf_client_set_bool (mt->client, OPT_CTW, FALSE, NULL);

    return TRUE;
}

gboolean
mt_ctw_init (MTClosure *mt, gint x, gint y)
{
    GtkWidget *ctw, *button, *item;
    const gchar *b[] = { "single", "double", "drag", "right" };
    GSList *group;
    gpointer data;
    gint i;

    xml = glade_xml_new (GLADEDIR "/ctw.glade", NULL, NULL);
    if (!xml)
	return FALSE;

    ctw = glade_xml_get_widget (xml, "ctw");
    gtk_window_stick (GTK_WINDOW(ctw));
    gtk_window_set_keep_above (GTK_WINDOW(ctw), TRUE);
    g_signal_connect (G_OBJECT(ctw), "delete-event", 
		      G_CALLBACK(ctw_delete_cb), mt);

    for (i = 0; i < N_CLICK_TYPES; i++) {
	button = glade_xml_get_widget (xml, b[i]);
	gtk_toggle_button_set_mode (GTK_TOGGLE_BUTTON(button), FALSE);

	g_signal_connect (G_OBJECT(button), "toggled", 
			  G_CALLBACK(ctw_button_cb), (gpointer) mt);
	g_signal_connect (G_OBJECT(button), "button-press-event", 
			  G_CALLBACK(ctw_context_menu), NULL);
    }

    item = glade_xml_get_widget (xml, "text");
    g_signal_connect (G_OBJECT(item), "toggled", 
		      G_CALLBACK(ctw_menu_toggled), (gpointer) mt);
    item = glade_xml_get_widget (xml, "icon");
    g_signal_connect (G_OBJECT(item), "toggled", 
		      G_CALLBACK(ctw_menu_toggled), (gpointer) mt);
    item = glade_xml_get_widget (xml, "both");
    g_signal_connect (G_OBJECT(item), "toggled", 
		      G_CALLBACK(ctw_menu_toggled), (gpointer) mt);

    group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM(item));
    data = g_slist_nth_data (group, mt->style);
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(data), TRUE);

    mt_ctw_update_style (mt->style);
    mt_ctw_update_sensitivity (mt);
    mt_ctw_update_visibility (mt);

    if (x != -1 && y != -1)
	gtk_window_move (GTK_WINDOW(ctw), x, y);

    return TRUE;
}