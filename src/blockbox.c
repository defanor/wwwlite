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
#include "blockbox.h"
#include "inlinebox.h"

G_DEFINE_TYPE (BlockBox, block_box, GTK_TYPE_BOX);

static GtkSizeRequestMode block_box_get_request_mode (GtkWidget *widget)
{
  return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

static void
block_box_class_init (BlockBoxClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
  widget_class->get_request_mode = block_box_get_request_mode;
  return;
}

static void
block_box_init (BlockBox *bb)
{
  return;
}

GtkWidget *block_box_new (guint spacing)
{
  BlockBox *bb = BLOCK_BOX(g_object_new(block_box_get_type(),
                                        "orientation", GTK_ORIENTATION_VERTICAL,
                                        "spacing", spacing,
                                        NULL));
  return GTK_WIDGET(bb);
}
