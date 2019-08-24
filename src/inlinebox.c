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
#include "inlinebox.h"


static GtkSizeRequestMode inline_box_get_request_mode (GtkWidget *widget);
static void inline_box_get_preferred_width(GtkWidget *widget,
                                           gint *minimal, gint *natural);
static void inline_box_get_preferred_height_for_width(GtkWidget *widget,
                                                      gint width,
                                                      gint *minimal,
                                                      gint *natural);
static void inline_box_size_allocate(GtkWidget *widget,
                                     GtkAllocation *allocation);
static GType inline_box_child_type(GtkContainer *container);
static void inline_box_add(GtkContainer *container, GtkWidget *widget);
static void inline_box_remove(GtkContainer *container, GtkWidget *widget);
static void inline_box_forall(GtkContainer *container,
                              gboolean include_internals,
                              GtkCallback callback, gpointer callback_data);
static void inline_box_dispose (GObject *object);
static void inline_box_finalize (GObject *object);
static void ib_text_dispose (GObject *self);
static void ib_link_dispose (GObject *self);


static void ib_text_class_init (IBTextClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  object_class->dispose = ib_text_dispose;
}

static void ib_text_init (IBText *self)
{
  self->layout = NULL;
}

static void ib_text_dispose (GObject *self)
{
  IBText *ibt = IB_TEXT(self);
  g_clear_object(&ibt->layout);
}

IBText *ib_text_new (PangoLayout *layout)
{
  IBText *ib_text = g_object_new (IB_TEXT_TYPE, NULL);
  PangoRectangle extents;
  pango_layout_get_pixel_extents(layout, NULL, &extents);
  ib_text->alloc.x = 0;
  ib_text->alloc.y = 0;
  ib_text->alloc.width = extents.width;
  ib_text->alloc.height = extents.height;
  ib_text->layout = layout;
  g_object_ref(layout);
  return IB_TEXT (ib_text);
}

static void ib_link_class_init (IBLinkClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  object_class->dispose = ib_link_dispose;
}

static void ib_link_init (IBLink *self)
{
  self->url = NULL;
  self->objects = NULL;
}

static void ib_link_dispose (GObject *self)
{
  IBLink *ibl = IB_LINK(self);
  g_free(ibl->url);
  ibl->url = NULL;
}

IBLink *ib_link_new (const gchar *url)
{
  IBLink *ib_link = g_object_new (IB_LINK_TYPE, NULL);
  ib_link->url = strdup(url);
  return ib_link;
}


static void ib_break_class_init (IBBreakClass *klass)
{
  return;
}

static void ib_break_init (IBBreak *self)
{
  return;
}

IBBreak *ib_break_new ()
{
  return g_object_new (IB_BREAK_TYPE, NULL);
}


G_DEFINE_TYPE (IBLink, ib_link, G_TYPE_OBJECT);
G_DEFINE_TYPE (IBBreak, ib_break, G_TYPE_OBJECT);
G_DEFINE_TYPE (IBText, ib_text, G_TYPE_OBJECT);
G_DEFINE_TYPE (InlineBox, inline_box, GTK_TYPE_CONTAINER);


static gint
inline_box_draw (GtkWidget *widget,
                 cairo_t   *cr)
{
  GList *child;
  InlineBox *ib = INLINE_BOX(widget);
  guint text_position = 0;
  for (child = ib->children; child; child = child->next) {
    if (GTK_IS_WIDGET(child->data)) {
      gtk_container_propagate_draw((GTK_CONTAINER(widget)),
                                   GTK_WIDGET(child->data), cr);
      /* todo: render focus around widgets (images in particular)
         too */
    } else if (IS_IB_TEXT(child->data)) {
      IBText *ibt = IB_TEXT(child->data);
      GtkAllocation alloc;
      GtkStyleContext *styleCtx = gtk_widget_get_style_context(widget);
      guint text_len = strlen(pango_layout_get_text(ibt->layout));
      gtk_widget_get_allocation (widget, &alloc);
      cairo_translate (cr, -alloc.x, -alloc.y);

      if (ib->selection_start <= text_position + text_len &&
          ib->selection_end >= text_position) {
        guint sel_start = ibt->alloc.x, sel_width = ibt->alloc.width;
        gint x_pos;
        if (ib->selection_start > text_position) {
          pango_layout_index_to_line_x(ibt->layout,
                                       ib->selection_start - text_position,
                                       FALSE, NULL, &x_pos);
          sel_start += x_pos / PANGO_SCALE;
          sel_width -= x_pos / PANGO_SCALE;
        }
        if (ib->selection_end < text_position + text_len) {
          pango_layout_index_to_line_x(ibt->layout,
                                       ib->selection_end - text_position,
                                       FALSE, NULL, &x_pos);
          sel_width -= ibt->alloc.width - x_pos / PANGO_SCALE;
        }
        /* todo: the following seems to render "inactive" selection,
           but would be nice to render an active one */
        gtk_style_context_add_class(styleCtx, "rubberband");
        gtk_render_background(styleCtx, cr, sel_start, ibt->alloc.y,
                              sel_width, ibt->alloc.height);
        gtk_style_context_remove_class(styleCtx, "rubberband");
      }
      /* duplication here (todo) */
      if (ib->match_start <= text_position + text_len &&
          ib->match_end >= text_position) {
        guint sel_start = ibt->alloc.x, sel_width = ibt->alloc.width;
        gint x_pos;
        if (ib->match_start > text_position) {
          pango_layout_index_to_line_x(ibt->layout,
                                       ib->match_start - text_position,
                                       FALSE, NULL, &x_pos);
          sel_start += x_pos / PANGO_SCALE;
          sel_width -= x_pos / PANGO_SCALE;
        }
        if (ib->match_end < text_position + text_len) {
          pango_layout_index_to_line_x(ibt->layout,
                                       ib->match_end - text_position,
                                       FALSE, NULL, &x_pos);
          sel_width -= ibt->alloc.width - x_pos / PANGO_SCALE;
        }
        gtk_style_context_add_class(styleCtx, "rubberband");
        gtk_render_background(styleCtx, cr, sel_start, ibt->alloc.y,
                              sel_width, ibt->alloc.height);
        gtk_style_context_remove_class(styleCtx, "rubberband");
      }

      gtk_render_layout(styleCtx, cr, ibt->alloc.x, ibt->alloc.y, ibt->layout);

      if (ib->focused_object) {
        if (IS_IB_LINK(ib->focused_object)) {
          IBLink *ibl = IB_LINK(ib->focused_object);
          if (ibl->start <= text_position + text_len &&
              ibl->end > text_position) {
            int start_index = 0, end_index = text_len;
            if (ibl->start > text_position) {
              start_index = ibl->start - text_position;
            }
            if (ibl->end < text_position + text_len) {
              end_index = ibl->end - text_position;
            }
            int start_x = 0, end_x = 0;
            pango_layout_index_to_line_x(ibt->layout, start_index,
                                         0, NULL, &start_x);
            pango_layout_index_to_line_x(ibt->layout, end_index,
                                         0, NULL, &end_x);
            gtk_render_focus(styleCtx, cr,
                             ibt->alloc.x + start_x / PANGO_SCALE,
                             ibt->alloc.y,
                             (end_x - start_x) / PANGO_SCALE,
                             ibt->alloc.height);
          }
        }
      }
      cairo_translate (cr, alloc.x, alloc.y);
      text_position += text_len;
    }
  }
  return FALSE;
}

static gboolean
inline_box_focus (GtkWidget        *widget,
                  GtkDirectionType  direction)
{
  InlineBox *ib = INLINE_BOX(widget);
  if (ib->children == NULL) {
    return FALSE;
  }
  GList *ci;
  guint text_position;
  gboolean focus_next = FALSE;

  if (ib->focused_object == NULL) {
    focus_next = TRUE;
  }

  /* todo: allow moving focus inside a single word */
  for (text_position = 0, ci = ib->children; ci; ci = ci->next) {
    if (focus_next) {
      if (GTK_IS_WIDGET(ci->data)) {
        ib->focused_object = ci->data;
        if (gtk_widget_child_focus(ci->data, direction)) {
          gtk_widget_queue_draw(widget);
          return TRUE;
        }
      } else if (ib->links != NULL && IS_IB_TEXT(ci->data)) {
        GList *li;
        for (li = ib->links; li; li = li->next) {
          if (IB_LINK(li->data)->start <=
              text_position +
              strlen(pango_layout_get_text(IB_TEXT(ci->data)->layout)) &&
              IB_LINK(li->data)->end > text_position) {
            ib->focused_object = li->data;
            gtk_widget_grab_focus(widget);
            gtk_widget_queue_draw(widget);
            return TRUE;
          }
        }
      }
    }
    if (ci->data == ib->focused_object) {
      focus_next = TRUE;
    }

    if (IS_IB_TEXT(ci->data)) {
      text_position +=
        strlen(pango_layout_get_text(IB_TEXT(ci->data)->layout));
      if (IS_IB_LINK(ib->focused_object) &&
          text_position >= IB_LINK(ib->focused_object)->end) {
        focus_next = TRUE;
      }
    }
  }
  ib->focused_object = NULL;
  gtk_widget_queue_draw(widget);
  return FALSE;
}


static void
inline_box_class_init (InlineBoxClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->dispose = inline_box_dispose;
  gobject_class->finalize = inline_box_finalize;

  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
  widget_class->get_request_mode = inline_box_get_request_mode;
  widget_class->get_preferred_width = inline_box_get_preferred_width;
  widget_class->get_preferred_height_for_width =
    inline_box_get_preferred_height_for_width;
  widget_class->size_allocate = inline_box_size_allocate;
  widget_class->draw = inline_box_draw;
  widget_class->focus = inline_box_focus;

  GtkContainerClass *container_class = GTK_CONTAINER_CLASS(klass);
  container_class->child_type = inline_box_child_type;
  container_class->add = inline_box_add;
  container_class->remove = inline_box_remove;
  container_class->forall = inline_box_forall;
}

static void
inline_box_init (InlineBox *ib)
{
  gtk_widget_set_has_window(GTK_WIDGET(ib), FALSE);
  gtk_widget_set_can_focus(GTK_WIDGET(ib), TRUE);
  INLINE_BOX(ib)->children = NULL;
  INLINE_BOX(ib)->links = NULL;
  INLINE_BOX(ib)->focused_object = NULL;
}


InlineBox *inline_box_new ()
{
  InlineBox *ib = INLINE_BOX(g_object_new(inline_box_get_type(), NULL));
  ib->selection_start = 0;
  ib->selection_end = 0;
  ib->children = NULL;
  ib->last_child = NULL;
  ib->wrap = TRUE;
  return ib;
}

static void inline_box_dispose (GObject *object)
{
  InlineBox *ib = INLINE_BOX(object);
  if (ib->children != NULL) {
    GList *il, *next;
    for (il = ib->children; il; il = next) {
      next = il->next;
      if (IS_IB_TEXT(il->data) || IS_IB_BREAK(il->data)) {
        g_object_unref(il->data);
        ib->children = g_list_remove(ib->children, il->data);
      }
    }
  }
  if (ib->links != NULL) {
    g_list_free_full(ib->links, g_object_unref);
    ib->links = NULL;
  }
  G_OBJECT_CLASS (inline_box_parent_class)->dispose (object);
}

static void inline_box_finalize (GObject *object)
{
  InlineBox *ib = INLINE_BOX(object);
  g_list_free(ib->children);
  G_OBJECT_CLASS (inline_box_parent_class)->finalize (object);
}

static GtkSizeRequestMode inline_box_get_request_mode (GtkWidget *widget)
{
  return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

static void
inline_box_get_preferred_width(GtkWidget *widget, gint *minimal, gint *natural)
{
  GList *child;
  gint child_min, child_nat, cur_natural;
  *minimal = 0;
  *natural = 0;
  cur_natural = 0;
  for(child = INLINE_BOX(widget)->children; child; child = child->next) {
    if (GTK_IS_WIDGET(child->data)) {
      gtk_widget_get_preferred_width(GTK_WIDGET(child->data),
                                     &child_min, &child_nat);
      if (*minimal < child_min) {
        *minimal = child_min;
      }
      cur_natural += child_nat;
    } else if (IS_IB_TEXT(child->data)) {
      if (INLINE_BOX(widget)->wrap) {
        if (IB_TEXT(child->data)->alloc.width > *minimal) {
          *minimal = IB_TEXT(child->data)->alloc.width;
        }
      } else {
        /* todo */
      }
      cur_natural += IB_TEXT(child->data)->alloc.width;
    } else if (IS_IB_BREAK(child->data)) {
      if (cur_natural > *natural) {
        *natural = cur_natural;
      }
      cur_natural = 0;
    }
  }
  if (cur_natural > *natural) {
    *natural = cur_natural;
  }
}

static void inline_box_get_preferred_height_for_width(GtkWidget *widget,
                                                      gint width,
                                                      gint *minimal,
                                                      gint *natural)
{
  GtkAllocation alloc, child_alloc;
  GList *child;
  gint child_min;
  alloc.x = 0;
  alloc.y = 0;
  alloc.width = width;
  alloc.height = 0;
  *minimal = 0;
  if (g_list_length(INLINE_BOX(widget)->children) > 0) {
    /* todo: would be better to avoid reusing the same function, since
       it actually allocates child window sizes. */
    inline_box_size_allocate(widget, &alloc);
    for(child = INLINE_BOX(widget)->children; child; child = child->next) {
      child_min = 0;
      if (GTK_IS_WIDGET(child->data)) {
        gtk_widget_get_allocation(GTK_WIDGET(child->data), &child_alloc);
        child_min = child_alloc.y + child_alloc.height;
      } else if (IS_IB_TEXT(child->data)) {
        child_min =
          IB_TEXT(child->data)->alloc.y + IB_TEXT(child->data)->alloc.height;
      }
      if (*minimal < child_min) {
        *minimal = child_min;
      }
    }
  }
  *natural = *minimal;
}

/* todo: this function is rather slow on larger books, in part because
   of pango_layout_get_baseline, which is better to cache. Maybe
   replace PangoLayout in IBText with its subtype, which would
   include cached baseline. */
int line_baseline(GList *iter, int full_width, gboolean wrap) {
  int max_baseline = 0, line_width = 0, cur_baseline = 0;
  for (; iter && (! IS_IB_BREAK(iter->data)); iter = iter->next) {
    if (IS_IB_TEXT(iter->data)) {
      cur_baseline = pango_layout_get_baseline(IB_TEXT(iter->data)->layout);
      line_width += IB_TEXT(iter->data)->alloc.width;
    } else if (GTK_IS_WIDGET(iter->data)) {
      int w;
      gtk_widget_get_preferred_width(iter->data, &w, NULL);
      line_width += w;
    }
    if (wrap && (line_width > full_width)) {
      break;
    }
    if (cur_baseline > max_baseline) {
      max_baseline = cur_baseline;
    }
  }
  max_baseline /= PANGO_SCALE;
  return max_baseline;
}

static void
inline_box_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
  gtk_widget_set_allocation(widget, allocation);

  unsigned border_width =
    gtk_container_get_border_width(GTK_CONTAINER(widget));
  int full_width = allocation->width - 2 * border_width;
  int extra_width = full_width;

  int x = allocation->x + border_width;
  int y = allocation->y + border_width;
  int line_height = 0, max_baseline;

  GList *iter = INLINE_BOX(widget)->children;

  max_baseline = line_baseline(iter, full_width, INLINE_BOX(widget)->wrap);

  for(; iter; iter = iter->next) {
    if (GTK_IS_WIDGET(iter->data)) {

      if(!gtk_widget_get_visible(iter->data))
        continue;

      GtkAllocation child_allocation;
      gtk_widget_get_preferred_width(iter->data, &child_allocation.width, NULL);
      gtk_widget_get_preferred_height(iter->data, &child_allocation.height, NULL);

      if (extra_width < child_allocation.width && extra_width < full_width) {
        x = allocation->x + border_width;
        y += line_height;
        extra_width = full_width;
        line_height = 0;
        max_baseline = line_baseline(iter, full_width, INLINE_BOX(widget)->wrap);
      }

      child_allocation.x = x;
      child_allocation.y = y;
      gtk_widget_size_allocate(iter->data, &child_allocation);
      extra_width -= child_allocation.width;
      x += child_allocation.width;
      line_height = line_height > child_allocation.height
        ? line_height
        : child_allocation.height;
    } else if (IS_IB_TEXT(iter->data)) {
      IBText *ibt = IB_TEXT(iter->data);
      if (INLINE_BOX(widget)->wrap && extra_width < ibt->alloc.width &&
          extra_width < full_width) {
        x = allocation->x + border_width;
        y += line_height;
        extra_width = full_width;
        line_height = 0;
        max_baseline = line_baseline(iter, full_width, INLINE_BOX(widget)->wrap);
      }
      int y_offset = max_baseline - pango_layout_get_baseline(ibt->layout) / PANGO_SCALE;
      ibt->alloc.x = x;
      ibt->alloc.y = y + y_offset;

      if ((guint)x == allocation->x + border_width &&
          INLINE_BOX(widget)->wrap &&
          strcmp(pango_layout_get_text(IB_TEXT(iter->data)->layout), " ") == 0) {
        /* A space in the beginning of a line, not in <pre> */
      } else {
        extra_width -= ibt->alloc.width;
        x += ibt->alloc.width;
        line_height = line_height > (ibt->alloc.height + y_offset)
          ? line_height
          : (ibt->alloc.height + y_offset);
      }
    } else if (IS_IB_BREAK(iter->data)) {
      x = allocation->x + border_width;
      y += line_height;
      extra_width = full_width;
      max_baseline = line_baseline(iter->next, full_width, INLINE_BOX(widget)->wrap);
    }
  }
}

static GType
inline_box_child_type(GtkContainer *container)
{
  return GTK_TYPE_WIDGET;
}

void inline_box_add_text(InlineBox *container, IBText *text)
{
  container->last_child = g_list_append(container->last_child, text);
  if (container->children == NULL) {
    container->children = container->last_child;
  }
  if (container->last_child->next != NULL) {
    container->last_child = container->last_child->next;
  }
  gtk_widget_queue_resize(GTK_WIDGET(container));
}

void inline_box_break(InlineBox *container)
{
  container->last_child = g_list_append(container->last_child, ib_break_new());
  if (container->children == NULL) {
    container->children = container->last_child;
  }
  if (container->last_child->next != NULL) {
    container->last_child = container->last_child->next;
  }
  gtk_widget_queue_resize(GTK_WIDGET(container));
}


static void
inline_box_add(GtkContainer *container, GtkWidget *widget)
{
  InlineBox *ib = INLINE_BOX(container);
  ib->last_child = g_list_append(ib->last_child, widget);
  if (ib->children == NULL) {
    ib->children = ib->last_child;
  }
  if (ib->last_child->next != NULL) {
    ib->last_child = ib->last_child->next;
  }
  gtk_widget_set_parent(widget, GTK_WIDGET(container));
  if(gtk_widget_get_visible(widget))
    gtk_widget_queue_resize(GTK_WIDGET(container));
}

static void
inline_box_remove(GtkContainer *container, GtkWidget *widget)
{
  InlineBox *ib = INLINE_BOX (container);
  gtk_widget_unparent (widget);
  ib->children = g_list_remove (ib->children, widget);
}

static void
inline_box_forall (GtkContainer *container, gboolean include_internals,
                   GtkCallback callback, gpointer callback_data)
{
  InlineBox *ib = INLINE_BOX (container);
  GList *child, *next;
  child = ib->children;
  while (child) {
    /* Current child can be removed and freed, so better remember the
       next one before running the callback. */
    next = child->next;
    if (child && child->data && GTK_IS_WIDGET(child->data)) {
      (* callback) (GTK_WIDGET(child->data), callback_data);
    }
    child = next;
  }
}

gchar*
inline_box_get_text (InlineBox *ib)
{
  GList *child;
  gchar **words = calloc(g_list_length(ib->children) + 1, sizeof(gchar*));
  guint n = 0;
  for (child = ib->children; child; child = child->next) {
    if (IS_IB_TEXT(child->data)) {
      words[n] = (gchar*)pango_layout_get_text(IB_TEXT(child->data)->layout);
      n++;
    }
  }
  gchar *result = g_strjoinv(NULL, words);
  free(words);
  return result;
}

gint
inline_box_search (InlineBox *ib,
                   guint start,
                   gint end,
                   const gchar *str)
{
  gchar *orig_text = inline_box_get_text(ib);
  gchar *text = g_utf8_strdown(orig_text, -1);
  g_free(orig_text);
  if (end != -1) {
    end -= start;
  }
  gchar *result = g_strstr_len(text + start, end, str);
  g_free(text);
  if (result != NULL) {
    return result - text;
  }
  return -1;
}
