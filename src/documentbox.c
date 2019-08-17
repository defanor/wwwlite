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

#include <gtk/gtk.h>
#include "documentbox.h"
#include "inlinebox.h"

G_DEFINE_TYPE (DocumentBox, document_box, GTK_TYPE_SCROLLED_WINDOW);


static void document_box_dispose (GObject *object) {
  DocumentBox *db = DOCUMENT_BOX(object);
  if (db->links != NULL) {
    /* The same links are also referenced from InlineBox, and freed on
       its disposal, so only the list needs to be freed here. */
    g_list_free(db->links);
    db->links = NULL;
  }
  G_OBJECT_CLASS (document_box_parent_class)->dispose (object);
}

enum {
  FOLLOW,
  SELECT,
  HOVER
};

static guint signals[3];

static GtkSizeRequestMode document_box_get_request_mode (GtkWidget *widget)
{
  return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

static void
document_box_class_init (DocumentBoxClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
  /* GtkContainerClass *container_class = GTK_CONTAINER_CLASS(klass); */
  signals[FOLLOW] =
    g_signal_new("follow",
                 G_TYPE_FROM_CLASS (gobject_class),
                 G_SIGNAL_RUN_LAST,
                 0,             /* class_offset */
                 NULL,          /* accumulator */
                 NULL,          /* accu_data */
                 NULL,          /* c_marshaller */
                 G_TYPE_NONE,   /* return_type */
                 2,             /* n_params */
                 G_TYPE_STRING,
                 G_TYPE_BOOLEAN);
  signals[SELECT] =
    g_signal_new("select",
                 G_TYPE_FROM_CLASS (gobject_class),
                 G_SIGNAL_RUN_LAST,
                 0,             /* class_offset */
                 NULL,          /* accumulator */
                 NULL,          /* accu_data */
                 NULL,          /* c_marshaller */
                 G_TYPE_NONE,   /* return_type */
                 1,             /* n_params */
                 G_TYPE_STRING);
  signals[HOVER] =
    g_signal_new("hover",
                 G_TYPE_FROM_CLASS (gobject_class),
                 G_SIGNAL_RUN_LAST,
                 0,             /* class_offset */
                 NULL,          /* accumulator */
                 NULL,          /* accu_data */
                 NULL,          /* c_marshaller */
                 G_TYPE_NONE,   /* return_type */
                 1,             /* n_params */
                 G_TYPE_STRING);
  widget_class->get_request_mode = document_box_get_request_mode;
  gobject_class->dispose = document_box_dispose;
  return;
}

static void
document_box_init (DocumentBox *db)
{
  db->links = NULL;
}


typedef struct _SearchState SearchState;
struct _SearchState
{
  gint x;
  gint y;
  guint index;
  IBText *ibt;
  InlineBox *ib;
  guint ib_index;
};

static void
text_at_position(GtkWidget *widget, SearchState *st)
{
  GtkAllocation alloc;
  gtk_widget_get_allocation(widget, &alloc);
  if (st->x >= alloc.x &&
      st->x <= alloc.x + alloc.width &&
      st->y >= alloc.y &&
      st->y <= alloc.y + alloc.height) {
    if (IS_INLINE_BOX(widget)) {
      st->ib = INLINE_BOX(widget);
      GList *ti;
      guint text_position = 0;
      for (ti = INLINE_BOX(widget)->children; ti; ti = ti->next) {
        if (IS_IB_TEXT(ti->data)) {
          IBText *ibt = IB_TEXT(ti->data);
          if (st->x >= ibt->alloc.x &&
              st->x <= ibt->alloc.x + ibt->alloc.width &&
              st->y >= ibt->alloc.y &&
              st->y <= ibt->alloc.y + ibt->alloc.height) {
            gint position;
            pango_layout_xy_to_index(ibt->layout,
                                     (st->x - ibt->alloc.x) * PANGO_SCALE,
                                     (st->y - ibt->alloc.y) * PANGO_SCALE,
                                     &position,
                                     NULL);
            st->index = position;
            st->ibt = ibt;
            st->ib_index = text_position + position;
            return;
          }
          text_position += strlen(pango_layout_get_text(ibt->layout));
        }
      }
      return;
    } else if (GTK_IS_CONTAINER(widget)) {
      gtk_container_foreach(GTK_CONTAINER(widget),
                            (GtkCallback)text_at_position, st);
    }
  }
}

static gboolean widget_is_affected(GtkAllocation *wa, GtkAllocation *ta1,
                                   GtkAllocation *ta2)
{
  if (wa == NULL || ta1 == NULL || ta2 == NULL) {
    return FALSE;
  }
  return
    gdk_rectangle_intersect(wa, ta1, NULL) ||
    gdk_rectangle_intersect(wa, ta2, NULL) ||
    ((ta2->y >= ta1->y &&
      ((wa->y >= ta1->y) && (wa->y <= ta2->y))) ||
     ((ta2->y <= ta1->y) &&
      ((wa->y >= ta2->y) && (wa->y <= ta1->y))));
}

/* Assuming (Y, X) is strictly increasing */
static gint compare_positions(GtkAllocation *a1, guint i1,
                              GtkAllocation *a2, guint i2)
{
  if ((a1->y < a2->y) ||
      (a1->y == a2->y && a1->x < a2->x) ||
      (a1->y == a2->y && a1->x == a2->x && i1 < i2)) {
    return -1;
  } else if (a1->y == a2->y && a1->x == a2->x && i1 == i2) {
    return 0;
  } else {
    return 1;
  }
}


static void
selection_update (GtkWidget *widget, SelectionState *st)
{
  GtkAllocation alloc;
  gtk_widget_get_allocation(widget, &alloc);
  if (widget_is_affected(&alloc, &st->selection_start->alloc,
                         &st->selection_end->alloc) ||
      widget_is_affected(&alloc, &st->selection_start->alloc,
                         &st->selection_prev->alloc) ||
      widget_is_affected(&alloc, &st->selection_end->alloc,
                         &st->selection_prev->alloc)) {
    if (IS_INLINE_BOX(widget)) {
      InlineBox *ib = INLINE_BOX(widget);
      ib->selection_end = 0;
      ib->selection_start = 0;
      GList *ti;
      guint text_position = 0;

      for (ti = ib->children; ti; ti = ti->next) {
        if (IS_IB_TEXT(ti->data)) {
          IBText *ibt = IB_TEXT(ti->data);
          gint direction = compare_positions(&st->selection_start->alloc,
                                     st->selection_start_index,
                                     &st->selection_end->alloc,
                                     st->selection_end_index);
          if (direction == -1) {
            if (st->selection_start == ibt) {
              ib->selection_start = st->selection_start_index + text_position;
              st->selecting = TRUE;
            }
            if (st->selecting && st->selection_end == ibt) {
              ib->selection_end = st->selection_end_index + text_position;
              st->selecting = FALSE;
            }
          } else if (direction == 1) {
            if (st->selection_end == ibt) {
              ib->selection_start = st->selection_end_index + text_position;
              st->selecting = TRUE;
            }
            if (st->selecting && st->selection_start == ibt) {
              ib->selection_end = st->selection_start_index + text_position;
              st->selecting = FALSE;
            }
          }
          text_position += strlen(pango_layout_get_text(ibt->layout));
          gtk_widget_queue_draw (widget);
        }
      }
      if (st->selecting) {
        ib->selection_end = text_position;
      }
    } else if (GTK_IS_CONTAINER(widget)) {
      gtk_container_foreach(GTK_CONTAINER(widget),
                            (GtkCallback)selection_update, st);
    }
  }
}

static void
selection_read (GtkWidget *widget, gchar **str)
{
  if (IS_INLINE_BOX(widget)) {
    InlineBox *ib = INLINE_BOX(widget);
    if (ib->selection_end == 0) {
      return;
    }
    GList *ti;
    guint text_position = 0;
    gboolean affected = FALSE, breaks = FALSE;
    for (ti = ib->children; ti; ti = ti->next) {
      if (IS_IB_TEXT(ti->data)) {
        IBText *ibt = IB_TEXT(ti->data);
        const gchar *word = pango_layout_get_text(ibt->layout);
        guint word_len = strlen(word);
        if (ib->selection_start <= text_position + word_len &&
            ib->selection_end > text_position) {
          guint start_offset = 0, end_offset = 0;
          if (ib->selection_start > text_position) {
            start_offset = ib->selection_start - text_position;
          }
          if (ib->selection_end < text_position + word_len) {
            end_offset = text_position + word_len - ib->selection_end;
          }
          guint len = word_len - start_offset - end_offset;
          *str = realloc(*str, strlen(*str) + len + 1);
          g_strlcpy(*str + strlen(*str), word + start_offset, len + 1);
          affected = TRUE;
          breaks = TRUE;
        } else {
          breaks = FALSE;
        }
        text_position += word_len;
      } else if (breaks && IS_IB_BREAK(ti->data)) {
        *str = realloc(*str, strlen(*str) + 2);
        (*str)[strlen(*str) + 1] = 0;
        (*str)[strlen(*str)] = '\n';
        breaks = FALSE;
      }
    }
    if (affected) {
      /* Add one more newline in the end, so that there are newlines
         between paragraphs. */
      *str = realloc(*str, strlen(*str) + 2);
      (*str)[strlen(*str) + 1] = 0;
      (*str)[strlen(*str)] = '\n';
    }
  } else if (GTK_IS_CONTAINER(widget)) {
    gtk_container_foreach(GTK_CONTAINER(widget),
                          (GtkCallback)selection_read, str);
  }
}

static IBLink *find_link (SearchState *ss, gint x, gint y)
{
  if (ss->ib && ss->ib->links != NULL) {
    GList *li;
    for (li = ss->ib->links; li; li = li->next) {
      IBLink *link = IB_LINK(li->data);
      if (ss->ibt) {
        if (link->start <= ss->ib_index && link->end > ss->ib_index) {
          return link;
        }
      }
      GList *oi;
      GtkAllocation alloc;
      for (oi = link->objects; oi; oi = oi->next) {
        gtk_widget_get_allocation(oi->data, &alloc);
        if (alloc.x <= x && alloc.x + alloc.width >= x &&
            alloc.y <= y && alloc.y + alloc.height >= y) {
          return link;
        }
      }
    }
  }
  return NULL;
}

static gboolean
button_press_event_cb (GtkWidget      *widget,
                       GdkEventButton *event,
                       DocumentBox    *db)
{
  if (event->button != 1) {
    return FALSE;
  }
  SearchState ss;
  gint orig_x, orig_y, ev_orig_x, ev_orig_y;
  gdk_window_get_origin(gtk_widget_get_parent_window(GTK_WIDGET(db->evbox)),
                        &orig_x, &orig_y);
  gdk_window_get_origin(event->window, &ev_orig_x, &ev_orig_y);
  ss.ibt = NULL;
  ss.x = ev_orig_x - orig_x + event->x;
  ss.y = ev_orig_y - orig_y + event->y;
  text_at_position(widget, &ss);

  if (db->sel.selection_end) {
    /* Remove existing selection */
    db->sel.selection_prev = db->sel.selection_end;
    db->sel.selection_prev_index = db->sel.selection_end_index + 1;
    db->sel.selection_end = db->sel.selection_start;
    db->sel.selection_end_index = db->sel.selection_start_index;
    selection_update(widget, &db->sel);
  }

  if (ss.ibt) {
    db->sel.selection_active = TRUE;
    db->sel.selection_start = ss.ibt;
    db->sel.selection_start_index = ss.index;
    db->sel.selection_end = ss.ibt;
    db->sel.selection_end_index = ss.index;
    /* todo: grab focus when any non-widget space is clicked, not
       just texts */
    gtk_widget_grab_focus(GTK_WIDGET(db));
  }
  return FALSE;
}

static gboolean
motion_notify_event_cb (GtkWidget      *widget,
                        GdkEventButton *event,
                        DocumentBox    *db)
{
  SearchState ss;
  gint orig_x, orig_y, ev_orig_x, ev_orig_y;
  gdk_window_get_origin(gtk_widget_get_parent_window(GTK_WIDGET(db->evbox)),
                        &orig_x, &orig_y);
  gdk_window_get_origin(event->window, &ev_orig_x, &ev_orig_y);
  ss.ib = NULL;
  ss.ibt = NULL;
  ss.x = ev_orig_x - orig_x + event->x;
  ss.y = ev_orig_y - orig_y + event->y;
  text_at_position(widget, &ss);
  if (ss.ibt && db->sel.selection_active) {
    db->sel.selection_prev = db->sel.selection_end;
    db->sel.selection_prev_index = db->sel.selection_end_index;
    db->sel.selection_end = ss.ibt;
    db->sel.selection_end_index = ss.index;
    db->sel.selecting = FALSE;
    selection_update(widget, &db->sel);
  }
  IBLink *link = find_link(&ss, event->x, event->y);
  if (link != NULL) {
    g_signal_emit(db, signals[HOVER], 0, link->url);
  }
  return FALSE;
}


static gboolean
button_release_event_cb (GtkWidget      *widget,
                         GdkEventButton *event,
                         DocumentBox    *db)
{
  if (event->button != 1 && event->button != 2) {
    return FALSE;
  }
  gchar *str = malloc(1);
  gboolean got_selection = FALSE;
  str[0] = 0;
  selection_read(widget, &str);
  if (strlen(str) > 0) {
    /* Strip the last newline */
    str[strlen(str) - 1] = 0;
    got_selection = TRUE;
  }
  g_signal_emit(db, signals[SELECT], 0, str);
  g_free(str);
  db->sel.selection_active = FALSE;
  if (got_selection) {
    return FALSE;
  }

  SearchState ss;
  gint orig_x, orig_y, ev_orig_x, ev_orig_y;
  gdk_window_get_origin(gtk_widget_get_parent_window(GTK_WIDGET(db->evbox)),
                        &orig_x, &orig_y);
  gdk_window_get_origin(event->window, &ev_orig_x, &ev_orig_y);
  ss.ib = NULL;
  ss.ibt = NULL;
  ss.x = ev_orig_x - orig_x + event->x;
  ss.y = ev_orig_y - orig_y + event->y;
  text_at_position(widget, &ss);

  IBLink *link = find_link(&ss, event->x, event->y);
  if (link != NULL) {
    g_signal_emit(db, signals[FOLLOW], 0, link->url, event->button == 2);
    return TRUE;
  }
  return FALSE;
}

static void
find_focused (GtkWidget *widget, GObject **focused) {
  if (IS_INLINE_BOX(widget)) {
    if (INLINE_BOX(widget)->focused_object != NULL) {
      *focused = INLINE_BOX(widget)->focused_object;
      return;
    }
  } else if (GTK_IS_CONTAINER(widget)) {
    gtk_container_foreach(GTK_CONTAINER(widget),
                          (GtkCallback)find_focused, focused);
  }
}


static gboolean
key_press_event_cb (GtkWidget *widget, GdkEventKey *event, DocumentBox *db)
{
  if (event->keyval == GDK_KEY_Return) {
    GObject *focused;
    /* This is inefficient and can be optimised, but there's no
       perceivable delay, so perhaps it doesn't worth adding more
       code. */
    find_focused(widget, &focused);
    if (focused != NULL && IS_IB_LINK(focused)) {
      g_signal_emit(db, signals[FOLLOW], 0, IB_LINK(focused)->url,
                    event->state & GDK_CONTROL_MASK);
      return TRUE;
    }
  }
  return FALSE;
}

DocumentBox *document_box_new ()
{
  DocumentBox *db = DOCUMENT_BOX(g_object_new(document_box_get_type(),
                                              "hadjustment", NULL,
                                              "vadjustment", NULL,
                                              NULL));
  db->evbox = GTK_EVENT_BOX(gtk_event_box_new());
  gtk_widget_add_events(GTK_WIDGET(db->evbox), GDK_POINTER_MOTION_MASK);
  gtk_container_add (GTK_CONTAINER (db), GTK_WIDGET (db->evbox));

  g_signal_connect (db->evbox, "button-press-event",
                    G_CALLBACK (button_press_event_cb), db);
  g_signal_connect (db->evbox, "button-release-event",
                    G_CALLBACK (button_release_event_cb), db);
  g_signal_connect (db->evbox, "motion-notify-event",
                    G_CALLBACK (motion_notify_event_cb), db);
  g_signal_connect (db->evbox, "key-press-event",
                    G_CALLBACK (key_press_event_cb), db);
  db->links = NULL;
  db->sel.selection_active = FALSE;
  return db;
}
