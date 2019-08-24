/* WWWLite, a lightweight web browser.
   Copyright (C) 2019 defanor

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <glib.h>
#include <gtk/gtk.h>
#include "browserbox.h"

gchar **start_uri = NULL;

static GOptionEntry entries[] =
{
  { G_OPTION_REMAINING, 0, G_OPTION_FLAG_NONE,
    G_OPTION_ARG_STRING_ARRAY, &start_uri, "URI", NULL },
  { NULL }
};

static gboolean
key_press_event_cb (GtkWidget *widget, GdkEventKey *ev, GtkStack *tabs)
{
  if (ev->state & GDK_CONTROL_MASK) {
    if (ev->keyval == GDK_KEY_t) {
      GtkWidget *browser_box = GTK_WIDGET(browser_box_new(NULL));
      BROWSER_BOX(browser_box)->tabs = tabs;
      gtk_stack_add_titled(tabs, browser_box, "New tab", "New tab");
      gtk_widget_show_all(browser_box);
      gtk_stack_set_visible_child(tabs, browser_box);
      return TRUE;
    } else if (ev->keyval == GDK_KEY_w) {
      GtkWidget *current_tab = gtk_stack_get_visible_child(tabs);
      if (current_tab != NULL) {
        gtk_widget_destroy(current_tab);
        return TRUE;
      }
    } else if (ev->keyval == GDK_KEY_s) {
      GtkWidget *current_tab = gtk_stack_get_visible_child(tabs);
      if (current_tab != NULL) {
        BrowserBox *bb = BROWSER_BOX(current_tab);
        if (bb->search_state == SEARCH_INACTIVE) {
          bb->search_state = SEARCH_FORWARD;
          browser_box_display_search_status(bb);
        } else if (bb->search_state == SEARCH_FORWARD) {
          TextSearchState *tss = &(DOCUMENT_BOX(bb->builder_state->docbox)->search);
          if (tss->state == FOUND) {
            tss->start++;
          }
          document_box_find(DOCUMENT_BOX(bb->builder_state->docbox), bb->search_string);
          browser_box_display_search_status(bb);
        }
        return TRUE;
      }
    } else if (ev->keyval == GDK_KEY_g) {
      GtkWidget *current_tab = gtk_stack_get_visible_child(tabs);
      if (current_tab != NULL) {
        BrowserBox *bb = BROWSER_BOX(current_tab);
        if (bb->search_state != SEARCH_INACTIVE) {
          bb->search_state = SEARCH_INACTIVE;
          bb->search_string[0] = 0;
          browser_box_set_status(bb, "Interrupted");
        }
      }
    }
  }
  GtkWidget *current_tab = gtk_stack_get_visible_child(tabs);
  if (current_tab != NULL) {
    BrowserBox *bb = BROWSER_BOX(current_tab);
    if (ev->keyval == GDK_KEY_Back ||
        (bb->search_state == SEARCH_INACTIVE &&
         ev->keyval == GDK_KEY_BackSpace)) {
      return history_back(bb);
    } else if (ev->keyval == GDK_KEY_Forward) {
      return history_forward(bb);
    } else if (bb->search_state != SEARCH_INACTIVE &&
               bb->builder_state != NULL &&
               bb->builder_state->docbox != NULL) {
      size_t ss_len = strlen(bb->search_string);
      DocumentBox *db = DOCUMENT_BOX(bb->builder_state->docbox);
      if (ev->keyval == GDK_KEY_BackSpace && ss_len > 0) {
        /* todo: this won't work well for unicode */
        bb->search_string[ss_len - 1] = 0;
        document_box_find(db, bb->search_string);
        browser_box_display_search_status(bb);
        return TRUE;
      }
      if (ss_len + 4 < MAX_SEARCH_STRING_LEN) {
        gunichar c = gdk_keyval_to_unicode(ev->keyval);
        gint c_len = g_unichar_to_utf8(c, bb->search_string + ss_len);
        if (c_len > 0) {
          bb->search_string[ss_len + c_len] = 0;
          browser_box_display_search_status(bb);
          document_box_find(db, bb->search_string);
          return TRUE;
        }
      }
    }
  }
  return FALSE;
}

static gboolean
button_press_event_cb (GtkWidget *widget, GdkEventButton *ev, GtkStack *tabs)
{
  GtkWidget *current_tab = gtk_stack_get_visible_child(tabs);
  if (current_tab != NULL) {
    BrowserBox *bb = BROWSER_BOX(current_tab);
    if (ev->button == 8) {
      return history_back(bb);
    } else if (ev->button == 9) {
      return history_forward(bb);
    }
  }
  return FALSE;
}

static void activate (GtkApplication *app, gpointer user_data)
{
  GtkWidget *window;

  window = gtk_application_window_new (app);
  gtk_window_resize(GTK_WINDOW(window), 800, 800);
  gtk_window_set_title (GTK_WINDOW (window), "WWWLite");

  GtkWidget *evbox = gtk_event_box_new();
  gtk_container_add (GTK_CONTAINER (window), evbox);

  GtkWidget *box = block_box_new(0);
  gtk_container_add (GTK_CONTAINER (evbox), box);

  GtkWidget *switcher = gtk_stack_switcher_new();
  gtk_container_add (GTK_CONTAINER (box), switcher);

  GtkWidget *stack = gtk_stack_new();
  gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(stack));
  gtk_container_add (GTK_CONTAINER (box), stack);
  gtk_box_set_child_packing(GTK_BOX(box), stack, TRUE, TRUE, 0, GTK_PACK_START);

  GtkWidget *browser_box = GTK_WIDGET(browser_box_new(start_uri != NULL ? start_uri[0] : NULL));
  gtk_stack_add_titled(GTK_STACK(stack), browser_box, "Tab 1", "Tab 1");
  BROWSER_BOX(browser_box)->tabs = GTK_STACK(stack);

  g_signal_connect (evbox, "key-press-event",
                    G_CALLBACK (key_press_event_cb), stack);
  g_signal_connect (evbox, "button-release-event",
                    G_CALLBACK (button_press_event_cb), stack);

  gtk_widget_show_all (window);

  word_cache = g_hash_table_new((GHashFunc)wck_hash, (GEqualFunc)wck_equal);
  return;
}

int
main (int argc, char **argv)
{
  GOptionContext *context = g_option_context_new ("[URI]");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_add_group (context, gtk_get_option_group(TRUE));
  GError *error = NULL;
  GtkApplication *app;
  int status;
  if (! g_option_context_parse (context, &argc, &argv, &error)) {
    g_print("Failed to parse arguments: %s\n", error->message);
    exit(1);
  }

  app = gtk_application_new (NULL, G_APPLICATION_FLAGS_NONE);
  g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);
  status = g_application_run (G_APPLICATION (app), argc, argv);
  g_object_unref (app);

  return status;
}
