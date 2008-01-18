/*
 * Copyright © 2007 Gerd Kohlberger <lowfi@chello.at>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <gtk/gtk.h>
#include <glade/glade.h>
#include <panel-applet.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <libgnomeui/gnome-help.h>

#include "mt-common.h"
#include "mt-dbus.h"

typedef struct _DwellData DwellData;
struct _DwellData {
    DBusConnection *conn;
    GConfClient    *client;

    GladeXML  *xml;
    GtkWidget *box;
    GtkWidget *button;
    GdkPixbuf *click[4];

    gint button_width;
    gint button_height;
    gint cct;
    gint active;
};

static const gchar *img_widgets[] = {
    "right_click_img",
    "drag_click_img",
    "double_click_img",
    "single_click_img"
};

static const gchar *img_widgets_v[] = {
    "right_click_img_v",
    "drag_click_img_v",
    "double_click_img_v",
    "single_click_img_v"
};

static const gchar menu_xml[] =
    "<popup name=\"button3\">\n"
    "  <menuitem name=\"Properties Item\" verb=\"PropertiesVerb\" "
    "          _label=\"_Preferences\"\n"
    "         pixtype=\"stock\" pixname=\"gtk-properties\"/>\n"
    "  <menuitem name=\"Help Item\" verb=\"HelpVerb\" "
    "          _label=\"_Help\"\n"
    "         pixtype=\"stock\" pixname=\"gtk-help\"/>\n"
    "  <menuitem name=\"About Item\" verb=\"AboutVerb\" _label=\"_About\"\n"
    "         pixtype=\"stock\" pixname=\"gtk-about\"/>\n"
    "</popup>\n";

static void update_sensitivity (DwellData *dd);

static void preferences_dialog (BonoboUIComponent *component,
				gpointer data,
				const char *cname);
static void help_dialog        (BonoboUIComponent *component,
				gpointer data,
				const char *cname);
static void about_dialog       (BonoboUIComponent *component,
				gpointer data,
				const char *cname);

static const BonoboUIVerb menu_verb[] = {
    BONOBO_UI_UNSAFE_VERB ("PropertiesVerb", preferences_dialog),
    BONOBO_UI_UNSAFE_VERB ("HelpVerb", help_dialog),
    BONOBO_UI_UNSAFE_VERB ("AboutVerb", about_dialog),
    BONOBO_UI_VERB_END
};

static void
send_dbus_signal (DBusConnection *conn, const gchar *type, gint arg)
{
    DBusMessage *msg;

    msg = dbus_message_new_signal (MOUSETWEAKS_DBUS_PATH_MAIN,
				   MOUSETWEAKS_DBUS_INTERFACE,
				   type);
    dbus_message_append_args (msg,
			      DBUS_TYPE_INT32, &arg,
			      DBUS_TYPE_INVALID);
    dbus_connection_send (conn, msg, NULL);
    dbus_connection_flush (conn);
    dbus_message_unref (msg);
}

static DBusHandlerResult
receive_dbus_signal (DBusConnection *conn, DBusMessage *msg, void *data)
{
    DwellData *dd = (DwellData *) data;

    if (dbus_message_is_signal (msg,
				MOUSETWEAKS_DBUS_INTERFACE,
				CLICK_TYPE_SIGNAL)) {
	if (dbus_message_get_args (msg,
				   NULL, 
				   DBUS_TYPE_INT32,
				   &dd->cct,
				   DBUS_TYPE_INVALID)) {
	    GSList *group;
	    gpointer data;

	    group = gtk_radio_button_get_group (GTK_RADIO_BUTTON(dd->button));
	    data = g_slist_nth_data (group, dd->cct);
	    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(data), TRUE);

	    return DBUS_HANDLER_RESULT_HANDLED;
	}
    }
    else if (dbus_message_is_signal (msg,
				     MOUSETWEAKS_DBUS_INTERFACE,
				     RESTORE_SIGNAL)) {
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(dd->button), TRUE);

	return DBUS_HANDLER_RESULT_HANDLED;
    }
    else if (dbus_message_is_signal (msg,
				     MOUSETWEAKS_DBUS_INTERFACE,
				     ACTIVE_SIGNAL)) {
	if (dbus_message_get_args (msg,
			       NULL, 
			       DBUS_TYPE_INT32,
			       &dd->active,
			       DBUS_TYPE_INVALID)) {
	    update_sensitivity (dd);

	    return DBUS_HANDLER_RESULT_HANDLED;
	}
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static gboolean
do_not_eat (GtkWidget *widget, GdkEventButton *bev, gpointer user)
{
    if (bev->button != 1)
	g_signal_stop_emission_by_name (widget, "button_press_event");

    return FALSE;
}

static void
button_cb (GtkToggleButton *button, gpointer data)
{
    DwellData *dd = (DwellData *) data;

    if (gtk_toggle_button_get_active (button)) {
	GSList *group;

	group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (button));
	dd->cct = g_slist_index (group, (gconstpointer) button);

	send_dbus_signal (dd->conn, CLICK_TYPE_SIGNAL, dd->cct);
    }
}

static void
box_size_allocate (GtkWidget *widget, GtkAllocation *alloc, gpointer data)
{
    DwellData *dd = (DwellData *) data;
    GtkWidget *w;
    GdkPixbuf *tmp;
    const gchar *name;
    gint i;

    if (dd->button_width == alloc->width && dd->button_height == alloc->height)
	return;

    name = glade_get_widget_name (dd->box);

    if (g_str_equal (name, "box_vert"))
	/* vertical */
	for (i = 0; i < N_CLICK_TYPES; i++) {
	    w = glade_xml_get_widget (dd->xml, img_widgets_v[i]);

	    if (alloc->width < 31) {
		tmp = gdk_pixbuf_scale_simple (dd->click[i],
					       alloc->width - 7,
					       alloc->width - 7,
					       GDK_INTERP_HYPER);
		gtk_image_set_from_pixbuf (GTK_IMAGE(w), tmp);
		g_object_unref (tmp);
	    }
	    else
		gtk_image_set_from_pixbuf (GTK_IMAGE(w), dd->click[i]);
	}
    else
	/* horizontal */
	for (i = 0; i < N_CLICK_TYPES; i++) {
	    w = glade_xml_get_widget (dd->xml, img_widgets[i]);

	    if (alloc->height < 31) {
		tmp = gdk_pixbuf_scale_simple (dd->click[i],
					       alloc->height - 7,
					       alloc->height - 7,
					       GDK_INTERP_HYPER);
		gtk_image_set_from_pixbuf (GTK_IMAGE(w), tmp);
		g_object_unref (tmp);
	    }
	    else
		gtk_image_set_from_pixbuf (GTK_IMAGE(w), dd->click[i]);
	}

    dd->button_width = alloc->width;
    dd->button_height = alloc->height;
}

static void
fini_dwell_data (DwellData *dd)
{
    GtkWidget *w;
    gint i;

    w = glade_xml_get_widget (dd->xml, "box_vert");
    g_object_unref (w);
    w = glade_xml_get_widget (dd->xml, "box_hori");
    g_object_unref (w);

    if (dd->conn)
	dbus_connection_unref (dd->conn);

     if (dd->client)
	g_object_unref (dd->client);

    for (i = 0; i < N_CLICK_TYPES; i++)
	g_object_unref (dd->click[i]);

    g_free (dd);
}

/* applet callbacks */
static void
applet_orient_changed (PanelApplet *applet, guint orient, gpointer data)
{
    DwellData *dd = (DwellData *) data;

    gtk_container_remove (GTK_CONTAINER(applet), dd->box);

    switch (orient) {
    case PANEL_APPLET_ORIENT_UP:
    case PANEL_APPLET_ORIENT_DOWN:
	dd->box = glade_xml_get_widget (dd->xml, "box_hori");
	dd->button = glade_xml_get_widget (dd->xml, "single_click");
	break;
    case PANEL_APPLET_ORIENT_LEFT:
    case PANEL_APPLET_ORIENT_RIGHT:
	dd->box = glade_xml_get_widget (dd->xml, "box_vert");
	dd->button = glade_xml_get_widget (dd->xml, "single_click_v");
    default:
	break;
    }

    if (dd->box->parent)
	gtk_widget_reparent (dd->box, GTK_WIDGET(applet));
    else
	gtk_container_add (GTK_CONTAINER(applet), dd->box);
}

static void
applet_unrealized (GtkWidget *widget, gpointer data)
{
    fini_dwell_data ((DwellData *) data);
}

static void
preferences_dialog (BonoboUIComponent *component,
		    gpointer           data,
		    const char        *cname)
{
    g_spawn_command_line_async ("gnome-mouse-properties", NULL);
}

static void
help_dialog (BonoboUIComponent *component, gpointer data, const char *cname)
{
    GError *error = NULL;

    if (!gnome_help_display_desktop (NULL,
				     "mousetweaks",
				     "mousetweaks",
				     NULL,
				     &error)) {
	mt_show_dialog (_("Couldn't display help"),
			error->message,
			GTK_MESSAGE_ERROR);
	g_error_free (error);
    }
}

static void
about_dialog (BonoboUIComponent *component, gpointer data, const char *cname)
{
    DwellData *dd = (DwellData *) data;
    GtkWidget *about;

    about = glade_xml_get_widget (dd->xml, "about");

    if (GTK_WIDGET_VISIBLE(about))
	gtk_window_present (GTK_WINDOW(about));
    else
	gtk_widget_show (about);
}

static void
about_response (GtkWidget *about, gint response, gpointer data)
{
    DwellData *dd = (DwellData *) data;

    gtk_widget_hide (glade_xml_get_widget (dd->xml, "about"));
}

static inline void
init_button (DwellData *dd, GtkWidget *button)
{
    gtk_toggle_button_set_mode (GTK_TOGGLE_BUTTON(button), FALSE);
    g_signal_connect (G_OBJECT(button), "button-press-event",
		      G_CALLBACK(do_not_eat), NULL);
    g_signal_connect (G_OBJECT(button), "toggled",
		      G_CALLBACK(button_cb), dd);
}

static void
init_horizontal_box (DwellData *dd)
{
    GtkWidget *w;
    gint i;

    w = glade_xml_get_widget (dd->xml, "box_hori");
    g_object_ref (w);

    w = glade_xml_get_widget (dd->xml, "single_click");
    g_signal_connect (G_OBJECT(w), "size-allocate",
		      G_CALLBACK(box_size_allocate), dd);
    init_button (dd, w);

    w = glade_xml_get_widget (dd->xml, "double_click");
    init_button (dd, w);

    w = glade_xml_get_widget (dd->xml, "drag_click");
    init_button (dd, w);

    w = glade_xml_get_widget (dd->xml, "right_click");
    init_button (dd, w);

    for (i = 0; i < N_CLICK_TYPES; i++) {
	w = glade_xml_get_widget (dd->xml, img_widgets[i]);
	gtk_image_set_from_pixbuf (GTK_IMAGE(w), dd->click[i]);
    }
}

static void
init_vertical_box (DwellData *dd)
{
    GtkWidget *w;
    gint i;

    w = glade_xml_get_widget (dd->xml, "box_vert");
    g_object_ref (w);

    w = glade_xml_get_widget (dd->xml, "single_click_v");
    g_signal_connect (G_OBJECT(w), "size-allocate",
		      G_CALLBACK(box_size_allocate), dd);
    init_button (dd, w);

    w = glade_xml_get_widget (dd->xml, "double_click_v");
    init_button (dd, w);

    w = glade_xml_get_widget (dd->xml, "drag_click_v");
    init_button (dd, w);

    w = glade_xml_get_widget (dd->xml, "right_click_v");
    init_button (dd, w);

    for (i = 0; i < N_CLICK_TYPES; i++) {
	w = glade_xml_get_widget (dd->xml, img_widgets_v[i]);
	gtk_image_set_from_pixbuf (GTK_IMAGE(w), dd->click[i]);
    }
}

static void
update_sensitivity (DwellData *dd)
{
    gint mode;
    gboolean enabled, sensitive;

    enabled = gconf_client_get_bool (dd->client, OPT_DWELL, NULL);
    mode = gconf_client_get_int (dd->client, OPT_MODE, NULL);

    sensitive = dd->active && enabled && mode == DWELL_MODE_CTW;
    gtk_widget_set_sensitive (dd->box, sensitive);
}

static void
gconf_value_changed (GConfClient *client,
		     const gchar *key,
		     GConfValue *value,
		     gpointer data)
{
    if (g_str_equal (key, OPT_MODE) || g_str_equal (key, OPT_DWELL))
	update_sensitivity ((DwellData *) data);
}

static gboolean
fill_applet (PanelApplet *applet)
{
    DwellData *dd;
    GtkWidget *about;
    PanelAppletOrient orient;

    dd = g_new0 (DwellData, 1);
    if (!dd)
	return FALSE;

    dd->cct = DWELL_CLICK_TYPE_SINGLE;

    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    dd->xml = glade_xml_new (GLADEDIR "/dwell-click-applet.glade", NULL, NULL);
    if (!dd->xml) {
	fini_dwell_data (dd);
	return FALSE;
    }

    /* about dialog */
    gtk_window_set_default_icon_name (MT_ICON_NAME);

    about = glade_xml_get_widget (dd->xml, "about");
    g_object_set (about, "version", VERSION, NULL);

    g_signal_connect (G_OBJECT(about), "delete-event",
		      G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    g_signal_connect (G_OBJECT(about), "response",
		      G_CALLBACK(about_response), dd);

    dd->conn = dbus_bus_get (DBUS_BUS_SESSION, NULL);
    if (!dd->conn) {
	fini_dwell_data (dd);
	return FALSE;
    }

    dbus_bus_add_match (dd->conn,
			"type='signal',"
			"sender='"   MOUSETWEAKS_DBUS_SERVICE"',"
			"interface='"MOUSETWEAKS_DBUS_INTERFACE"',"
			"path='"     MOUSETWEAKS_DBUS_PATH_APPLET"'",
			NULL);
    dbus_connection_add_filter (dd->conn,
				receive_dbus_signal,
				dd,
				NULL);
    dbus_connection_setup_with_g_main (dd->conn, NULL);

    if (dbus_bus_name_has_owner (dd->conn, MOUSETWEAKS_DBUS_SERVICE, NULL))
	dd->active = 1;

    dd->client = gconf_client_get_default ();
    gconf_client_add_dir (dd->client,
			  MT_GCONF_HOME,
			  GCONF_CLIENT_PRELOAD_ONELEVEL,
			  NULL);
    g_signal_connect (G_OBJECT(dd->client), "value_changed",
		      G_CALLBACK(gconf_value_changed), dd);

    dd->click[DWELL_CLICK_TYPE_SINGLE] =
	gdk_pixbuf_new_from_file (GLADEDIR "/human-single.png", NULL);
    dd->click[DWELL_CLICK_TYPE_DOUBLE] =
	gdk_pixbuf_new_from_file (GLADEDIR "/human-double.png", NULL);
    dd->click[DWELL_CLICK_TYPE_DRAG] =
	gdk_pixbuf_new_from_file (GLADEDIR "/human-drag.png", NULL);
    dd->click[DWELL_CLICK_TYPE_RIGHT] =
	gdk_pixbuf_new_from_file (GLADEDIR "/human-right.png", NULL);

    panel_applet_set_flags (applet,
			    PANEL_APPLET_EXPAND_MINOR |
			    PANEL_APPLET_HAS_HANDLE);
    panel_applet_set_background_widget (applet, GTK_WIDGET(applet));
    panel_applet_setup_menu (applet, menu_xml, menu_verb, dd);

    g_signal_connect (applet, "change-orient",
		      G_CALLBACK(applet_orient_changed), dd);
    g_signal_connect (applet, "unrealize",
		      G_CALLBACK(applet_unrealized), dd);

    orient = panel_applet_get_orient (applet);
    if (orient == PANEL_APPLET_ORIENT_UP ||
	orient == PANEL_APPLET_ORIENT_DOWN) {
	dd->box = glade_xml_get_widget (dd->xml, "box_hori");
	dd->button = glade_xml_get_widget (dd->xml, "single_click");
    }
    else {
	dd->box = glade_xml_get_widget (dd->xml, "box_vert");
	dd->button = glade_xml_get_widget (dd->xml, "single_click_v");
    }

    init_horizontal_box (dd);
    init_vertical_box (dd);

    gtk_widget_reparent (dd->box, GTK_WIDGET(applet));
    gtk_widget_show (GTK_WIDGET(applet));

    update_sensitivity (dd);

    return TRUE;
}

static gboolean
applet_factory (PanelApplet *applet, const gchar *iid, gpointer data)
{
    if (!g_str_equal (iid, "OAFIID:DwellClickApplet"))
	return FALSE;

    return fill_applet (applet);
}

PANEL_APPLET_BONOBO_FACTORY ("OAFIID:DwellClickApplet_Factory",
			     PANEL_TYPE_APPLET,
			     "Dwell Click Factory",
			     "0",
			     applet_factory,
			     NULL);