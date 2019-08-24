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

#ifndef BROWSER_BOX_H
#define BROWSER_BOX_H

#include <gtk/gtk.h>
#include <libsoup/soup.h>
#include "documentbox.h"
#include "inlinebox.h"
#include "blockbox.h"
#include <libxml/HTMLparser.h>

G_BEGIN_DECLS

typedef struct _FormField FormField;
struct _FormField
{
  gchar *name;
  GtkWidget *widget;
};

enum {
  ENCTYPE_URLENCODED,
  ENCTYPE_MULTIPART,
  ENCTYPE_PLAIN
};

typedef struct _Form Form;
struct _Form
{
  gchar *method;
  int enctype;
  SoupURI *action;
  GList *fields;
  gpointer *submission_data;
};

#define BUILDER_STATE_TYPE (builder_state_get_type())
G_DECLARE_FINAL_TYPE (BuilderState, builder_state, BUILDER, STATE, GObject);

struct _BuilderState
{
  GObject parent_instance;
  gboolean active;
  GtkWidget *root;
  DocumentBox *docbox;
  GtkWidget *vbox;
  GSList *stack;
  GdkRGBA link_color;
  guint text_position;
  PangoAttrList *current_attrs;
  IBLink *current_link;
  gchar *current_word;
  gboolean ignore_text;
  gboolean prev_space;
  gboolean pre;
  htmlParserCtxtPtr parser;
  SoupURI *uri;
  GSList *queued_identifiers;
  GHashTable *identifiers;
  gulong anchor_handler_id;
  gchar *option_value;
  GSList *ol_numbers;
  Form *current_form;
};

#define BROWSER_BOX_TYPE            (browser_box_get_type())
#define BROWSER_BOX(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), BROWSER_BOX_TYPE, BrowserBox))
#define BROWSER_BOX_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), BROWSER_BOX_TYPE, BrowserBoxClass))
#define IS_BROWSER_BOX(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), BROWSER_BOX_TYPE))
#define IS_BROWSER_BOX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), BROWSER_BOX_TYPE))


typedef struct _BrowserBox BrowserBox;
typedef struct _BrowserBoxClass BrowserBoxClass;

typedef enum _BTSState BTSState;
enum _BTSState {
  SEARCH_INACTIVE,
  SEARCH_FORWARD,
  SEARCH_BACKWARD
};

#define MAX_SEARCH_STRING_LEN 512

struct _BrowserBox
{
  BlockBox parent_instance;
  SoupSession *soup_session;
  BuilderState *builder_state;
  GtkWidget *address_bar;
  GtkWidget *docbox_root;
  GtkWidget *status_bar;
  GList *forms;
  GList *history;
  GList *history_position;
  BTSState search_state;
  gchar search_string[MAX_SEARCH_STRING_LEN + 1];
  GtkStack *tabs;
  /* GHashTable *word_cache; */
};

struct _BrowserBoxClass
{
  BlockBoxClass parent_class;
};

GType browser_box_get_type(void) G_GNUC_CONST;
BrowserBox *browser_box_new(gchar *uri_str);

typedef struct _WordCacheKey WordCacheKey;
struct _WordCacheKey
{
  gchar *text;
  PangoAttrList *attrs;
};
guint wck_hash (WordCacheKey *wck);
gboolean wck_equal (WordCacheKey *wck1, WordCacheKey *wck2);

void document_request_sm (BrowserBox *bb, SoupMessage *sm);
void document_request (BrowserBox *bb, SoupURI *uri);
gboolean history_back (BrowserBox *bb);
gboolean history_forward (BrowserBox *bb);
void browser_box_set_status(BrowserBox *bb, const gchar *status_str);
void browser_box_display_search_status (BrowserBox *bb);

GHashTable *word_cache;

G_END_DECLS

#endif
