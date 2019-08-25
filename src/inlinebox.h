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

#ifndef INLINE_BOX_H
#define INLINE_BOX_H

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS


/* inline box text */

/* Using just a GObject for it (and not a GtkWidget), since it's a few
   times slower with GtkWidget. */

#define IB_TEXT_TYPE (ib_text_get_type())
G_DECLARE_FINAL_TYPE (IBText, ib_text, IB, TEXT, GObject);

struct _IBText {
  GObject parent_instance;
  PangoLayout *layout;
  GtkAllocation alloc;
};

#define IS_IB_TEXT(obj)            (G_TYPE_CHECK_INSTANCE_TYPE((obj), IB_TEXT_TYPE))

IBText* ib_text_new (PangoLayout *layout);
gboolean ib_text_at_point(IBText *ibt, gint x, gint y, gint *position);


/* line break */

#define IB_BREAK_TYPE (ib_break_get_type())
G_DECLARE_FINAL_TYPE (IBBreak, ib_break, IB, BREAK, GObject);

struct _IBBreak
{
  GObject parent_instance;
};

#define IS_IB_BREAK(obj)            (G_TYPE_CHECK_INSTANCE_TYPE((obj), IB_BREAK_TYPE))
IBBreak *ib_break_new ();



/* link */

#define IB_LINK_TYPE (ib_link_get_type())
G_DECLARE_FINAL_TYPE (IBLink, ib_link, IB, LINK, GObject);

struct _IBLink
{
  GObject parent_instance;
  guint start;
  guint end;
  GList *objects;               /* todo */
  gchar *url;
};

#define IS_IB_LINK(obj)            (G_TYPE_CHECK_INSTANCE_TYPE((obj), IB_LINK_TYPE))
IBLink *ib_link_new (const gchar *url);


/* inline box */

#define INLINE_BOX_TYPE            (inline_box_get_type())
#define INLINE_BOX(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), INLINE_BOX_TYPE, InlineBox))
#define INLINE_BOX_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), INLINE_BOX_TYPE, InlineBoxClass))
#define IS_INLINE_BOX(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), INLINE_BOX_TYPE))
#define IS_INLINE_BOX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), INLINE_BOX_TYPE))

typedef struct _InlineBox InlineBox;
typedef struct _InlineBoxClass InlineBoxClass;

struct _InlineBox
{
  GtkContainer parent_instance;
  GList *children;
  GList *last_child;
  /* It would be cleaner to store links as children, but that would
     require additional functions to manage children. Keeping a
     separate list for now; probably it's not worth the complication,
     and it's only needed to manage/draw focus. */
  GList *links;
  GObject *focused_object;
  guint selection_start;
  guint selection_end;
  gboolean wrap;
};

struct _InlineBoxClass
{
  GtkContainerClass parent_class;
};

GType inline_box_get_type (void) G_GNUC_CONST;
InlineBox *inline_box_new (void);
void inline_box_add_text (InlineBox *container, IBText *text);
void inline_box_break (InlineBox *container);
gchar *inline_box_get_text (InlineBox *ib);
gint inline_box_search (InlineBox *ib, guint start, gint end, const gchar *str);
guint inline_box_get_text_length (InlineBox *ib);

G_END_DECLS

#endif
