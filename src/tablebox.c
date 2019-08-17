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
#include <math.h>
#include "tablebox.h"

G_DEFINE_TYPE (TableCell, table_cell, BLOCK_BOX_TYPE);
G_DEFINE_TYPE (TableBox, table_box, GTK_TYPE_CONTAINER);


/* cell */

static void
table_cell_class_init (TableCellClass *klass)
{
  return;
}

static void
table_cell_init (TableCell *tc)
{
  tc->colspan = 1;
  tc->rowspan = 1;
  return;
}

GtkWidget *
table_cell_new ()
{
  TableCell *tc = TABLE_CELL(g_object_new(table_cell_get_type(),
                                          "orientation", GTK_ORIENTATION_VERTICAL,
                                          "spacing", 10,
                                          NULL));
  return GTK_WIDGET(tc);
}


/* table */

static GType table_box_child_type (GtkContainer *container);
static void table_box_add (GtkContainer *container, GtkWidget *widget);
static void table_box_size_allocate (GtkWidget *widget,
                                     GtkAllocation *allocation);
static void table_box_forall (GtkContainer *container,
                              gboolean include_internals,
                              GtkCallback callback, gpointer callback_data);
static void table_box_remove (GtkContainer *container, GtkWidget *widget);
static GtkSizeRequestMode table_box_get_request_mode (GtkWidget *widget);
static void table_box_get_preferred_width(GtkWidget *widget,
                                          gint *minimal, gint *natural);
static void table_box_get_preferred_height_for_width(GtkWidget *widget,
                                                     gint width,
                                                     gint *minimal,
                                                     gint *natural);
static void table_box_column_widths (TableBox *tb, GList **min_widths, GList **nat_widths);
static void table_box_finalize (GObject *object);

static guint padding = 10;

static void
table_box_class_init (TableBoxClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = table_box_finalize;
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
  widget_class->size_allocate = table_box_size_allocate;
  widget_class->get_request_mode = table_box_get_request_mode;
  widget_class->get_preferred_width = table_box_get_preferred_width;
  widget_class->get_preferred_height_for_width =
    table_box_get_preferred_height_for_width;
  GtkContainerClass *container_class = GTK_CONTAINER_CLASS(klass);
  container_class->child_type = table_box_child_type;
  container_class->add = table_box_add;
  container_class->forall = table_box_forall;
  container_class->remove = table_box_remove;
  return;
}

static void
table_box_init (TableBox *tb)
{
  gtk_widget_set_has_window(GTK_WIDGET(tb), FALSE);
  tb->rows = NULL;
}

static void table_box_finalize (GObject *object)
{
  TableBox *tb = TABLE_BOX(object);
  g_list_free_full(tb->rows, (GDestroyNotify)g_list_free);
  G_OBJECT_CLASS (table_box_parent_class)->finalize (object);
}

GtkWidget *
table_box_new ()
{
  TableBox *tb = TABLE_BOX(g_object_new(table_box_get_type(),
                                        NULL));
  return GTK_WIDGET(tb);
}

void
table_box_add_row (TableBox *tb)
{
  tb->rows = g_list_append(tb->rows, NULL);
  return;
}

static GtkSizeRequestMode
table_box_get_request_mode (GtkWidget *widget)
{
  return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

static void
table_box_get_preferred_width(GtkWidget *widget,
                              gint *minimal, gint *natural)
{
  TableBox *tb = TABLE_BOX(widget);
  GList *minimal_widths, *natural_widths, *iter;
  table_box_column_widths(tb, &minimal_widths, &natural_widths);

  if (minimal != NULL) {
    *minimal = -padding;
    for (iter = minimal_widths; iter; iter = iter->next) {
      *minimal += GPOINTER_TO_INT(iter->data) + padding;
    }
  }
  if (natural != NULL) {
    *natural = -padding;
    for (iter = natural_widths; iter; iter = iter->next) {
      *natural += GPOINTER_TO_INT(iter->data) + padding;
    }
  }
  g_list_free(minimal_widths);
  g_list_free(natural_widths);
}

static void
table_box_get_preferred_height_for_width(GtkWidget *widget,
                                         gint width,
                                         gint *minimal,
                                         gint *natural)
{
  GtkAllocation alloc, child_alloc;
  alloc.x = 0;
  alloc.y = 0;
  alloc.width = width;
  alloc.height = 0;
  table_box_size_allocate(widget, &alloc);

  TableBox *tb = TABLE_BOX(widget);
  GList *row, *cell;
  *minimal = 0;
  for (row = tb->rows; row; row = row->next) {
    for (cell = row->data; cell; cell = cell->next) {
      gtk_widget_get_allocation(cell->data, &child_alloc);
      if (*minimal < child_alloc.y + child_alloc.height) {
        *minimal = child_alloc.y + child_alloc.height;
      }
    }
  }
  *natural = *minimal;
}

static GType
table_box_child_type (GtkContainer *container)
{
  return TABLE_CELL_TYPE;
}

static void
table_box_add (GtkContainer *container, GtkWidget *widget)
{
  TableBox *tb = TABLE_BOX(container);
  GList *row = g_list_last(tb->rows);
  row->data = g_list_append(row->data, widget);
  gtk_widget_set_parent(widget, GTK_WIDGET(container));
  if (gtk_widget_get_visible(widget))
    gtk_widget_queue_resize(GTK_WIDGET(container));
}

static void
table_box_forall (GtkContainer *container, gboolean include_internals,
                  GtkCallback callback, gpointer callback_data)
{
  GList *row, *cell, *next_row, *next_cell;
  row = TABLE_BOX(container)->rows;
  while (row) {
    next_row = row->next;
    cell = row->data;
    while (cell) {
      next_cell = cell->next;
      (* callback) (GTK_WIDGET(cell->data), callback_data);
      cell = next_cell;
    }
    row = next_row;
  }
}

static void
table_box_remove (GtkContainer *container, GtkWidget *widget)
{
  TableBox *tb = TABLE_BOX (container);
  gtk_widget_unparent (widget);
  GList *row, *cell;
  for (row = tb->rows; row; row = row->next) {
    for (cell = row->data; cell; cell = cell->next) {
      if (cell->data == widget) {
        row->data = g_list_delete_link(row->data, cell);
        return;
      }
    }
  }
}

static void
table_box_column_widths (TableBox *tb, GList **min_widths, GList **nat_widths)
{
  /* todo: would be nice to ensure that all the columns end up being
     of approximately the same height */
  GList *row, *cell;
  GList *minimal_widths = NULL;
  GList *natural_widths = NULL;
  GList *descending_cells = NULL;
  gint cell_x, cell_y, cell_next_x;
  cell_y = 0;
  for (row = tb->rows; row; row = row->next) {
    cell_x = 0;
    for (cell = row->data; cell; cell = cell->next) {
      /* Skip the cells spanning from above */
      while (g_list_nth_data(descending_cells, cell_x)) {
        cell_x++;
      }

      TableCell *tc = cell->data;
      gint min, nat;
      gtk_widget_get_preferred_width(GTK_WIDGET(tc), &min, &nat);
      min /= tc->colspan;
      nat /= tc->colspan;
      for (cell_next_x = cell_x; cell_next_x < cell_x + tc->colspan; cell_next_x++) {
        while (g_list_nth(minimal_widths, cell_next_x) == NULL) {
          minimal_widths = g_list_append(minimal_widths, GINT_TO_POINTER(0));
        }
        while (g_list_nth(natural_widths, cell_next_x) == NULL) {
          natural_widths = g_list_append(natural_widths, GINT_TO_POINTER(0));
        }
        while (g_list_nth(descending_cells, cell_next_x) == NULL) {
          descending_cells = g_list_append(descending_cells, GINT_TO_POINTER(0));
        }
        GList *col_min_width = g_list_nth(minimal_widths, cell_next_x);
        if (GPOINTER_TO_INT(col_min_width->data) < min) {
          col_min_width->data = GINT_TO_POINTER(min);
        }
        GList *col_nat_width = g_list_nth(natural_widths, cell_next_x);
        if (GPOINTER_TO_INT(col_nat_width->data) < nat) {
          col_nat_width->data = GINT_TO_POINTER(nat);
        }
        /* Update descending cells */
        GList *descending_cell = g_list_nth(descending_cells, cell_next_x);
        descending_cell->data = GINT_TO_POINTER(tc->rowspan);
      }
      cell_x += tc->colspan;
    }
    GList *descending_cell;
    for (descending_cell = descending_cells; descending_cell;
         descending_cell = descending_cell->next) {
      if (GPOINTER_TO_INT(descending_cell->data) > 0) {
        descending_cell->data--;
      }
    }
    cell_y++;
  }
  *min_widths = minimal_widths;
  *nat_widths = natural_widths;
  g_list_free(descending_cells);
}

static void
table_box_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
  gtk_widget_set_allocation(widget, allocation);
  guint border_width = gtk_container_get_border_width(GTK_CONTAINER(widget));
  gint full_width = allocation->width - 2 * border_width;
  TableBox *tb = TABLE_BOX(widget);
  GList *row, *cell;
  gint x;
  gint y = allocation->y + border_width;
  gint row_height;

  GList *minimal_widths = NULL;
  GList *natural_widths = NULL;
  gint *descending_cells;
  gint *pending_heights;
  gint *actual_widths;
  gint cell_x, cell_y, cell_next_x;

  int natural_width;
  table_box_get_preferred_width(widget, NULL, &natural_width);
  gdouble shrinking = (gdouble)natural_width / (gdouble)full_width;

  table_box_column_widths(tb, &minimal_widths, &natural_widths);
  gint col_cnt = g_list_length(minimal_widths);

  descending_cells = g_malloc0(sizeof(gint) * col_cnt);
  pending_heights = g_malloc0(sizeof(gint) * col_cnt);
  actual_widths = g_malloc0(sizeof(gint) * col_cnt);

  gint extra_width = full_width + padding;

  /* Assign minimal widths to columns */
  for (cell_x = 0, cell = minimal_widths; cell; cell_x++, cell = cell->next) {
    gint minimal_width = GPOINTER_TO_INT(cell->data);
    actual_widths[cell_x] = minimal_width;
    extra_width -= actual_widths[cell_x] + padding;
  }
  /* Distribute remaining width */
  for (cell_x = 0, cell = natural_widths; cell && extra_width > 0; cell_x++, cell = cell->next) {
    gint natural_width = GPOINTER_TO_INT(cell->data);
    if (shrinking <= 1.0) {
      extra_width -= natural_width - actual_widths[cell_x];
      actual_widths[cell_x] = natural_width;
    } else if (natural_width / shrinking > actual_widths[cell_x]) {
      if (extra_width > natural_width / shrinking - actual_widths[cell_x]) {
        extra_width -= natural_width / shrinking - actual_widths[cell_x];
        actual_widths[cell_x] = natural_width / shrinking;
      } else {
        actual_widths[cell_x] += extra_width;
        extra_width = 0;
      }
    }
  }

  cell_y = 0;
  for (row = tb->rows; row; row = row->next) {
    cell_x = 0;
    x = allocation->x + border_width;
    row_height = 0;
    for (cell = row->data; cell; cell = cell->next) {
      /* Skip the cells spanning from above */
      while (cell_x < col_cnt && descending_cells[cell_x] > 0) {
        x += actual_widths[cell_x] + padding;
        cell_x++;
      }
      TableCell *tc = cell->data;
      GtkAllocation child_alloc;
      child_alloc.width = -padding;
      for (cell_next_x = cell_x; cell_next_x < cell_x + tc->colspan; cell_next_x++) {
        child_alloc.width += actual_widths[cell_next_x] + padding;
      }
      gtk_widget_get_preferred_height_for_width(cell->data, child_alloc.width,
                                                &child_alloc.height, NULL);
      child_alloc.x = x;
      child_alloc.y = y;
      gtk_widget_size_allocate(cell->data, &child_alloc);
      x += child_alloc.width + padding;

      for (cell_next_x = cell_x; cell_next_x < cell_x + tc->colspan; cell_next_x++) {
        /* Update descending cells and pending heights */
        descending_cells[cell_next_x] = tc->rowspan;
        pending_heights[cell_next_x] = child_alloc.height;
      }
      cell_x += tc->colspan;
    }
    /* Update descending cells and pending heights, and row_height
       based on those. */
    for (cell_x = 0; cell_x < col_cnt; cell_x++) {
      if (descending_cells[cell_x] > 0) {
        descending_cells[cell_x]--;
        if (descending_cells[cell_x] == 0) {
          if (pending_heights[cell_x] > row_height) {
            row_height = pending_heights[cell_x];
          }
        }
      }
    }
    for (cell_x = 0; cell_x < (gint)g_list_length(minimal_widths); cell_x++) {
      if (pending_heights[cell_x] > row_height) {
        pending_heights[cell_x] -= row_height;
      } else {
        pending_heights[cell_x] = 0;
      }
    }
    y += row_height;
    cell_y++;
  }

  g_list_free(minimal_widths);
  g_list_free(natural_widths);
  free(actual_widths);
  free(descending_cells);
  free(pending_heights);
}
