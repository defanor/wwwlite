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

#ifndef DOCUMENT_BOX_H
#define DOCUMENT_BOX_H

#include <gtk/gtk.h>
#include "inlinebox.h"
#include <libsoup/soup.h>

G_BEGIN_DECLS

#define DOCUMENT_BOX_TYPE            (document_box_get_type())
#define DOCUMENT_BOX(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), DOCUMENT_BOX_TYPE, DocumentBox))
#define DOCUMENT_BOX_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), DOCUMENT_BOX_TYPE, DocumentBoxClass))
#define IS_DOCUMENT_BOX(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), DOCUMENT_BOX_TYPE))
#define IS_DOCUMENT_BOX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), DOCUMENT_BOX_TYPE))

typedef struct _DocumentBox DocumentBox;
typedef struct _DocumentBoxClass DocumentBoxClass;

typedef struct _SelectionState SelectionState;
struct _SelectionState
{
  IBText *selection_start;
  guint selection_start_index;
  IBText *selection_end;
  guint selection_end_index;
  IBText *selection_prev;
  guint selection_prev_index;
  gboolean selection_active;
  gboolean selecting;
};

typedef enum _TSState TSState;
enum _TSState {
  START,
  LOOKING,
  FOUND
};

typedef struct _TextSearchState TextSearchState;
struct _TextSearchState
{
  InlineBox *ib;
  gint start;
  gint end;
  const gchar *str;
  gboolean forward;
  TSState state;
};

struct _DocumentBox
{
  GtkScrolledWindow parent_instance;
  GtkEventBox *evbox;
  GList *links;
  SelectionState sel;
  TextSearchState search;
  GdkWindow *event_window;
};

struct _DocumentBoxClass
{
  GtkScrolledWindowClass parent_class;
};

GType document_box_get_type(void) G_GNUC_CONST;
DocumentBox *document_box_new(void);
gboolean document_box_find (DocumentBox *db, const gchar *str);

G_END_DECLS

#endif
