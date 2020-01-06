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

/* The code in this file is particularly messy, and some of it should
   be reorganised. */

#include <glib.h>
#include <gtk/gtk.h>
#include "browserbox.h"
#include "inlinebox.h"
#include "blockbox.h"
#include "tablebox.h"
#include "documentbox.h"
#include <libxml/HTMLparser.h>
#include <libsoup/soup.h>


typedef struct _ImageSetData ImageSetData;
struct _ImageSetData
{
  GtkImage *image;
  BuilderState *bs;
};



G_DEFINE_TYPE (BuilderState, builder_state, G_TYPE_OBJECT);
G_DEFINE_TYPE (BrowserBox, browser_box, BLOCK_BOX_TYPE);

/* todo: move some of the properties into BrowserBox (or DocumentBox),
   particularly the ones that are used after rendering. */
static void builder_state_init (BuilderState *bs)
{
  bs->active = TRUE;
  bs->vbox = NULL;
  bs->docbox = NULL;
  bs->root = NULL;
  bs->stack = g_slist_alloc();
  bs->stack->data = NULL;
  bs->text_position = 0;
  bs->current_attrs = pango_attr_list_new();
  bs->current_link = NULL;
  bs->current_word = NULL;
  bs->ignore_text = FALSE;
  bs->prev_space = TRUE;
  bs->pre = FALSE;
  bs->parser = NULL;
  bs->uri = NULL;
  bs->queued_identifiers = NULL;
  bs->identifiers =
    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  bs->anchor_handler_id = 0;
  bs->option_value = NULL;
  bs->ol_numbers = NULL;
  bs->current_form = NULL;
}

BuilderState *builder_state_new (GtkWidget *root)
{
  BuilderState *bs = g_object_new (BUILDER_STATE_TYPE, NULL);
  bs->root = root;
  GtkStyleContext *styleCtx = gtk_widget_get_style_context(root);
  gtk_style_context_get_color(styleCtx, GTK_STATE_FLAG_LINK, &bs->link_color);
  return bs;
}

void builder_state_dispose (GObject *self)
{
  BuilderState *bs = BUILDER_STATE(self);
  if (bs->parser) {
    htmlFreeParserCtxt(bs->parser);
    bs->parser = NULL;
  }
  if (bs->stack) {
    g_slist_free(bs->stack);
    bs->stack = NULL;
  }
  if (bs->current_attrs) {
    pango_attr_list_unref(bs->current_attrs);
    bs->current_attrs = NULL;
  }
  if (bs->identifiers) {
    g_hash_table_unref(bs->identifiers);
    bs->identifiers = NULL;
  }
  if (bs->queued_identifiers) {
    g_slist_free(bs->queued_identifiers);
    bs->queued_identifiers = NULL;
  }
  if (bs->option_value) {
    free(bs->option_value);
    bs->option_value = NULL;
  }
  if (bs->ol_numbers) {
    g_slist_free_full(bs->ol_numbers, g_free);
    bs->ol_numbers = NULL;
  }
  G_OBJECT_CLASS (builder_state_parent_class)->dispose (self);
}

static void builder_state_class_init (BuilderStateClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  object_class->dispose = builder_state_dispose;
}

void scroll_to (BuilderState *bs, GObject *target)
{
  GtkAllocation widget_alloc, *alloc;
  if (GTK_IS_WIDGET(target)) {
    gtk_widget_get_allocation(GTK_WIDGET(target), &widget_alloc);
    alloc = &widget_alloc;
  } else if (IS_IB_TEXT(target)) {
    alloc = &IB_TEXT(target)->alloc;
  } else {
    puts("Shouldn't happen");
    return;
  }
  GtkAdjustment *adj =
    gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(bs->docbox));
  gtk_adjustment_set_value(adj, alloc->y);
  gtk_scrolled_window_set_vadjustment(GTK_SCROLLED_WINDOW(bs->docbox),
                                      adj);
}

void scroll_to_identifier(BuilderState *bs, const char *identifier)
{
  GObject *target = g_hash_table_lookup(bs->identifiers, identifier);
  if (target != NULL) {
    scroll_to(bs, target);
  }
}

void browser_box_set_status(BrowserBox *bb, const gchar *status_str) {
  gtk_statusbar_remove_all(GTK_STATUSBAR(bb->status_bar),
                           gtk_statusbar_get_context_id(GTK_STATUSBAR(bb->status_bar), "status"));
  gtk_statusbar_push(GTK_STATUSBAR(bb->status_bar),
                     gtk_statusbar_get_context_id(GTK_STATUSBAR(bb->status_bar), "status"),
                     status_str);
}

void browser_box_display_search_status (BrowserBox *bb) {
  gchar status[MAX_SEARCH_STRING_LEN + 33];
  if (bb->search_state == SEARCH_FORWARD) {
    sprintf(status, "Forward search: %s", bb->search_string);
    browser_box_set_status(bb, status);
  }
}

void image_set (SoupSession *session, SoupMessage *msg, ImageSetData *isd)
{
  /* Just setting a whole image at once for now, progressive loading
     is left for later. */
  if (isd->bs->active && msg->response_body->data != NULL) {
    GdkPixbufLoader *il = gdk_pixbuf_loader_new();
    GError *err = NULL;
    gdk_pixbuf_loader_write(il, (unsigned char *)msg->response_body->data,
                            msg->response_body->length, &err);
    gdk_pixbuf_loader_close(il, &err);
    GdkPixbuf *pb = gdk_pixbuf_loader_get_pixbuf(il);
    if (pb != NULL) {
      /* Temporarily scaling large images on loading: it's imprecise
         and generally awkward, but better than embedding huge images.
         Better to resize on window resize and along size allocation
         in the future, but GTK is unhappy if it's done during size
         allocation, and without storing the original image, it also
         leads to poor quality (i.e., perhaps will need a custom
         GtkImage subtype). */
      int doc_width = gtk_widget_get_allocated_width(GTK_WIDGET(isd->bs->root));
      int pb_width = gdk_pixbuf_get_width(pb);
      int pb_height = gdk_pixbuf_get_height(pb);
      if (pb_width > doc_width) {
        GdkPixbuf *old_pb = pb;
        int new_height = (double)pb_height * (double)doc_width / (double)pb_width;
        if (new_height < 1) {
          new_height = 1;
        }
        pb = gdk_pixbuf_scale_simple(old_pb, doc_width, new_height,
                                     GDK_INTERP_BILINEAR);
      }
      if (pb != NULL) {
        gtk_image_set_from_pixbuf(isd->image, pb);
        if (pb_width > doc_width) {
          g_object_unref(pb);
        }
      }
    }
    g_object_unref(il);
  }
  free(isd);
  g_object_unref(isd->bs);
}


/* Word cache utilities */

guint pango_attr_hash (PangoAttribute *attr)
{
  /* todo: that's not a great hash, maybe improve later */
  return attr->klass->type ^ attr->start_index ^ attr->end_index;
}

guint wck_hash (WordCacheKey *wck)
{
  guint attr_hash = 0;
  PangoAttrIterator *pai = pango_attr_list_get_iterator(wck->attrs);
  GSList *attrs = pango_attr_iterator_get_attrs(pai);
  GSList *ai;
  for (ai = attrs; ai; ai = ai->next) {
    attr_hash ^= pango_attr_hash(ai->data);
  }
  g_slist_free_full(attrs, (GDestroyNotify)pango_attribute_destroy);
  pango_attr_iterator_destroy(pai);
  guint text_hash = g_str_hash(wck->text);
  return attr_hash ^ text_hash;
}

gboolean wck_equal (WordCacheKey *wck1, WordCacheKey *wck2)
{
  PangoAttrIterator *pai1 = pango_attr_list_get_iterator(wck1->attrs);
  PangoAttrIterator *pai2 = pango_attr_list_get_iterator(wck2->attrs);
  GSList *attrs1 = pango_attr_iterator_get_attrs(pai1);
  GSList *attrs2 = pango_attr_iterator_get_attrs(pai2);
  GSList *ai1, *ai2;
  for (ai1 = attrs1, ai2 = attrs2; ai1 || ai2; ai1 = ai1->next, ai2 = ai2->next) {
    if (( ! (ai1 && ai2)) || ( ! pango_attribute_equal(ai1->data, ai2->data))) {
      g_slist_free_full(attrs1, (GDestroyNotify)pango_attribute_destroy);
      g_slist_free_full(attrs2, (GDestroyNotify)pango_attribute_destroy);
      pango_attr_iterator_destroy(pai1);
      pango_attr_iterator_destroy(pai2);
      return FALSE;
    }
  }
  g_slist_free_full(attrs1, (GDestroyNotify)pango_attribute_destroy);
  g_slist_free_full(attrs2, (GDestroyNotify)pango_attribute_destroy);
  pango_attr_iterator_destroy(pai1);
  pango_attr_iterator_destroy(pai2);
  return g_str_equal(wck1->text, wck2->text);
}




PangoLayout *get_layout(GtkWidget *widget, const gchar *text,
                        PangoAttrList *attrs)
{
  WordCacheKey *wck = malloc(sizeof(WordCacheKey));
  wck->text = strdup(text);
  wck->attrs = attrs;
  pango_attr_list_ref(wck->attrs);
  PangoLayout *pl = g_hash_table_lookup(word_cache, wck);
  if (pl == NULL) {
    pl = gtk_widget_create_pango_layout(widget, text);
    pango_layout_set_attributes(pl, attrs);
    g_hash_table_insert(word_cache, wck, pl);
  } else {
    free(wck->text);
    pango_attr_list_unref(wck->attrs);
    free(wck);
  }
  return pl;
}

PangoAttrList *shift_attributes(PangoAttrList *src_attrs, guint len)
{
  PangoAttrIterator *pai;
  PangoAttrList *new_attrs;
  PangoAttribute *attr;
  GSList *iter_al, *al;
  new_attrs = pango_attr_list_new();
  pai = pango_attr_list_get_iterator(src_attrs);
  if (pai != NULL) {
    do {
      iter_al = pango_attr_iterator_get_attrs(pai);
      for (al = iter_al; al; al = al->next) {
        attr = al->data;
        if (attr->end_index > len || attr->end_index == G_MAXUINT) {
          attr->start_index = 0;
          if (attr->end_index != G_MAXUINT) {
            attr->end_index -= len;
          }
          pango_attr_list_insert(new_attrs, attr);
        } else {
          pango_attribute_destroy(attr);
        }
      }
      g_slist_free(iter_al);
    } while (pango_attr_iterator_next(pai));
    pango_attr_iterator_destroy(pai);
  }
  pango_attr_list_unref(src_attrs);
  return new_attrs;
}

void attribute_start(PangoAttrList *attrs, PangoAttribute *attr, guint position)
{
  attr->start_index = position;
  pango_attr_list_insert(attrs, attr);
}

/* todo: better tracking of attributes is needed; this would end all
   the matching attributes instead of just a particular one */
PangoAttrList *attribute_end(PangoAttrList *attrs,
                             PangoAttrType type, guint position)
{
  PangoAttrIterator *pai;
  PangoAttrList *new_attrs;
  PangoAttribute *attr;
  GSList *iter_al, *al;
  new_attrs = pango_attr_list_new();
  pai = pango_attr_list_get_iterator(attrs);
  if (pai != NULL) {
    do {
      iter_al = pango_attr_iterator_get_attrs(pai);
      for (al = iter_al; al; al = al->next) {
        attr = al->data;
        if (attr->klass->type == type && attr->end_index > position) {
          attr->end_index = position;
        }
        pango_attr_list_change(new_attrs, attr);
      }
      g_slist_free(iter_al);
    } while (pango_attr_iterator_next(pai));
    pango_attr_iterator_destroy(pai);
  }
  pango_attr_list_unref(attrs);
  return new_attrs;
}

void ensure_inline_box (BuilderState *bs)
{
  if (! IS_INLINE_BOX(bs->stack->data)) {
    if (GTK_IS_CONTAINER(bs->stack->data)) {
      InlineBox *ib = inline_box_new();
      bs->text_position = 0;
      gtk_container_add (GTK_CONTAINER (bs->stack->data), GTK_WIDGET (ib));
      gtk_widget_show_all (GTK_WIDGET(ib));
      bs->stack = g_slist_prepend(bs->stack, ib);
    } else {
      puts("neither a text nor a container");
      return;
    }
  }
}

void anchor_allocated (GtkWidget    *widget,
                       GdkRectangle *alloc,
                       BuilderState *bs)
{
  GtkAdjustment *adj =
    gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(bs->docbox));
  gtk_adjustment_set_value(adj, alloc->y);
  gtk_scrolled_window_set_vadjustment(GTK_SCROLLED_WINDOW(bs->docbox),
                                      adj);
  g_signal_handler_disconnect(widget, bs->anchor_handler_id);
  bs->anchor_handler_id = 0;
}

IBText *add_word(BuilderState *bs, gchar *word, PangoAttrList **attrs)
{
  ensure_inline_box(bs);
  InlineBox *ib = bs->stack->data;
  IBText *ibt = NULL;
  if (word[0] != 0) {
    PangoLayout *pl = get_layout(GTK_WIDGET(ib), word, *attrs);
    ibt = ib_text_new(pl);
    inline_box_add_text(ib, ibt);
    *attrs = shift_attributes(*attrs, strlen(word));
    if (bs->queued_identifiers) {
      GSList *ii;
      for (ii = bs->queued_identifiers; ii; ii = ii->next) {
        const char *fragment = soup_uri_get_fragment(bs->uri);
        if (fragment && bs->anchor_handler_id == 0 &&
            strcmp(ii->data, fragment) == 0) {
          bs->anchor_handler_id =
            g_signal_connect (ib, "size-allocate",
                              G_CALLBACK(anchor_allocated), bs);
        }
        g_hash_table_insert(bs->identifiers, ii->data, ibt);
      }
      g_slist_free(bs->queued_identifiers);
      bs->queued_identifiers = NULL;
    }
  }
  return ibt;
}




void history_add (BrowserBox *bb, SoupURI *uri)
{
  if (bb->history_position && soup_uri_equal(uri, bb->history_position->data)) {
    return;
  }
  if (bb->history_position != NULL && bb->history_position->next != NULL) {
    GList *tail = bb->history_position->next;
    bb->history_position->next = NULL;
    tail->prev = NULL;
    g_list_free(tail);
  }
  bb->history = g_list_append(bb->history, soup_uri_copy(uri));
  bb->history_position = g_list_last(bb->history);
}

gboolean history_back (BrowserBox *bb)
{
  if (bb->history_position != NULL && bb->history_position->prev) {
    bb->history_position = bb->history_position->prev;
    document_request(bb, soup_uri_copy(bb->history_position->data));
    return TRUE;
  }
  return FALSE;
}

gboolean history_forward (BrowserBox *bb)
{
  if (bb->history_position != NULL && bb->history_position->next) {
    bb->history_position = bb->history_position->next;
    document_request(bb, soup_uri_copy(bb->history_position->data));
    return TRUE;
  }
  return FALSE;
}

static void form_submit (GtkButton *button, gpointer ptr)
{
  Form *form = ptr;
  BrowserBox *bb = BROWSER_BOX(form->submission_data);
  gchar *method = "GET";
  puts("submitting");
  if (form->method != NULL) {
    if (g_ascii_strncasecmp(form->method, "post", 4) == 0) {
      method = "POST";
    }
  }
  gchar *uri_str = soup_uri_to_string(form->action, FALSE);

  if (form->enctype == ENCTYPE_URLENCODED) {
    GHashTable *fields = g_hash_table_new(g_str_hash, g_str_equal);
    GList *fi;
    for (fi = form->fields; fi; fi = fi->next) {
      FormField *ff = fi->data;
      if (GTK_IS_ENTRY(ff->widget)) {
        g_hash_table_insert(fields, ff->name,
                            (gpointer)gtk_entry_get_text(GTK_ENTRY(ff->widget)));
      } else if (GTK_IS_COMBO_BOX(ff->widget)) {
        if (gtk_combo_box_get_active_id(GTK_COMBO_BOX(ff->widget)) != NULL) {
          g_hash_table_insert(fields, ff->name,
                              (gpointer)gtk_combo_box_get_active_id(GTK_COMBO_BOX(ff->widget)));
        }
      }
    }
    SoupMessage *sm = soup_form_request_new_from_hash(method, uri_str, fields);
    g_hash_table_unref(fields);
    history_add(bb, soup_message_get_uri(sm));
    document_request_sm(bb, sm);
  } else if (form->enctype == ENCTYPE_MULTIPART) {
    puts("multipart, not supported yet");
  } else if (form->enctype == ENCTYPE_PLAIN) {
    puts("plain, not supported yet");
  }
  g_free(uri_str);
}



void sax_characters (BrowserBox *bb, const xmlChar * ch, int len)
{
  BuilderState *bs = bb->builder_state;
  if (bs->ignore_text || IS_TABLE_BOX(bs->stack->data)) {
    return;
  }

  char *value = malloc(len + 1);
  g_strlcpy(value, (const char*)ch, len + 1);

  if (GTK_IS_COMBO_BOX_TEXT(bs->stack->data)) {
    if (bs->option_value) {
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(bs->stack->data),
                                bs->option_value, value);
      free(bs->option_value);
      bs->option_value = NULL;
    }
    free(value);
    return;
  }

  ensure_inline_box(bs);

  gint i = 0, j = 0;
  while (i < len) {
    if (value[i] == ' ' || value[i] == '\n' ||
        value[i] == '\r' || value[i] == '\t') {
      gchar c = value[i];
      value[i] = 0;
      if (bs->current_word != NULL) {
        bs->current_word =
          realloc(bs->current_word,
                  strlen(bs->current_word) + strlen(value + j) + 1);
        g_strlcpy(bs->current_word + strlen(bs->current_word),
                  value + j, strlen(value + j) + 1);
        add_word(bs, bs->current_word, &bs->current_attrs);
        free(bs->current_word);
        bs->current_word = NULL;
      } else {
        add_word(bs, value + j, &bs->current_attrs);
      }
      bs->text_position += strlen(value + j);
      if (bs->pre && c == '\n') {
        inline_box_break(INLINE_BOX(bs->stack->data));
      } else {
        if (bs->pre || ! bs->prev_space) {
          add_word(bs, " ", &bs->current_attrs);
          bs->text_position += 1;
          bs->prev_space = TRUE;
        }
      }
      j = i + 1;
    } else {
      bs->prev_space = FALSE;
    }
    i++;
  }
  if (i > j) {
    if (bs->current_word == NULL) {
      bs->current_word = strdup(value + j);
    } else {
      bs->current_word =
        realloc(bs->current_word,
                strlen(bs->current_word) + strlen(value + j) + 1);
      g_strlcpy(bs->current_word + strlen(bs->current_word),
                value + j, strlen(value + j) + 1);
    }
    bs->text_position += strlen(value + j);
  }
  free(value);
}

gboolean element_is_blocking (const char *name)
{
  /* Not including <div> elements: the results of their inclusion
     aren't always good, and according to the specification they have
     no special meaning at all. */
  return (strcmp(name, "p") == 0 ||
          strcmp(name, "h1") == 0 || strcmp(name, "h2") == 0 ||
          strcmp(name, "h3") == 0 || strcmp(name, "h4") == 0 ||
          strcmp(name, "h5") == 0 || strcmp(name, "h6") == 0 ||
          strcmp(name, "pre") == 0 || strcmp(name, "ul") == 0 ||
          strcmp(name, "ol") == 0 || strcmp(name, "li") == 0 ||
          strcmp(name, "dl") == 0 || strcmp(name, "dt") == 0 ||
          strcmp(name, "dd") == 0 || strcmp(name, "table") == 0 ||
          strcmp(name, "td") == 0 || strcmp(name, "th") == 0 ||
          strcmp(name, "tr") == 0
          );
}

gboolean element_flushes_text (const char *name)
{
  return (element_is_blocking (name) ||
          (strcmp(name, "br") == 0 || strcmp(name, "img") == 0 ||
           strcmp(name, "input") == 0 || strcmp(name, "select") == 0
           ));
}

void sax_start_element (BrowserBox *bb,
                        const xmlChar * u_name,
                        const xmlChar ** attrs)
{
  BuilderState *bs = bb->builder_state;
  const char *name = (const char*)u_name;

  if (IS_INLINE_BOX(bs->stack->data)) {
    if (element_flushes_text(name)) {
      if (bs->current_word != NULL) {
        add_word(bs, bs->current_word, &bs->current_attrs);
        free(bs->current_word);
        bs->current_word = NULL;
      }
      bs->prev_space = TRUE;
    }
    if (element_is_blocking(name)) {
      GSList *next = bs->stack->next;
      if (next != NULL) {
        g_slist_free_1(bs->stack);
        bs->stack = next;
      }
    }
    /* Line breaks */
    if (strcmp(name, "br") == 0) {
      inline_box_break(INLINE_BOX(bs->stack->data));
    }
  }

  if (IS_BLOCK_BOX(bs->stack->data)) {
    /* Elements that (may) need inline boxes */
    if (strcmp(name, "a") == 0 ||
        strcmp(name, "br") == 0 ||
        strcmp(name, "img") == 0 ||
        strcmp(name, "select") == 0 ||
        strcmp(name, "input") == 0) {
      ensure_inline_box(bs);
    }
  }

  if (IS_BLOCK_BOX(bs->stack->data)) {
    /* Lists */
    if (strcmp(name, "dl") == 0 || strcmp(name, "ul") == 0 ||
        strcmp(name, "ol") == 0) {
      /* todo: maybe use a dedicated widget for ul and ol */
      GtkWidget *dl = block_box_new(0);
      gtk_container_add (GTK_CONTAINER (bs->stack->data), GTK_WIDGET (dl));
      gtk_widget_show_all(dl);
      bs->stack = g_slist_prepend(bs->stack, dl);
      if ((strcmp(name, "ol") == 0) || (strcmp(name, "ul") == 0)) {
        guint *num = malloc(sizeof(guint));
        if (strcmp(name, "ol") == 0) {
          *num = 1;
        } else if (strcmp(name, "ul") == 0) {
          *num = 0;
        }
        bs->ol_numbers = g_slist_prepend(bs->ol_numbers, num);
      }
    }

    if (strcmp(name, "dd") == 0) {
      GtkWidget *dd = block_box_new(10);
      gtk_container_add (GTK_CONTAINER (bs->stack->data), GTK_WIDGET (dd));
      gtk_widget_show_all(dd);
      bs->stack = g_slist_prepend(bs->stack, dd);
      gtk_widget_set_margin_start(bs->stack->data, 32);
    }

    if (bs->ol_numbers) {
      if (strcmp(name, "li") == 0) {
        GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gchar *str;
        guint num = *((guint*)bs->ol_numbers->data);
        if (num == 0) {
          str = "*";
        } else {
          str = g_strdup_printf("%u.", num);
          *((guint*)bs->ol_numbers->data) = num + 1;
        }
        GtkWidget *lbl = gtk_label_new(str);
        if (num > 0) {
          g_free(str);
        }
        GtkWidget *vbox = block_box_new(10);
        gtk_widget_set_valign(lbl, GTK_ALIGN_START);
        gtk_container_add (GTK_CONTAINER (hbox), GTK_WIDGET (lbl));
        gtk_container_add (GTK_CONTAINER (hbox), GTK_WIDGET (vbox));
        gtk_container_add (GTK_CONTAINER (bs->stack->data), hbox);
        gtk_widget_show_all(hbox);
        bs->stack = g_slist_prepend(bs->stack, hbox);
        bs->stack = g_slist_prepend(bs->stack, vbox);
      }
    }

    /* Tables */
    if (strcmp(name, "table") == 0) {
      GtkWidget *tb = table_box_new();
      gtk_container_add (GTK_CONTAINER (bs->stack->data), tb);
      gtk_widget_show_all(tb);
      bs->stack = g_slist_prepend(bs->stack, tb);
    }

    /* Preformatted texts */
    if (strcmp(name, "pre") == 0 && IS_BLOCK_BOX(bs->stack->data)) {
      InlineBox *ib = inline_box_new();
      bs->text_position = 0;
      ib->wrap = FALSE;
      gtk_container_add (GTK_CONTAINER (bs->stack->data), GTK_WIDGET (ib));
      gtk_widget_show_all(GTK_WIDGET(ib));
      bs->stack = g_slist_prepend(bs->stack, ib);
      bs->pre = TRUE;
      attribute_start(bs->current_attrs, pango_attr_family_new("mono"), 0);
    }
  }

  if (IS_TABLE_BOX(bs->stack->data)) {
    if (strcmp(name, "tr") == 0) {
      table_box_add_row(bs->stack->data);
    }

    if (TABLE_BOX(bs->stack->data)->rows != NULL) {
      if (strcmp(name, "td") == 0 || strcmp(name, "th") == 0) {
        GtkWidget *tc = table_cell_new();
        if (attrs != NULL) {
          const gchar *rowspan = NULL, *colspan = NULL;
          guint i;
          for (i = 0; attrs[i]; i += 2){
            if (g_strcmp0((const char*)attrs[i], "colspan") == 0) {
              colspan = (const char*)attrs[i+1];
            }
            if (g_strcmp0((const char*)attrs[i], "rowspan") == 0) {
              rowspan = (const char*)attrs[i+1];
            }
          }
          if (rowspan != NULL) {
            sscanf(rowspan, "%u", &(TABLE_CELL(tc)->rowspan));
            if (TABLE_CELL(tc)->rowspan > 65534) {
              TABLE_CELL(tc)->rowspan = 65534;
            } else if (TABLE_CELL(tc)->rowspan == 0) {
              TABLE_CELL(tc)->rowspan = 1;
            }
          }
          if (colspan != NULL) {
            sscanf(colspan, "%u", &(TABLE_CELL(tc)->colspan));
            if (TABLE_CELL(tc)->colspan > 65534) {
              TABLE_CELL(tc)->colspan = 65534;
            } else if (TABLE_CELL(tc)->colspan == 0) {
              TABLE_CELL(tc)->colspan = 1;
            }
          }
        }
        gtk_container_add (GTK_CONTAINER (bs->stack->data), tc);
        gtk_widget_show_all(tc);
        bs->stack = g_slist_prepend(bs->stack, tc);
      }
    }
  }

  /* Ignored */
  if (strcmp(name, "head") == 0 || strcmp(name, "script") == 0 ||
      strcmp(name, "style") == 0) {
    bs->ignore_text = TRUE;
  }

  /* Images */
  if (IS_INLINE_BOX(bs->stack->data)) {
    if (strcmp(name, "img") == 0) {
      guint i;
      const char *src = NULL;
      if (attrs != NULL) {
        for (i = 0; attrs[i]; i += 2){
          if (strcmp((const char*)attrs[i], "src") == 0) {
            src = (const char*)attrs[i+1];
          }
        }
      }
      if (src != NULL) {
        GtkWidget *image = gtk_image_new_from_file(NULL);
        if (image != NULL) {
          /* todo: progressive image loading */
          gtk_container_add (GTK_CONTAINER (bs->stack->data), image);
          gtk_widget_show_all(image);

          SoupURI *uri = soup_uri_new_with_base(bs->uri, src);
          SoupMessage *sm = soup_message_new_from_uri("GET", uri);
          soup_uri_free(uri);
          ImageSetData *isd = malloc(sizeof(ImageSetData));
          isd->image = GTK_IMAGE(image);
          isd->bs = bs;
          g_object_ref(bs);
          soup_session_queue_message(bb->soup_session, sm,
                                     (SoupSessionCallback)image_set, isd);
          if (bs->current_link != NULL) {
            bs->current_link->objects =
              g_list_prepend(bs->current_link->objects, image);
          }
        }
      }
    }

    /* Inputs */
    if (strcmp(name, "input") == 0) {
      guint i;
      const char *type = NULL, *value = NULL, *a_name = NULL;
      if (attrs != NULL) {
        for (i = 0; attrs[i]; i += 2){
          if (g_strcmp0((const char*)attrs[i], "type") == 0) {
            type = (const char*)attrs[i+1];
          }
          if (g_strcmp0((const char*)attrs[i], "value") == 0) {
            value = (const char*)attrs[i+1];
          }
          if (g_strcmp0((const char*)attrs[i], "name") == 0) {
            a_name = (const char*)attrs[i+1];
          }
        }
      }
      GtkWidget *input = NULL;
      if (g_strcmp0(type, "submit") == 0) {
        input =
          gtk_button_new_with_label(value == NULL ? "submit" : value);
        if (bs->current_form != NULL) {
          g_signal_connect (input, "clicked",
                            G_CALLBACK(form_submit), bs->current_form);
        }
      } else if (g_strcmp0(type, "checkbox") == 0) {
        input = gtk_check_button_new();
      } else {
        /* Defaulting to type=text */
        input = gtk_entry_new();
        if (value != NULL) {
          gtk_entry_set_text(GTK_ENTRY(input), value);
        }
        if (bs->current_form != NULL) {
          g_signal_connect (input, "activate",
                            G_CALLBACK(form_submit), bs->current_form);
        }
      }
      if (input != NULL) {
        gtk_container_add (GTK_CONTAINER (bs->stack->data), input);
        if (g_strcmp0(type, "hidden") != 0) {
          gtk_widget_show_all(input);
        }
      }
      if (input != NULL && bs->current_form != NULL && a_name != NULL) {
        FormField *ff = malloc(sizeof(FormField));
        ff->name = strdup(a_name);
        ff->widget = input;
        bs->current_form->fields = g_list_append(bs->current_form->fields, ff);
      }
    }
    if (strcmp(name, "select") == 0) {
      const gchar *a_name = NULL;
      if (attrs != NULL) {
        guint i;
        for (i = 0; attrs[i]; i += 2){
          if (g_strcmp0((const char*)attrs[i], "name") == 0) {
            a_name = (const char*)attrs[i+1];
          }
        }
      }
      GtkWidget *cbox = gtk_combo_box_text_new();
      gtk_container_add (GTK_CONTAINER (bs->stack->data), cbox);
      bs->stack = g_slist_prepend(bs->stack, cbox);
      gtk_widget_show_all(cbox);
      if (bs->current_form != NULL && a_name != NULL) {
        FormField *ff = malloc(sizeof(FormField));
        ff->name = strdup(a_name);
        ff->widget = cbox;
        bs->current_form->fields = g_list_append(bs->current_form->fields, ff);
      }
    }

    /* Links */
    if (strcmp(name, "a") == 0) {
      guint i;
      const gchar *href = NULL;
      if (attrs != NULL) {
        for (i = 0; attrs[i]; i += 2){
          if (strcmp((const char*)attrs[i], "href") == 0) {
            href = (const char*)attrs[i+1];
          }
        }
      }
      if (href != NULL) {
        bs->current_link = ib_link_new(href);

        bs->current_link->start = bs->text_position;
        INLINE_BOX(bs->stack->data)->links =
          g_list_append(INLINE_BOX(bs->stack->data)->links, bs->current_link);
        bs->docbox->links = g_list_append(bs->docbox->links, bs->current_link);
      }
    }
  }
  if (GTK_IS_COMBO_BOX_TEXT(bs->stack->data)) {
    if (strcmp(name, "option") == 0) {
      guint i;
      if (attrs != NULL) {
        for (i = 0; attrs[i]; i += 2) {
          if (strcmp((const char*)attrs[i], "value") == 0) {
            if (bs->option_value != NULL) {
              free(bs->option_value);
            }
            bs->option_value = strdup((const char*)attrs[i+1]);
          }
        }
      }
    }
  }


  /* Formatting */
  if (strcmp(name, "b") == 0 || strcmp(name, "strong") == 0) {
    attribute_start(bs->current_attrs,
                    pango_attr_weight_new(PANGO_WEIGHT_BOLD),
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
  } else if (strcmp(name, "i") == 0 || strcmp(name, "em") == 0) {
    attribute_start(bs->current_attrs,
                    pango_attr_style_new(PANGO_STYLE_ITALIC),
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
  } else if (strcmp(name, "code") == 0) {
    attribute_start(bs->current_attrs,
                    pango_attr_family_new("mono"),
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
  } else if (strcmp(name, "sub") == 0) {
    /* todo: avoid using a constant */
    attribute_start(bs->current_attrs,
                    pango_attr_rise_new(-5 * PANGO_SCALE),
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
    attribute_start(bs->current_attrs,
                    pango_attr_scale_new(0.8),
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
  } else if (strcmp(name, "sup") == 0) {
    /* todo: avoid using a constant */
    attribute_start(bs->current_attrs,
                    pango_attr_rise_new(5 * PANGO_SCALE),
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
    attribute_start(bs->current_attrs,
                    pango_attr_scale_new(0.8),
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
  } else if (strcmp(name, "h1") == 0) {
    attribute_start(bs->current_attrs,
                    pango_attr_scale_new(1.8),
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
    attribute_start(bs->current_attrs,
                    pango_attr_weight_new(PANGO_WEIGHT_SEMIBOLD),
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
  } else if (strcmp(name, "h2") == 0) {
    attribute_start(bs->current_attrs,
                    pango_attr_scale_new(1.6),
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
    attribute_start(bs->current_attrs,
                    pango_attr_weight_new(PANGO_WEIGHT_SEMIBOLD),
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
  } else if (strcmp(name, "h3") == 0) {
    attribute_start(bs->current_attrs,
                    pango_attr_scale_new(1.4),
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
    attribute_start(bs->current_attrs,
                    pango_attr_weight_new(PANGO_WEIGHT_SEMIBOLD),
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
  } else if (strcmp(name, "h4") == 0) {
    attribute_start(bs->current_attrs,
                    pango_attr_scale_new(1.3),
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
    attribute_start(bs->current_attrs,
                    pango_attr_weight_new(PANGO_WEIGHT_SEMIBOLD),
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
  } else if (strcmp(name, "h5") == 0) {
    attribute_start(bs->current_attrs,
                    pango_attr_scale_new(1.2),
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
    attribute_start(bs->current_attrs,
                    pango_attr_weight_new(PANGO_WEIGHT_SEMIBOLD),
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
  } else if (strcmp(name, "h6") == 0) {
    attribute_start(bs->current_attrs,
                    pango_attr_scale_new(1.1),
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
    attribute_start(bs->current_attrs,
                    pango_attr_weight_new(PANGO_WEIGHT_SEMIBOLD),
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
  } else if (strcmp(name, "a") == 0) {
    attribute_start(bs->current_attrs,
                    pango_attr_foreground_new(bs->link_color.red * 65535,
                                              bs->link_color.green * 65535,
                                              bs->link_color.blue * 65535),
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
    attribute_start(bs->current_attrs,
                    pango_attr_underline_new(PANGO_UNDERLINE_SINGLE),
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
  }

  /* Identifiers */
  if (attrs != NULL) {
    guint i;
    for (i = 0; attrs[i]; i += 2){
      if (strcmp((const char*)attrs[i], "id") == 0 ||
          (strcmp(name, "a") == 0 &&
           strcmp((const char*)attrs[i], "name") == 0)) {
        bs->queued_identifiers =
          g_slist_prepend(bs->queued_identifiers,
                          strdup((const char*)attrs[i + 1]));
      }
    }
  }
  if (bs->queued_identifiers && GTK_IS_IMAGE(bs->stack->data)) {
    GSList *ii;
    for (ii = bs->queued_identifiers; ii; ii = ii->next) {
      /* todo: perhaps abstract this into a function, since it's the
         same for texts. */
      const char *fragment = soup_uri_get_fragment(bs->uri);
      if (fragment && bs->anchor_handler_id == 0 &&
          strcmp(ii->data, fragment) == 0) {
        bs->anchor_handler_id =
          g_signal_connect (bs->stack->data, "size-allocate",
                            G_CALLBACK(anchor_allocated), bs);
      }
      g_hash_table_insert(bs->identifiers, ii->data, bs->stack->data);
    }
    g_slist_free_full(bs->queued_identifiers, g_free);
    bs->queued_identifiers = NULL;
  }

  /* Forms */
  if (strcmp(name, "form") == 0) {
    Form *form = malloc(sizeof(Form));
    form->submission_data = (gpointer)bb;
    form->method = NULL;
    form->enctype = ENCTYPE_URLENCODED;
    form->action = NULL;
    form->fields = NULL;
    guint i;
    gchar *action = NULL;
    if (attrs != NULL) {
      for (i = 0; attrs[i]; i += 2) {
        if (strcmp((const char*)attrs[i], "method") == 0) {
          form->method = strdup((const char*)attrs[i+1]);
        }
        if (strcmp((const char*)attrs[i], "enctype") == 0) {
          if (strcmp((const char*)attrs[i + 1], "multipart/form-data") == 0) {
            form->enctype = ENCTYPE_MULTIPART;
          } else if (strcmp((const char*)attrs[i + 1], "text/plain") == 0) {
            form->enctype = ENCTYPE_PLAIN;
          }
        }
        if (strcmp((const char*)attrs[i], "action") == 0) {
          action = strdup((const char*)attrs[i+1]);
        }
      }
    }
    if (action == NULL) {
      form->action = soup_uri_copy(bs->uri);
    } else {
      form->action = soup_uri_new_with_base(bs->uri, action);
    }
    bb->forms = g_list_prepend(bb->forms, form);
    bs->current_form = form;
  }
}

void sax_end_element (BrowserBox *bb, const xmlChar *u_name)
{
  BuilderState *bs = bb->builder_state;
  const char *name = (const char*)u_name;

  if (IS_INLINE_BOX(bs->stack->data)) {
    if (element_flushes_text(name)) {
      if (bs->current_word != NULL) {
        add_word(bs, bs->current_word, &bs->current_attrs);
        free(bs->current_word);
        bs->current_word = NULL;
      }
      bs->prev_space = TRUE;
    }
    if (element_is_blocking(name)) {
      GSList *next = bs->stack->next;
      if (next != NULL) {
        g_slist_free_1(bs->stack);
        bs->stack = next;
      }
      bs->prev_space = TRUE;
    }
  }

  if ((strcmp(name, "dl") == 0 || strcmp(name, "ul") == 0 ||
       strcmp(name, "ol") == 0 || strcmp(name, "dd") == 0 ||
       strcmp(name, "li") == 0 || strcmp(name, "select") == 0)) {
    GSList *next = bs->stack->next;
    if (next != NULL) {
      g_slist_free_1(bs->stack);
      bs->stack = next;
    }
    if (bs->ol_numbers != NULL &&
        (strcmp(name, "ol") == 0 || strcmp(name, "ul") == 0)) {
      GSList *next = bs->ol_numbers->next;
      g_free(bs->ol_numbers->data);
      g_slist_free_1(bs->ol_numbers);
      bs->ol_numbers = next;
    }
  }
  if (bs->stack && strcmp(name, "li") == 0) {
    /* repeat */
    GSList *next = bs->stack->next;
    if (next != NULL) {
      g_slist_free_1(bs->stack);
      bs->stack = next;
    }
  }

  if (strcmp(name, "option") == 0 && bs->option_value != NULL) {
    free(bs->option_value);
    bs->option_value = NULL;
  }

  /* Tables */
  if (IS_TABLE_BOX(bs->stack->data)) {
    if (strcmp(name, "table") == 0) {
      GSList *next = bs->stack->next;
      if (next != NULL) {
        g_slist_free_1(bs->stack);
        bs->stack = next;
      }
    }
  }
  if (IS_TABLE_CELL(bs->stack->data)) {
    if (strcmp(name, "td") == 0 || strcmp(name, "th") == 0) {
      GSList *next = bs->stack->next;
      if (next != NULL) {
        g_slist_free_1(bs->stack);
        bs->stack = next;
      }
    }
  }

  /* Preformatted texts */
  if (strcmp(name, "pre") == 0) {
    bs->pre = FALSE;
    bs->current_attrs = attribute_end(bs->current_attrs, PANGO_ATTR_FAMILY, 0);
  }

  /* Ignored */
  if (strcmp(name, "head") == 0 || strcmp(name, "script") == 0 ||
      strcmp(name, "style") == 0) {
    bs->ignore_text = FALSE;
  }

  /* Formatting */
  if (strcmp(name, "b") == 0 || strcmp(name, "strong") == 0) {
    bs->current_attrs =
      attribute_end(bs->current_attrs,
                    PANGO_ATTR_WEIGHT,
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
  } else if (strcmp(name, "i") == 0 || strcmp(name, "em") == 0) {
    bs->current_attrs =
      attribute_end(bs->current_attrs,
                    PANGO_ATTR_STYLE,
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
  } else if (strcmp(name, "code") == 0) {
    bs->current_attrs =
      attribute_end(bs->current_attrs,
                    PANGO_ATTR_FAMILY,
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
  } else if (strcmp(name, "sub") == 0) {
    bs->current_attrs =
      attribute_end(bs->current_attrs,
                    PANGO_ATTR_RISE,
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
    bs->current_attrs =
      attribute_end(bs->current_attrs,
                    PANGO_ATTR_SCALE,
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
  } else if (strcmp(name, "sup") == 0) {
    bs->current_attrs =
      attribute_end(bs->current_attrs,
                    PANGO_ATTR_RISE,
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
    bs->current_attrs =
      attribute_end(bs->current_attrs,
                    PANGO_ATTR_SCALE,
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
  } else if (strcmp(name, "h1") == 0 || strcmp(name, "h2") == 0 ||
             strcmp(name, "h3") == 0 || strcmp(name, "h4") == 0 ||
             strcmp(name, "h5") == 0 || strcmp(name, "h6") == 0) {
    bs->current_attrs =
      attribute_end(bs->current_attrs,
                    PANGO_ATTR_SCALE,
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
    bs->current_attrs =
      attribute_end(bs->current_attrs,
                    PANGO_ATTR_WEIGHT,
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
  } else if (strcmp(name, "a") == 0) {
    bs->current_attrs =
      attribute_end(bs->current_attrs,
                    PANGO_ATTR_FOREGROUND,
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
    bs->current_attrs =
      attribute_end(bs->current_attrs,
                    PANGO_ATTR_UNDERLINE,
                    (bs->current_word == NULL ? 0 : strlen(bs->current_word)));
    if (bs->current_link != NULL) {
      bs->current_link->end = bs->text_position;
      bs->current_link = NULL;
    }
  }

  /* Forms */
  if (strcmp(name, "form") == 0) {
    bs->current_form = NULL;
  }
}





void select_text_cb (void *ptr,
                     gchar *str,
                     BrowserBox *bb)
{
  printf("Selection: '%s'\n", str);
}


void follow_link_cb (void *ptr,
                     gchar *url,
                     gboolean new_tab,
                     BrowserBox *bb)
{
  BuilderState *bs = bb->builder_state;
  SoupURI *new_uri = soup_uri_new_with_base(bs->uri, url);
  if (url[0] == '#') {
    char *uri_str = soup_uri_to_string(new_uri, FALSE);
    gtk_entry_set_text(GTK_ENTRY(bb->address_bar), uri_str);
    free(uri_str);
    soup_uri_free(new_uri);
    scroll_to_identifier(bs, url + 1);
    return;
  }
  BrowserBox *target_bb = bb;
  if (new_tab) {
    target_bb = browser_box_new(NULL);
    gtk_widget_show_all(GTK_WIDGET(target_bb));
    target_bb->tabs = bb->tabs;
    gtk_stack_add_titled(GTK_STACK(target_bb->tabs), GTK_WIDGET(target_bb),
                         url, url);
  }
  history_add(target_bb, new_uri);
  document_request(target_bb, new_uri);
}

void hover_link_cb (void *ptr,
                    gchar *url,
                    BrowserBox *bb)
{
  browser_box_set_status(bb, url);
}


xmlSAXHandler sax = {
  .characters = (charactersSAXFunc)sax_characters,
  .startElement = (startElementSAXFunc)sax_start_element,
  .endElement = (endElementSAXFunc)sax_end_element
};

void document_loaded(SoupSession *session,
                     SoupMessage *msg,
                     gpointer ptr)
{
  BrowserBox *bb = ptr;
  BuilderState *bs = bb->builder_state;
  if (bs == NULL || bs->active == FALSE) {
    browser_box_set_status(bb, "Failed to load the document");
    return;
  }
  htmlParseChunk(bs->parser, "", 0, 1);
  gtk_widget_grab_focus(GTK_WIDGET(bs->docbox));
  printf("word cache: %u\n", g_hash_table_size(word_cache));
  browser_box_set_status(bb, "Ready");
}

void got_chunk(SoupMessage *msg,
               SoupBuffer *chunk,
               gpointer ptr)
{
  BrowserBox *bb = ptr;
  BuilderState *bs = bb->builder_state;
  browser_box_set_status(bb, "Loading");
  if (bs->parser == NULL) {
    /* todo: maybe move it into got_headers */
    char *uri_str = soup_uri_to_string(bs->uri, FALSE);
    bs->parser =
      htmlCreatePushParserCtxt(&sax, bb, "", 0, uri_str,
                               XML_CHAR_ENCODING_UTF8);
    free(uri_str);
    bs->docbox = document_box_new();
    gtk_container_add (GTK_CONTAINER (bs->root), GTK_WIDGET (bs->docbox));
    bs->vbox = block_box_new(10);
    bs->stack->data = bs->vbox;
    gtk_container_add(GTK_CONTAINER (DOCUMENT_BOX(bs->docbox)->evbox),
                      GTK_WIDGET (bs->vbox));
    g_signal_connect (bs->docbox, "follow", G_CALLBACK(follow_link_cb), bb);
    g_signal_connect (bs->docbox, "hover", G_CALLBACK(hover_link_cb), bb);
    g_signal_connect (bs->docbox, "select", G_CALLBACK(select_text_cb), bb);
    gtk_widget_show_all(GTK_WIDGET(bs->docbox));
    gtk_box_set_child_packing(GTK_BOX(bs->root), GTK_WIDGET(bs->docbox),
                              TRUE, TRUE, 0, GTK_PACK_END);
  }
  if (bs->active) {
    htmlParseChunk(bs->parser, chunk->data, chunk->length, 0);
  }
  return;
}

void got_headers(SoupMessage *msg, gpointer ptr)
{
  BrowserBox *bb = ptr;
  browser_box_set_status(bb, "Got headers");
  /* todo: check content type, don't assume HTML */
  if (bb->builder_state != NULL) {
    if (bb->builder_state->docbox != NULL) {
      gtk_widget_destroy(GTK_WIDGET(bb->builder_state->docbox));
    }
    g_object_unref(bb->builder_state);
  }
  bb->builder_state = builder_state_new(bb->docbox_root);
  bb->builder_state->uri = soup_uri_copy(soup_message_get_uri(msg));
  char *uri_str = soup_uri_to_string(bb->builder_state->uri, FALSE);
  gtk_entry_set_text(GTK_ENTRY(bb->address_bar), uri_str);
  free(uri_str);
}

void document_request_sm (BrowserBox *bb, SoupMessage *sm)
{
  browser_box_set_status(bb, "Requesting");
  if (bb->builder_state != NULL) {
    bb->builder_state->active = FALSE;
  }
  soup_session_abort(bb->soup_session);
  g_signal_connect (sm, "got-chunk", (GCallback)got_chunk, bb);
  g_signal_connect (sm, "got-headers", (GCallback)got_headers, bb);
  soup_session_queue_message(bb->soup_session, sm,
                             (SoupSessionCallback)document_loaded, bb);
}

void document_request (BrowserBox *bb, SoupURI *uri)
{
  SoupMessage *sm = soup_message_new_from_uri("GET", uri);
  document_request_sm(bb, sm);
}


void address_bar_activate (GtkEntry *ab, BrowserBox *bb)
{
  SoupURI *uri = soup_uri_new(gtk_entry_get_text(ab));
  if (uri) {
    history_add(bb, uri);
    document_request(bb, uri);
  }
}



static void browser_box_dispose (GObject *object) {
  BrowserBox *bb = BROWSER_BOX(object);
  GList *form_iter;
  if (bb->forms != NULL) {
    for (form_iter = bb->forms; form_iter; form_iter = form_iter->next) {
      Form *form = form_iter->data;
      if (form->method != NULL) {
        free(form->method);
        form->method = NULL;
      }
      if (form->action != NULL) {
        soup_uri_free(form->action);
        form->action = NULL;
      }
      if (form->fields != NULL) {
        GList *field_iter;
        for (field_iter = form->fields; field_iter; field_iter = field_iter->next) {
          FormField *field = field_iter->data;
          if (field->name != NULL) {
            free(field->name);
            field->name = NULL;
          }
          free(field);
        }
        g_list_free(form->fields);
        form->fields = NULL;
      }
      free(form);
    }
    g_list_free(bb->forms);
    bb->forms = NULL;
  }
  if (bb->history != NULL) {
    g_list_free_full(bb->history, (GDestroyNotify)soup_uri_free);
    bb->history = NULL;
    bb->history_position = NULL;
  }
  G_OBJECT_CLASS (browser_box_parent_class)->dispose(object);
}

static void
browser_box_class_init (BrowserBoxClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  object_class->dispose = browser_box_dispose;
  return;
}

static void
browser_box_init (BrowserBox *bb)
{
  bb->builder_state = NULL;
  bb->forms = NULL;
  bb->history = NULL;
  bb->history_position = NULL;
  bb->search_string[0] = 0;
  return;
}

void document_request (BrowserBox *bb, SoupURI *uri);


BrowserBox *browser_box_new (gchar *uri_str)
{
  BrowserBox *bb = BROWSER_BOX(g_object_new(browser_box_get_type(),
                                        "orientation", GTK_ORIENTATION_VERTICAL,
                                        "spacing", 0,
                                        NULL));
  bb->address_bar = gtk_entry_new();
  gtk_container_add (GTK_CONTAINER(bb), bb->address_bar);
  g_signal_connect(bb->address_bar, "activate",
                   (GCallback)address_bar_activate, bb);

  bb->docbox_root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add (GTK_CONTAINER(bb), bb->docbox_root);
  gtk_box_set_child_packing(GTK_BOX(bb), bb->docbox_root, TRUE, TRUE,
                            0, GTK_PACK_START);
  gtk_container_set_border_width(GTK_CONTAINER(bb->docbox_root), 4);

  bb->status_bar = gtk_statusbar_new();
  gtk_container_add (GTK_CONTAINER(bb), bb->status_bar);

  bb->soup_session =
    soup_session_new_with_options("user-agent", "WWWLite/0.0.0", NULL);

  /* bb->word_cache = g_hash_table_new((GHashFunc)wck_hash, (GEqualFunc)wck_equal); */

  if (uri_str) {
    SoupURI *uri = soup_uri_new(uri_str);
    history_add(bb, uri);
    document_request(bb, soup_uri_new(uri_str));
  }

  return bb;
}
