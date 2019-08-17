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

#ifndef TABLE_BOX_H
#define TABLE_BOX_H

#include <gtk/gtk.h>
#include "blockbox.h"

G_BEGIN_DECLS

/* cell */

#define TABLE_CELL_TYPE            (table_cell_get_type())
#define TABLE_CELL(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), TABLE_CELL_TYPE, TableCell))
#define TABLE_CELL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), TABLE_CELL_TYPE, TableCellClass))
#define IS_TABLE_CELL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), TABLE_CELL_TYPE))
#define IS_TABLE_CELL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), TABLE_CELL_TYPE))

typedef struct _TableCell TableCell;
typedef struct _TableCellClass TableCellClass;

struct _TableCell
{
  BlockBox parent_instance;
  gint rowspan;
  gint colspan;
};

struct _TableCellClass
{
  BlockBoxClass parent_class;
};

GType table_cell_get_type(void) G_GNUC_CONST;
GtkWidget *table_cell_new();


/* table */

#define TABLE_BOX_TYPE            (table_box_get_type())
#define TABLE_BOX(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), TABLE_BOX_TYPE, TableBox))
#define TABLE_BOX_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), TABLE_BOX_TYPE, TableBoxClass))
#define IS_TABLE_BOX(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), TABLE_BOX_TYPE))
#define IS_TABLE_BOX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), TABLE_BOX_TYPE))

typedef struct _TableBox TableBox;
typedef struct _TableBoxClass TableBoxClass;

struct _TableBox
{
  GtkContainer parent_instance;
  GList *rows;
};

struct _TableBoxClass
{
  GtkContainerClass parent_class;
};

GType table_box_get_type(void) G_GNUC_CONST;
GtkWidget *table_box_new();
void table_box_add_row(TableBox *tb);
/* void table_box_get_dimensions (TableBox *tb, guint *cols, guint *rows); */
/* void table_box_get_column_widths (TableBox *tb, gint *minimal, gint *natural); */


G_END_DECLS

#endif
