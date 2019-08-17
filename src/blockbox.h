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

#ifndef BLOCK_BOX_H
#define BLOCK_BOX_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define BLOCK_BOX_TYPE            (block_box_get_type())
#define BLOCK_BOX(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), BLOCK_BOX_TYPE, BlockBox))
#define BLOCK_BOX_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), BLOCK_BOX_TYPE, BlockBoxClass))
#define IS_BLOCK_BOX(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), BLOCK_BOX_TYPE))
#define IS_BLOCK_BOX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), BLOCK_BOX_TYPE))

typedef struct _BlockBox BlockBox;
typedef struct _BlockBoxClass BlockBoxClass;

struct _BlockBox
{
  GtkBox parent_instance;
};

struct _BlockBoxClass
{
  GtkBoxClass parent_class;
};

GType block_box_get_type(void) G_GNUC_CONST;
GtkWidget *block_box_new(guint spacing);


G_END_DECLS

#endif
