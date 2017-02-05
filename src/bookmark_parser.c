#include "osso_bookmark_parser.h"

#include <libgnomevfs/gnome-vfs.h>
#include <gio/gio.h>
#include <glib/gprintf.h>
#include <gconf/gconf-client.h>

#include <unistd.h>
#include <string.h>
#include <libintl.h>
#include <ctype.h>

#ifdef BOOKMARK_PARSER_TEST
#define TEST(fun) __##fun
#else
#define TEST(fun) fun
#endif

BookmarkItem *
create_bookmark_new(void)
{
  return g_new0(BookmarkItem, 1);
}

static const char *bookmark_template =
"<?xml version=\"1.0\"?>"
"<!DOCTYPE xbel PUBLIC \"+//IDN python.org//DTD XML Bookmark Exchange Language 1.0//EN//XML\" \"http://www.python.org/topics/xml/dtds/xbel-1.0.dtd\">"
"<xbel version=\"1.0\">"
 "<info>"
  "<metadata>"
   "<default_folder>yes</default_folder>"
  "</metadata>"
 "</info>"
 "<title>My bookmarks</title>"
 "<info>"
  "<metadata>"
   "<time_visited>%d</time_visited>"
   "<time_added>%d</time_added>"
  "</metadata>"
 "</info>"
"</xbel>";

static gchar *
file_path_with_home_dir(const gchar *file_name)
{
  const gchar *home_dir;
  gchar *rv;

  home_dir = g_getenv("HOME");

  if (!home_dir)
    home_dir = g_get_home_dir();

  if (file_name && *file_name)
  {
    if (*file_name == '/')
      rv = g_strconcat(home_dir, file_name, NULL);
    else
      rv = g_strconcat(home_dir, "/", file_name, NULL);
  }
  else
    rv = g_strdup(home_dir);

  return rv;
}

static xmlNode *
get_attribute_pointer(xmlNode *node, const char *attribute)
{
  if (!node)
    return NULL;

  do
  {
    if (node->type == XML_ELEMENT_NODE &&
        !xmlStrcmp(node->name, BAD_CAST attribute))
      break;

    node = node->next;
  }
  while (node);

  return node;
}

static void
free_bookmark_item(BookmarkItem *bm_item)
{
  if (!bm_item)
    return;

  if (bm_item->name)
  {
    g_free(bm_item->name);
    bm_item->name = NULL;
  }

  if (bm_item->url)
  {
    g_free(bm_item->url);
    bm_item->url = NULL;
  }

  if (bm_item->favicon_file)
  {
    g_free(bm_item->favicon_file);
    bm_item->favicon_file = NULL;
  }

  if (bm_item->thumbnail_file)
  {
    g_free(bm_item->thumbnail_file);
    bm_item->thumbnail_file = NULL;
  }

  if (bm_item->list)
  {
    g_slist_foreach(bm_item->list, (GFunc)free_bookmark_item, NULL);
    g_slist_free(bm_item->list);
    bm_item->list = NULL;
  }

  g_free(bm_item);
}

static const gchar *
bookmark_string_strcasestr(const gchar *s1, const gchar *s2)
{
  gchar *s1_dup;
  gchar *s1_down;
  gchar *s2_dup;
  gchar *s2_down;
  gchar *p;

  s1_dup = g_strdup(s1);
  s2_dup = g_strdup(s2);

  if (s1_dup)
    s1_down = g_utf8_strdown(s1_dup, -1);
  else
    s1_down = NULL;

  if ( s2_dup )
    s2_down = g_utf8_strdown(s2_dup, -1);
  else
    s2_down = NULL;

  p = g_strrstr(s1_down, s2_down);

  g_free(s1_dup);
  g_free(s2_dup);
  g_free(s1_down);
  g_free(s2_down);

  if (p)
    return &s1[p - s1_down];

  return NULL;
}

/* fmg: TODO: Optimize me !!! */
static gchar *
bookmark_read_line_from_html_file(GInputStream *in)
{
  gchar buf[2] = {0, };
  gchar *line = g_strdup("");

  while (g_input_stream_read(in, buf, 1, NULL, NULL) > 0)
  {
    gchar *s;

    s = g_strconcat(line, buf, NULL);
    g_free(line);
    line = s;

    if (*buf == '\n')
      break;
  }

  if (*line)
    return line;

  g_free(line);

  return NULL;
}

static void
assign_node_text_contents(gchar **s, xmlNode *node)
{
  xmlChar *xs = xmlNodeGetContent(node);

  if (xs)
  {
    *s = g_strdup((gchar *)xs);
    xmlFree(xs);
  }
  else
    *s = NULL;
}

static void
assign_text_attribute(gchar **s, xmlNode *node, const char* attr)
{
  xmlChar *xs = xmlGetProp(node, BAD_CAST attr);

  if (xs)
  {
    *s = g_strdup((gchar *)xs);
    xmlFree(xs);
  }
  else
    *s = NULL;
}

static long int
get_node_int_contents(xmlNode *node)
{
  xmlChar *xs;
  long int rv;

  xs = xmlNodeGetContent(node);
  rv = strtol((const char *)xs, NULL, 10);
  xmlFree(xs);

  return rv;
}

static inline gboolean
node_name_is(const xmlNode *node, const char *name)
{
  return !xmlStrcmp(node->name, BAD_CAST name);
}

static void
assign_node_metadata(BookmarkItem *bm_item, const xmlNode *node)
{
  xmlNode *n = get_attribute_pointer(node->children, "metadata");

  for (n = n->children; n; n = n->next)
  {
    while (n && n->type != XML_ELEMENT_NODE)
      n = n->next;

    if (!n)
      break;

    if (node_name_is(n, "time_visited"))
      bm_item->time_last_visited = get_node_int_contents(n);
    else if (node_name_is(n, "time_added"))
      bm_item->time_added = get_node_int_contents(n);
    else if (node_name_is(n, "operator_bookmark"))
      bm_item->isOperatorBookmark = get_node_int_contents(n);
    else if (node_name_is(n, "deleted"))
      bm_item->isDeleted = get_node_int_contents(n);
    else if (node_name_is(n, "visit_count"))
      bm_item->visit_count = get_node_int_contents(n);
  }
}

static BookmarkItem *
print_root_names(xmlNode *node)
{
  BookmarkItem *bm_item;

  if (!node)
    return NULL;

  bm_item = create_bookmark_new();

  for (node = node->children; node; node = node->next)
  {
    if (!xmlStrcmp(node->name, BAD_CAST "title"))
    {
      assign_node_text_contents(&bm_item->name, node);
    }
    else if (node_name_is(node,"info"))
    {
      assign_node_metadata(bm_item, node);
    }
    else if (node_name_is(node, "bookmark"))
    {
      BookmarkItem *bm_bookmark = print_root_names(node);
      BookmarkItem *bm_parent;
      gchar *name;

      assign_text_attribute(&bm_bookmark->url, node, "href");
      assign_text_attribute(&bm_bookmark->thumbnail_file, node, "thumbnail");
      assign_text_attribute(&bm_bookmark->favicon_file, node, "favicon");
      bm_bookmark->parent = bm_item;

      bm_item->list = g_slist_append(bm_item->list, bm_bookmark);

      for (bm_parent = bm_item; bm_parent; bm_parent = bm_parent->parent)
      {
        if (bm_parent->visit_count < bm_bookmark->visit_count)
          bm_parent->visit_count = bm_bookmark->visit_count;
      }

      name = bm_bookmark->name;
      bm_bookmark->name = g_strdup_printf("%s.%s", name, "bm");
      g_free(name);
    }
    else if (node_name_is(node, "folder"))
    {
      BookmarkItem *bm_folder = print_root_names(node);

      bm_folder->isFolder = TRUE;
      bm_folder->parent = bm_item;
      bm_item->list = g_slist_append(bm_item->list, bm_folder);
    }
  }

  return bm_item;
}

gboolean
TEST(get_root_bookmark_absolute_path)(BookmarkItem **bookmark_root,
                                      gchar *file_name)
{
  xmlDoc *doc;
  xmlNode *node;
  BookmarkItem *bm_item;

  doc = xmlReadFile(file_name, 0, XML_PARSE_SAX1 | XML_PARSE_RECOVER);

  if (!doc)
    return FALSE;

  node = xmlDocGetRootElement(doc);

  if (!node || !bookmark_root)
  {
    xmlFreeDoc(doc);
    return FALSE;
  }

  free_bookmark_item(*bookmark_root);
  *bookmark_root = NULL;

  bm_item = print_root_names(node);
  *bookmark_root = bm_item;

  xmlFreeDoc(doc);

  if (bm_item)
  {
    bm_item->isFolder = 1;
    return TRUE;
  }

  return FALSE;
}

static gboolean
_get_root_bookmark(BookmarkItem **bookmark_root, gchar *file_name)
{
  gboolean rv;

  gchar *bm_file = file_path_with_home_dir(file_name);

  rv = TEST(get_root_bookmark_absolute_path)(bookmark_root, bm_file);

  g_free(bm_file);

  return rv;
}

gboolean
TEST(get_root_bookmark) (BookmarkItem **bookmark_root,
                         gchar *file_name)
{
  (void)file_name;

  return _get_root_bookmark(bookmark_root, MYBOOKMARKS);
}

gboolean
create_bookmarks_backup(const gchar *file_name)
{
  gchar *bookmark_file;
  gchar *cmd;
  gchar *backup_file;

  (void)file_name;

  bookmark_file = file_path_with_home_dir(MYBOOKMARKS);
  backup_file = g_strdup_printf("%s.backup", bookmark_file);

  if (access(bookmark_file, R_OK))
    return FALSE;

  cmd = g_strdup_printf("cp %s %s", bookmark_file, backup_file);
  g_free(backup_file);
  g_free(bookmark_file);

  system(cmd);
  g_free(cmd);

  return TRUE;
}

static gboolean
create_empty_bookmark_template(char *file_name)
{
  FILE *fp;
  gchar *bm_template;
  time_t t;

  fp = fopen(file_name, "w");
  if (!fp)
    return FALSE;

  t = time(0);
  bm_template = g_strdup_printf(bookmark_template, t, t);
  fputs(bm_template, fp);
  g_free(bm_template);
  fclose(fp);

  return TRUE;
}

gboolean
get_bookmark_from_backup (BookmarkItem **bookmark_root,
                          const gchar *file_name_unused)
{
  gchar *file_name;
  gchar *backup_file_name;
  FILE *fp;
  gboolean rv;
  gsize length = 0;
  gchar *contents = NULL;

  (void)file_name_unused;

  file_name = file_path_with_home_dir(MYBOOKMARKS);
  backup_file_name = g_strdup_printf("%s.backup", file_name);

  if (g_file_get_contents(backup_file_name, &contents, &length, NULL))
  {
    if (contents)
    {
      fp = fopen(file_name, "w");
      if (fp)
      {
        fputs(contents, fp);
        fclose(fp);
      }
    }

    g_free(contents);
    contents = NULL;
  }

  g_free(backup_file_name);

  if (!_get_root_bookmark(bookmark_root, MYBOOKMARKS) &&
      create_empty_bookmark_template(file_name) )
  {
    rv = _get_root_bookmark(bookmark_root, MYBOOKMARKS);
  }
  else
    rv = TRUE;

  g_free(file_name);

  return rv;
}

static gboolean
find_bookmarks_line(GInputStream *in)
{
  gchar *line;
  int i = 0;
  gboolean rv = FALSE;

  do
  {
    line = bookmark_read_line_from_html_file(in);

    if (!line)
      break;

    if (strstr(line, "Bookmarks"))
    {
      rv = TRUE;
      g_free(line);
      break;
    }

    g_free(line);
    i++;
  }
  while (i != 10);

  return rv;
}

BookmarkItem *
bookmarks_new_bookmark(gboolean isFolder, const gchar *name, const gchar *url,
                       gboolean isOperatorBookmark)
{
  BookmarkItem *bm_item = create_bookmark_new();
  time_t tick;

  if (isFolder)
  {
    bm_item->isFolder = FALSE;
    bm_item->name = g_strdup_printf("%s.%s", name, "bm");
    bm_item->url = g_strdup(url);
  }
  else
  {
    bm_item->isFolder = TRUE;
    bm_item->name = g_strdup(name);
    bm_item->url = NULL;
  }

  bm_item->list = NULL;
  bm_item->parent = NULL;
  bm_item->favicon_file = NULL;
  bm_item->thumbnail_file = NULL;
  tick = time(0);
  bm_item->visit_count = 0;
  bm_item->isOperatorBookmark = isOperatorBookmark;
  bm_item->time_last_visited = tick;
  bm_item->time_added = tick;

  return bm_item;
}

static GTime
ns_get_bookmark_date(const char *line, const char *search)
{
  const char *found = bookmark_string_strcasestr(line, search);

  if (!found)
    return 0;

  return strtol(found + strlen(search) + 1, NULL, 10);
}

/**
  * This function replaces some weird elements
  * like &amp; &le;, etc..
  * More info : http://www.w3.org/TR/html4/charset.html#h-5.3.2
  * NOTE : We don't support &#D or &#xH.
  * Patch courtesy of Almer S. Tigelaar <almer1@dds.nl>
  */
static char *
ns_parse_bookmark_item(GString *string)
{
  char *iterator, *temp;
  int cnt = 0;
  GString *result = g_string_new(NULL);

  g_return_val_if_fail(string != NULL, NULL);
  g_return_val_if_fail(string->str != NULL, NULL);

  iterator = string->str;

  for (cnt = 0, iterator = string->str; cnt <= (int)(strlen(string->str));
       cnt++, iterator++)
  {
    if (*iterator == '&')
    {
      int jump = 0;
      int i;

      if (g_strncasecmp(iterator, "&amp;", 5) == 0)
      {
        g_string_append_c(result, '&');
        jump = 5;
      }
      else if (g_strncasecmp(iterator, "&lt;", 4) == 0)
      {
        g_string_append_c(result, '<');
        jump = 4;
      }
      else if (g_strncasecmp (iterator, "&gt;", 4) == 0)
      {

        g_string_append_c(result, '>');
        jump = 4;
      }
      else if (g_strncasecmp (iterator, "&quot;", 6) == 0)
      {

        g_string_append_c(result, '\"');
        jump = 6;
      }
      else if (g_strncasecmp (iterator, "&#39;", 5) == 0)
      {

        g_string_append_c(result, '\'');
        jump = 5;
      }
      else
      {
        /* It must be some numeric thing now */

        iterator++;

        if (iterator && *iterator == '#')
        {
          int val;
          char *num, *tmp;

          iterator++;

          val = atoi (iterator);

          tmp = g_strdup_printf("%d", val);
          jump = strlen (tmp);
          g_free (tmp);

          num = g_strdup_printf("%c", (char) val);
          g_string_append (result, num);
          g_free (num);
        }
      }
      for (i = jump - 1; i > 0; i--)
      {
        iterator++;

        if (iterator == NULL)
          break;
      }
    }
    else
      g_string_append_c(result, *iterator);
  }

  temp = result->str;
  g_string_free (result, FALSE);

  return temp;
}

static gchar *
convert_iso_string_to_utf8(const gchar *s)
{
  gchar *rv;
  gsize bytes = 0;
  GError *error = NULL;

  if (g_utf8_validate(s, -1, 0))
    return g_strdup(s);

  rv = g_convert(s, strlen(s), "UTF-8", "windows-1252", &bytes, 0, &error);

  if (error)
  {
    g_error_free(error);
    error = NULL;

    rv = g_convert(s, strlen(s), "UTF-8", "ISO-8859-1", &bytes, 0, &error);

    if (error)
    {
      g_error_free(error);
      return g_strdup(s);
    }
  }

  return rv;
}

BookmarkItem *
netscape_import_bookmarks(const gchar *path, gboolean use_locale,
                          gchar *importFolderName)
{
  gchar *line;
  const gchar *found;
  gchar *unescaped;
  gchar *converted;
  BookmarkItem *bm_item;
  GString *name;
  GString *url;
  GString *nick;
  BookmarkItem *bm;
  GFile *f ;
  GFileInputStream *in;

  /*
   * It seems Nokia took that code from galeon, where original declaration was:
   *
   * BookmarkItem *netscape_import_bookmarks (const gchar *filename,
   *                                          gboolean use_locale);
   *
   * for some reason they decided to just add a new parameter, while keeping
   * use_locale unused
   */
  (void)use_locale;

  name = g_string_new(0);
  url = g_string_new(0);
  nick = g_string_new(0);

  f = g_file_new_for_path(path);
  in = g_file_read(f, NULL, NULL);
  if (!in)
  {
    g_object_unref(f);
    return FALSE;
  }

  bm_item = bookmarks_new_bookmark(0, importFolderName, 0, 0);

  while (1)
  {
    while (g_main_context_pending(NULL))
      g_main_context_iteration(0, 0);

    line = bookmark_read_line_from_html_file(G_INPUT_STREAM(in));

    if (!line)
      break;

    if ((found = bookmark_string_strcasestr(line, "<A HREF=")))
    {
      g_string_assign(url, found + 9);
      g_string_truncate(url, strchr(url->str, '"') - url->str);

      if ((found = strstr(found + 9 + url->len, "\">")))
      {
        g_string_assign(name, found + 2);
        g_string_truncate(
              name, bookmark_string_strcasestr(name->str, "</A>") - name->str);
        found = bookmark_string_strcasestr(line, "SHORTCUTURL=");

        if (found)
        {
          g_string_assign(nick, found + 13);
          g_string_truncate(nick, strchr(nick->str, '\"') - nick->str);
        }
        else
          g_string_assign(nick, "");

        /* fmg: why is the result ignored? */
        ns_get_bookmark_date(line, "ADD_DATE=");
        ns_get_bookmark_date(line, "LAST_VISIT=");

        unescaped = ns_parse_bookmark_item(name);
        converted = convert_iso_string_to_utf8(unescaped);
        g_free(unescaped);

        bm = bookmarks_new_bookmark(TRUE, converted, url->str, FALSE);
        g_free(converted);

        bm->parent = bm_item;
        bm->isFolder = FALSE;

        if (bm_item)
          bm_item->list = g_slist_append(bm_item->list, bm);
      }
    }
    else if ((found = bookmark_string_strcasestr(line, "<DT><H3")))
    {
      if ((found = strchr(found + 7, '>')))
      {
        g_string_assign(name, found + 1);
        g_string_truncate(
              name,
              bookmark_string_strcasestr(name->str, "</H3>") - name->str);
        ns_get_bookmark_date(line, "ADD_DATE=");

        unescaped = ns_parse_bookmark_item(name);
        converted = convert_iso_string_to_utf8(unescaped);
        g_free(unescaped);

        bm = bookmarks_new_bookmark(0, converted, 0, 0);
        g_free(converted);

        bm_item->list = g_slist_append(bm_item->list, bm);
        bm->isFolder = TRUE;
        bm->parent = bm_item;
        bm_item = bm;
      }
    }
    else if (bookmark_string_strcasestr(line, "</DL>"))
    {
      if (bm_item->parent)
        bm_item = bm_item->parent;
    }
    else if (bookmark_string_strcasestr(line, "<HR>"))
    {
      found = bookmark_string_strcasestr(line, "<DD>");

      if (found)
        g_string_assign(name, found + 4);
      else if (!(strchr(line, '<') || strchr(line, '>')))
        g_string_assign(name, line);
    }

    g_free(line);
  }

  g_input_stream_close(G_INPUT_STREAM(in), NULL, NULL);
  g_object_unref(in);
  g_object_unref(f);
  g_string_free(name, 1);
  g_string_free(url, 1);
  g_string_free(nick, 1);

  return bm_item;
}

gboolean
TEST(bookmark_import)(const gchar *path, gchar *importFolderName,
                      BookmarkItem **import_folder)
{
  gchar *line;
  BookmarkItem *bm_item;
  gboolean is_bookmark_file;
  GFile *f ;
  GFileInputStream *in;

  f = g_file_new_for_path(path);
  in = g_file_read(f, NULL, NULL);

  if (!in)
  {
    g_object_unref(f);
    return FALSE;
  }

  line = bookmark_read_line_from_html_file(G_INPUT_STREAM(in));

  if (line)
  {
    is_bookmark_file = !!strstr(line, "NETSCAPE-Bookmark-file") ||
                       find_bookmarks_line(G_INPUT_STREAM(in));
    g_free(line);
  }
  else
    is_bookmark_file = FALSE;

  g_input_stream_close(G_INPUT_STREAM(in), NULL, NULL);
  g_object_unref(in);
  g_object_unref(f);

  if (!is_bookmark_file)
    return FALSE;

  bm_item = netscape_import_bookmarks(path, TRUE, importFolderName);

  if (bm_item && import_folder)
  {
    *import_folder = bm_item;
    return TRUE;
  }

  return FALSE;
}

gboolean
set_lock(gchar *lock_file_name)
{
  char *path;
  FILE *fp;

  path = file_path_with_home_dir(lock_file_name);
  fp = fopen(path, "w");
  g_free((gpointer)path);

  if (fp)
  {
    fclose(fp);
    return TRUE;
  }

  return FALSE;
}

gboolean
del_lock(gchar *lock_file_name)
{
  gchar *path;

  path = file_path_with_home_dir(lock_file_name);

  if (access(path, R_OK))
  {
    g_free(path);
    return FALSE;
  }

  unlink(path);
  g_free(path);

  return TRUE;
}

gchar *
get_base_url_name(const gchar *url)
{
  gchar *rv;
  char *p;

  if (!url)
    return NULL;

  rv = g_strdup(url);
  p = strstr(rv, "//");

  if (p)
  {
    p = strchr(p + 2, '/');

    if (p)
      *p = 0;
  }

  return rv;
}

void
set_bookmark_files_path(void)
{
  gchar *bm_path;
  gchar *bm_file_path;
  gchar *tn_path;
  gchar *cmd;

  bm_path = file_path_with_home_dir(".bookmarks");

  if (bm_path)
  {

    if (access(bm_path, R_OK))
      mkdir(bm_path, 0755);

    bm_file_path = g_strdup_printf("%s%s", bm_path, "/MyBookmarks.xml");
    tn_path = g_strdup_printf("%s/%s", bm_path, "thumbnails");

    if (access(bm_file_path, R_OK))
    {
      cmd = g_strdup_printf("cp %s%s %s",
                            "/usr/share/bookmark-manager/bookmarks",
                            "/MyBookmarks.xml", bm_path);
      system(cmd);
      g_free(cmd);
    }

    if (!access("/usr/share/bookmark-manager/thumbnails", R_OK))
    {
      if (access(tn_path, R_OK))
      {
        mkdir(bm_path, 0755);
        cmd = g_strdup_printf("cp %s/* %s",
                              "/usr/share/bookmark-manager/thumbnails",
                              tn_path);
        system(cmd);
        g_free(cmd);
      }
    }

    g_free(bm_file_path);
    g_free(tn_path);
    g_free(bm_path);
  }
}

static void
osso_bookmark_get_dir_node(const BookmarkItem *bm_item, GSList **folders)
{
  GSList *next;

  if (!bm_item)
    return;

  for (next = bm_item->list; next; next = next->next)
  {
    const BookmarkItem *bm;

    while (!((const BookmarkItem *)next->data)->isFolder)
    {
      next = next->next;

      if (!next)
        return;
    }

    bm = (const BookmarkItem *)next->data;
    *folders =  g_slist_append(*folders, g_strdup_printf("USER:%s", bm->name));
  }
}

GSList *
osso_bookmark_get_folders_list(void)
{
  GSList *folders;
  BookmarkItem *bm_item;

  set_bookmark_files_path();
  bm_item = create_bookmark_new();
  _get_root_bookmark(&bm_item, "/.bookmarks/MyBookmarks.xml");
  folders = g_slist_append(NULL,
                           g_strdup_printf("MY:%s",
                                           dgettext("osso-browser-ui",
                                                    "webb_folder_root_user")));
  osso_bookmark_get_dir_node(bm_item, &folders);

  free_bookmark_item (bm_item);

  return folders;
}

GSList *
get_complete_path(BookmarkItem *parentItem)
{
  GSList *rv;

  rv = g_slist_append(0, parentItem->name);

  for (parentItem = parentItem->parent; parentItem;
       parentItem = parentItem->parent)
  {
    rv = g_slist_append(rv, parentItem->name);
  }

  return rv;
}

static
gchar *escape_bookmark_str(const char *s)
{
  GString *str;
  gchar *rv;

  if (!s)
    return NULL;

  str = g_string_sized_new(2 * strlen(s));

  while(*s)
  {
    switch (*s)
    {
      case '>':
        g_string_append(str, "&gt;");
        break;
      case '<':
        g_string_append(str, "&lt;");
        break;
      case '&':
        g_string_append(str, "&amp;");
        break;
      case '"':
        g_string_append(str, "&quot;");
        break;
      case '\r':
        g_string_append(str, "&#13;");
        break;
      default:
        g_string_append_c(str, *s);
        break;
    }

    s++;
  }

  rv = str->str;
  g_string_free(str, FALSE);

  return rv;
}

#define bm_gio_write_string(out, string) \
  g_output_stream_write(out, string, sizeof(string) - 1, NULL, NULL); \

static void
netscape_export_bookmarks_item(GOutputStream *out, BookmarkItem *bm_item,
                               const gchar *bm_parent_name)
{
  if (!out || !bm_item)
  {
    g_print(" %s\n", "\nInvalid Input Parameter");
    return;
  }

  if (!bm_item->isFolder)
  {
    gchar *url = bm_item->url;
    gchar *name, *name_escaped;

    bm_gio_write_string(out, "\t<DT><A HREF=\"");

    while (isalnum(*url))
      url++;

    if (*url != ':')
    {
      if (!g_str_has_prefix(bm_item->url, "/"))
        bm_gio_write_string(out, "http://");
    }

    g_output_stream_write(out, bm_item->url, strlen(bm_item->url), NULL, NULL);

    bm_gio_write_string(out, "\"");

    if (bm_item->time_added > 0)
    {
      gchar *s = g_strdup_printf(" ADD_DATE=\"%d\"", bm_item->time_added);

      g_output_stream_write(out, s, strlen(s), NULL, NULL);
      g_free(s);
    }

    if ( bm_item->time_last_visited > 0 )
    {
      gchar *s = g_strdup_printf(" LAST_VISIT=\"%d\"",
                                 bm_item->time_last_visited);

      g_output_stream_write(out, s, strlen(s), NULL, NULL);
      g_free(s);
    }

    bm_gio_write_string(out, ">");
    name = g_strndup(bm_item->name, strlen(bm_item->name) - 3);
    name_escaped = escape_bookmark_str(name);
    g_output_stream_write(out, name_escaped, strlen(name_escaped), NULL, NULL);
    bm_gio_write_string(out, "</A>\n");

    g_free(name);
    g_free(name_escaped);
  }
  else
  {
    gchar *name;
    GSList *list;

    bm_gio_write_string(out, "<DT><H3 ADD_DATE=\"0\">");

    if (bm_parent_name)
      name = g_strdup(bm_parent_name);
    else
      name = escape_bookmark_str(bm_item->name);

    g_output_stream_write(out, name, strlen(name), NULL, NULL);
    g_free(name);

    bm_gio_write_string(out, "</H3>\n<DL><p>\n");

    for (list = bm_item->list; list; list = list->next)
    {
      while (g_main_context_iteration(0, 0));
      netscape_export_bookmarks_item(out, (BookmarkItem *)list->data, 0);
    }

    bm_gio_write_string(out, "</DL><p>\n");
  }
}

gboolean
__netscape_export_bookmarks(const gchar *filename, GSList *root,
                          const gchar *bm_parent_name)
{
  GFile *file ;
  GFileOutputStream *out;
  #define bm_export_header \
    "<!DOCTYPE NETSCAPE-Bookmark-file-1>\n" \
    "<!-- This is an automatically generated file.\n" \
    "It will be read and overwritten.\n" \
    "Do Not Edit! -->\n" \
    "<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\n" \
    "<TITLE>Bookmarks</TITLE>\n" \
    "<H1>Bookmarks</H1>\n\n" \
    "<DL><p>\n"

  if (!filename)
  {
    g_print(" %s\n", "\nInvalid Input Parameter");
    return FALSE;
  }

  file = g_file_new_for_path(filename);
  out = g_file_replace(file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, NULL);

  if (!out)
  {
    g_object_unref(file);
    return FALSE;
  }

  bm_gio_write_string(G_OUTPUT_STREAM(out), bm_export_header);

  while(root)
  {
    netscape_export_bookmarks_item(G_OUTPUT_STREAM(out),
                                   (BookmarkItem *)root->data, bm_parent_name);
    root = root->next;
  }

  bm_gio_write_string(G_OUTPUT_STREAM(out), "</DL><p>\n");

  g_output_stream_close(G_OUTPUT_STREAM(out), NULL, NULL);
  g_object_unref(out);

  return TRUE;
}

static gboolean
dump_xml_doc_and_fsync(xmlDoc *doc, const char *file_path)
{
  FILE *fp;
  gboolean rv;

  g_return_val_if_fail(doc, FALSE);
  g_return_val_if_fail(file_path, FALSE);

  fp = fopen(file_path, "w");
  g_return_val_if_fail(fp, FALSE);

  if (!xmlDocFormatDump(fp, doc, 1) == -1)
  {
    fsync(fileno(fp));
    rv = TRUE;
  }
  else
    rv = FALSE;

  fclose(fp);

  return rv;
}

static xmlNode *
get_node_by_tag(xmlNode *node, const char *tag)
{
  if (xmlStrcmp(node->name, BAD_CAST tag))
  {
    while (xmlStrcmp(node->name, BAD_CAST "info"))
    {
      node = node->next;

      if (!node)
        return NULL;
    }

    node = node->children;

    if (xmlStrcmp(node->name, BAD_CAST tag))
    {
      while (xmlStrcmp(node->name, BAD_CAST "metadata"))
      {
        node = node->next;

        if (!node)
          return NULL;
      }

      node = node->children;

      while (node->type != XML_ELEMENT_NODE)
      {
        node = node->next;

        if (!node)
          return NULL;
      }

again:
      if (xmlStrcmp(node->name, BAD_CAST tag))
      {
        while (1)
        {
          node = node->next;

          if (!node)
            break;

          if (node->type == XML_ELEMENT_NODE)
            goto again;
        }

        node = NULL;
      }
    }
  }

  return node;
}

gboolean nodeptriter = FALSE;

static xmlNode *
create_new_xmlnode(xmlNode *parent_node, BookmarkItem *bm_item)
{
  xmlNode *node;
  xmlNode *info;
  xmlNode *metadata;
  gchar *visit_count;
  gchar *last_visited;
  gchar *added;

  if (!bm_item || !parent_node)
    return NULL;

  last_visited = g_strdup_printf("%d", bm_item->time_last_visited);
  added = g_strdup_printf("%d", bm_item->time_added);
  visit_count = g_strdup_printf("%d", bm_item->visit_count);

  xmlAddChild(parent_node, xmlNewText(BAD_CAST "\n"));

  if (bm_item->isFolder)
  {
    node = xmlNewChild(parent_node, NULL, BAD_CAST "folder", NULL);
    xmlSetProp(node, BAD_CAST "folded", BAD_CAST "no");
    xmlAddChild(node, xmlNewText(BAD_CAST "\n"));
    xmlNewTextChild(node, NULL, BAD_CAST "title", BAD_CAST bm_item->name);
  }
  else
  {
    gchar *s;

    node = xmlNewChild(parent_node, NULL, BAD_CAST "bookmark", NULL);
    xmlSetProp(node, BAD_CAST "href", BAD_CAST bm_item->url);
    xmlSetProp(node, BAD_CAST "favicon", BAD_CAST bm_item->favicon_file);
    xmlSetProp(node, BAD_CAST "thumbnail", BAD_CAST bm_item->thumbnail_file);
    xmlAddChild(node, xmlNewText(BAD_CAST "\n"));

    s = g_strndup(bm_item->name, strlen(bm_item->name) - 3);
    xmlNewTextChild(node, NULL, BAD_CAST "title", BAD_CAST s);
    g_free(s);
  }

  info = xmlNewChild(node, NULL, BAD_CAST "info", NULL);
  metadata = xmlNewChild(info, NULL, BAD_CAST "metadata", NULL);
  xmlNewChild(metadata, NULL, BAD_CAST "time_visited", BAD_CAST last_visited);
  xmlNewChild(metadata, NULL, BAD_CAST "time_added", BAD_CAST added);
  xmlNewChild(metadata, NULL, BAD_CAST "visit_count", BAD_CAST visit_count);

  if (bm_item->isOperatorBookmark)
  {
    gchar *s = g_strdup_printf("%d", bm_item->isOperatorBookmark);

    xmlNewChild(info, NULL, BAD_CAST "operator_bookmark", BAD_CAST s);
    g_free(s);

    s = g_strdup_printf("%d", bm_item->isDeleted);
    xmlNewChild(info, NULL, BAD_CAST "deleted", BAD_CAST s);
    g_free(s);
  }

  g_free(added);
  g_free(last_visited);
  g_free(visit_count);

  return node;
}

static void
add_xmlnode_to_parent(BookmarkItem *parent_item, xmlNode *parent_node)
{
  xmlNode *node;
  GSList *list;

  CHECK_PARAM(!parent_item || !parent_node,
              "\nInvalid Input Parameter", return);

  node = create_new_xmlnode(parent_node, parent_item);

  for (list = parent_item->list; list; list = list->next)
  {
    BookmarkItem *bm_item;

    while (1)
    {
      bm_item = list->data;

      if (bm_item->isFolder)
        break;

      create_new_xmlnode(node, bm_item);
      list = list->next;

      if (!list)
        return;
    }

    add_xmlnode_to_parent(bm_item, node);
  }
}

static xmlNode *
add_bookmark_item(const BookmarkItem *bm_item)
{
  xmlNode *item;
  xmlNode *metadata;
  gchar *last_visited;
  gchar *added;
  int list_len;
  GSList *list;

  CHECK_PARAM(!bm_item, "\nInvalid Input Parameter", return NULL);

  last_visited = g_strdup_printf("%d", bm_item->time_last_visited);
  added = g_strdup_printf("%d", bm_item->time_added);

  if (bm_item->isFolder)
  {
    xmlNode *info;

    item = xmlNewNode(NULL, BAD_CAST "folder");
    xmlAddChild(item, xmlNewText(BAD_CAST "\n"));
    xmlSetProp(item, BAD_CAST "folded", BAD_CAST "no");
    xmlNewTextChild(item, NULL, BAD_CAST "title",
                    BAD_CAST bm_item->name);
    info = xmlNewChild(item, NULL, BAD_CAST "info", NULL);
    xmlAddChild(info, xmlNewText(BAD_CAST "\n"));
    metadata = xmlNewChild(info, NULL, BAD_CAST "metadata", 0);
    xmlNewChild(metadata, NULL, BAD_CAST "time_visited", BAD_CAST last_visited);
    xmlNewChild(metadata, NULL, BAD_CAST "time_added", BAD_CAST added);
  }
  else
  {
    gchar *s;

    item = xmlNewNode(NULL, BAD_CAST "bookmark");
    xmlAddChild(item, xmlNewText(BAD_CAST "\n"));
    xmlSetProp(item, BAD_CAST "href", BAD_CAST bm_item->url);
    xmlSetProp(item, BAD_CAST "favicon",
               BAD_CAST bm_item->favicon_file);
    xmlSetProp(item, BAD_CAST "thumbnail",
               BAD_CAST bm_item->thumbnail_file);

    s = g_strndup(bm_item->name, strlen(bm_item->name) - 3);
    xmlNewTextChild(item, NULL, BAD_CAST "title", BAD_CAST s);
    g_free(s);

    metadata =
        xmlNewChild(xmlNewChild(item, NULL, BAD_CAST "info", NULL),
                    NULL, BAD_CAST "metadata", NULL);
    xmlNewChild(metadata, NULL, BAD_CAST "time_visited", BAD_CAST last_visited);
    xmlNewChild(metadata, NULL, BAD_CAST "time_added", BAD_CAST added);

    s = g_strdup_printf("%d", bm_item->visit_count);
    xmlNewChild(metadata, NULL, BAD_CAST "visit_count", BAD_CAST s);
    g_free(s);
  }

  g_free(last_visited);
  g_free(added);

  if (bm_item->isOperatorBookmark)
  {
    gchar *s = g_strdup_printf("%d", bm_item->isOperatorBookmark);

    xmlNewChild(metadata, NULL, BAD_CAST "operator_bookmark", BAD_CAST s);
    g_free(s);

    s = g_strdup_printf("%d", bm_item->isDeleted);
    xmlNewChild(metadata, NULL, BAD_CAST "deleted", BAD_CAST s);
    g_free(s);
  }

  list_len = g_slist_length(bm_item->list);
  list = bm_item->list;

  while (list_len--)
  {
    add_xmlnode_to_parent((BookmarkItem *)list->data, item);
    list = list->next;
  }

  return item;
}

static xmlNode *
get_parent_nodeptr(GSList *items_list, xmlNode *node, int list_len)
{
  xmlNode *n;
  gboolean found = FALSE;

  if ( !items_list || !node )
    return NULL;

  if (list_len == 1)
  {
    n = node->children;

    while (n && xmlStrcmp(n->name, BAD_CAST "title"))
      n = n->next;

    return n;
  }

  for (n = node->children; n; n = n->next)
  {
    if (n->type != XML_ELEMENT_NODE)
      continue;

    if (xmlStrcmp(n->name, BAD_CAST "folder"))
    {
      if (!xmlStrcmp(n->name, BAD_CAST "bookmark"))
      {
        xmlChar *title =
            xmlNodeGetContent(get_attribute_pointer(n->children, "title"));

        if (nodeptriter)
        {
          gchar *data = g_strdup(g_slist_nth_data(items_list, nodeptriter));

          if (data)
          {
            size_t len = strlen(data);

            if (len > 2)
              data[len - 3] = 0;

            if (title && !strcmp((const char *)title, data) )
              found = TRUE;

            g_free(data);
          }
        }

        if (title)
          xmlFree(title);

        if (found)
          return n;
      }
    }
    else
    {
      const gchar *data = g_slist_nth_data(items_list, nodeptriter);
      xmlChar *title =
          xmlNodeGetContent(get_attribute_pointer(n->children, "title"));

      if (data && title && !strcmp((const char *)title, data))
      {
        xmlNode *next;

        if (list_len == ++nodeptriter)
          found = TRUE;

        xmlFree(title);
        title = NULL;

        if (found)
          return n;

        next = get_parent_nodeptr(items_list, n, list_len);

        if (next)
          return next;
      }

      if (title)
        xmlFree(title);
    }
  }

  return n;
}

gboolean
bm_engine_add_duplicate_item(BookmarkItem *parent, BookmarkItem *bm_item)
{
  gchar *bm_file;
  xmlDoc *doc;
  GSList *list;
  int len;
  gboolean rv;

  bm_file = file_path_with_home_dir("/.bookmarks/MyBookmarks.xml");
  doc = xmlParseFile(bm_file);

  if (!doc)
  {
    g_free(bm_file);
    return FALSE;
  }

  list = g_slist_reverse(get_complete_path(parent));
  len = g_slist_length(list);
  nodeptriter = 1;
  xmlAddNextSibling(get_parent_nodeptr(list, xmlDocGetRootElement(doc), len),
                    add_bookmark_item(bm_item));

  set_lock("/.bookmarks/.lock");

  g_slist_free(list);
  rv = dump_xml_doc_and_fsync(doc, bm_file);
  g_free(bm_file);
  xmlFreeDoc(doc);

  del_lock("/.bookmarks/.lock");

  return rv;
}

gboolean
bookmark_set_operator_bookmark_as_deleted(BookmarkItem *bm_item,
                                          gchar *file_name, xmlDocPtr doc,
                                          xmlNode *root_element)
{
  GSList *list;
  xmlNode *node;

  (void)file_name;

  CHECK_PARAM(!bm_item, "\nInvalid Input Parameter", return FALSE);

  CHECK_PARAM(!bm_item->isOperatorBookmark, "\nBM Not Operator BM",
              return FALSE);

  if (!doc)
    return FALSE;

  list = g_slist_reverse(get_complete_path(bm_item));

  nodeptriter = 1;
  node = get_parent_nodeptr(list, root_element, g_slist_length(list));

  if (node && (node = get_node_by_tag(node->children, "deleted")))
  {
    xmlNodeSetContent(node, BAD_CAST "1");
    g_slist_free(list);
    return TRUE;
  }

  g_slist_free(list);

  return FALSE;
}

static GConfClient *
osso_bookmark_gconf_init_default_client()
{
  return gconf_client_get_default();
}

SortType osso_bookmark_gconf_get_int(gchar * key)
{
  GConfClient *client;
  GError *err = NULL;

  if (key && (client = osso_bookmark_gconf_init_default_client()))
  {
    int rv = gconf_client_get_int(client, key, &err);

    if (err)
      g_error_free(err);

    return rv;
  }

  return 0;
}

gboolean
osso_bookmark_gconf_set_int(gchar *key, gint val)
{
  GConfClient *client;

  if (key && (client = osso_bookmark_gconf_init_default_client()))
  {
    gconf_client_set_int(client, key, val, NULL);
    return TRUE;
  }

  return FALSE;
}

gboolean
opened_bookmark_remove(BookmarkItem *node, xmlNodePtr root_element)
{
  GSList *list;

  if (!node)
    return FALSE;

  list = g_slist_reverse(get_complete_path(node));
  nodeptriter = 1;

  if (root_element)
  {
    xmlNode *n = get_parent_nodeptr(list, root_element, g_slist_length(list));

    if (n)
    {
      g_slist_free(list);
      xmlUnlinkNode(n);
      xmlFreeNodeList(n);
      return TRUE;
    }
  }

  g_slist_free(list);

  return FALSE;
}

gboolean
bookmark_set_visit_count(BookmarkItem *bm_item, const gchar *val,
                         const gchar *file_name, xmlDocPtr doc,
                         xmlNode *root_element)
{
  GSList *list;
  int list_len;
  xmlNode *node;

  CHECK_PARAM(!bm_item || !val, "\nInvalid Input Parameter", return FALSE);

  (void)file_name;

  if (!doc)
    return FALSE;

  list = g_slist_reverse(get_complete_path(bm_item));
  list_len = g_slist_length(list);
  nodeptriter = 1;

  node = get_parent_nodeptr(list, root_element, list_len);

  if (!node)
    goto err;

  node = get_node_by_tag(node->children, "visit_count");
  if (node)
  {
    xmlNodeSetContent(node, BAD_CAST val);
    g_slist_free(list);
    return TRUE;
  }

  node = get_node_by_tag(node->children, "metadata");

  if (!node)
    goto err;

  node = xmlNewChild(node, NULL, BAD_CAST "visit_count", BAD_CAST val);
  g_slist_free(list);

  return !!node;

err:
  g_slist_free(list);

  return FALSE;
}

gboolean
bookmark_set_name(BookmarkItem *bm_item, const gchar *val, xmlDocPtr doc,
                  xmlNode *root_element)
{
  gboolean rv = FALSE;
  GSList *list;
  xmlNode *node;
  xmlNode *children;

  if (!val || !bm_item)
    return FALSE;

  list = g_slist_reverse(get_complete_path(bm_item));

  nodeptriter = 1;
  node = get_parent_nodeptr(list, root_element, g_slist_length(list));

  if (node && (children = node->children) != 0)
  {
    xmlChar *val_enc;

    while (children && xmlStrcmp(children->name, BAD_CAST "title") )
      children = children->next;

    if (!children)
      goto out;

    if (bm_item->isFolder)
    {
      val_enc = xmlEncodeEntitiesReentrant(doc, BAD_CAST val);
      xmlNodeSetContent(children, val_enc);
    }
    else
    {
      gchar *s = g_strndup(val, strlen(val) - 3);
      val_enc = xmlEncodeEntitiesReentrant(doc, BAD_CAST s);

      xmlNodeSetContent(children, val_enc);
      g_free(s);
    }

    xmlFree(val_enc);

    rv = TRUE;
  }

out:
  g_slist_free(list);

  return rv;
}

gboolean
bookmark_set_url(BookmarkItem *bm_item, const gchar *val, xmlDocPtr doc,
                 xmlNode *root_element)
{
  gboolean rv = FALSE;
  GSList *list;
  xmlNode *node;

  (void)doc;

  if (!bm_item || !val)
    return FALSE;

  CHECK_PARAM(bm_item->isFolder, "\nThe BookmarkItem is a Folder",
              return FALSE);

  list = g_slist_reverse(get_complete_path(bm_item));
  nodeptriter = 1;
  node = get_parent_nodeptr(list, root_element, g_slist_length(list));

  while (node && xmlStrcmp(node->name, BAD_CAST "bookmark"))
    node = node->next;

  if (node)
  {
    xmlChar *new_href, *old_href;
    gchar *new_url, *old_url;

    old_href = xmlGetProp(node, BAD_CAST "href");
    xmlSetProp(node, BAD_CAST "href", BAD_CAST val);
    new_href = xmlGetProp(node, BAD_CAST "href");
    old_url = get_base_url_name((const gchar *)old_href);
    new_url = get_base_url_name((const gchar *)new_href);

    if (strcmp(old_url, new_url))
    {
      xmlSetProp(node, BAD_CAST "favicon", BAD_CAST "");
      xmlSetProp(node, BAD_CAST "thumbnail", BAD_CAST "");
    }

    xmlFree(old_href);
    xmlFree(new_href);
    g_free(old_url);
    g_free(new_url);

    rv = TRUE;
  }

  g_slist_free(list);

  return rv;
}

BMError
bookmark_add_child(BookmarkItem *parent, BookmarkItem *bm_item, gint position,
                   xmlNode *root_element)
{
  GSList *list;

  CHECK_PARAM(!bm_item || !parent || !parent->isFolder,
              "\nInvalid Input Parameter", return BM_INVALID_PARAMETER);

  if (parent->list)
  {
    xmlNode *node;

    if (position == -1)
      position = g_slist_length(parent->list) - 1;

    list = g_slist_reverse(
             get_complete_path(g_slist_nth(parent->list, position)->data));
    nodeptriter = 1;
    node = get_parent_nodeptr(list, root_element, g_slist_length(list));

    if (node)
      xmlAddPrevSibling(node, add_bookmark_item(bm_item));
  }
  else
  {
    guint list_len;
    xmlNode *node, *new_node;

    list = g_slist_reverse(get_complete_path(parent));
    nodeptriter = 1;
    list_len = g_slist_length(list);
    node = get_parent_nodeptr(list, root_element, list_len);
    new_node = add_bookmark_item(bm_item);

    if (list_len == 1)
      xmlAddSibling(node, new_node);
    else
      xmlAddChild(node, new_node);

  }

  g_slist_free(list);

  return BM_OK;
}

gboolean
bm_engine_add_folder(BookmarkItem *parent, BookmarkItem *bm_item,
                     const gchar *file_name)
{
  gchar *bm_file;
  xmlDoc *doc;
  gboolean rv = FALSE;

  (void)parent;
  (void)file_name;

  bm_file = file_path_with_home_dir(MYBOOKMARKS);
  doc = xmlParseFile(bm_file);

  if (doc)
  {
    xmlAddChild(xmlDocGetRootElement(doc), add_bookmark_item(bm_item));
    set_lock("/.bookmarks/.lock");
    rv = dump_xml_doc_and_fsync(doc, bm_file);
    del_lock("/.bookmarks/.lock");
    xmlFreeDoc(doc);
  }

  g_free(bm_file);

  return rv;
}

gboolean
bookmark_remove(BookmarkItem *bm_item, gchar *file_name)
{
  gboolean rv = FALSE;
  gchar *bm_file;
  xmlDoc *doc;

  (void)file_name;

  if (!bm_item)
    return FALSE;

  bm_file = file_path_with_home_dir("/.bookmarks/MyBookmarks.xml");
  doc = xmlReadFile(bm_file, 0, XML_PARSE_SAX1 | XML_PARSE_RECOVER);

  if (doc)
  {
    xmlNode *node = xmlDocGetRootElement(doc);
    GSList *list = g_slist_reverse(get_complete_path(bm_item));

    nodeptriter = 1;

    if (node)
    {
      node = get_parent_nodeptr(list, node, g_slist_length(list));

      if (node)
      {
        xmlUnlinkNode(node);
        xmlFreeNodeList(node);

        set_lock("/.bookmarks/.lock");
        rv = dump_xml_doc_and_fsync(doc, bm_file);
        del_lock("/.bookmarks/.lock");
      }
    }

    g_slist_free(list);
    xmlFreeDoc(doc);
  }

  g_free(bm_file);

  return rv;
}

gboolean
bookmark_remove_list(GSList *item_list)
{
  gchar *bm_file;
  xmlDoc *doc;
  xmlNode *node;
  gboolean rv;

  g_return_val_if_fail("item_list", FALSE);

  bm_file = file_path_with_home_dir("/.bookmarks/MyBookmarks.xml");
  doc = xmlReadFile(bm_file, 0, 513);

  if (!doc)
  {
    g_free(bm_file);
    return FALSE;
  }

  node = xmlDocGetRootElement(doc);
  if (!node)
    goto out;

  while (item_list)
  {
    xmlNode *n;

    if (((BookmarkItem *)item_list->data)->isOperatorBookmark)
    {
      bookmark_set_operator_bookmark_as_deleted(item_list->data, MYBOOKMARKS,
                                                doc, node);
    }
    else
    {
      GSList *list = g_slist_reverse(get_complete_path(item_list->data));

      nodeptriter = 1;
      n = get_parent_nodeptr(list, node, g_slist_length(list));
      g_slist_free(list);

      if (n)
      {
        xmlUnlinkNode(n);
        xmlFreeNodeList(n);
      }
    }

    item_list = item_list->next;
  }

  set_lock("/.bookmarks/.lock");
  rv = dump_xml_doc_and_fsync(doc, bm_file);
  del_lock("/.bookmarks/.lock");

out:
  xmlFreeDoc(doc);
  g_free(bm_file);

  return rv;
}

#ifdef BOOKMARK_PARSER_TEST

#include <assert.h>

void
compare(BookmarkItem *bm1, BookmarkItem *bm2, gboolean compare_times)
{
  if (!bm1->thumbnail_file)
    assert(!bm2->thumbnail_file);
  if (!bm2->thumbnail_file)
    assert(!bm1->thumbnail_file);
  if (bm1->thumbnail_file && bm2->thumbnail_file)
    assert(!strcmp(bm1->thumbnail_file, bm2->thumbnail_file));

  assert (bm1->isFolder == bm2->isFolder);
  assert (bm1->isDeleted == bm2->isDeleted);
  assert (bm1->isOperatorBookmark == bm2->isOperatorBookmark);
  assert (bm1->visit_count == bm2->visit_count);

  if (compare_times)
  {
    assert (bm1->time_added == bm2->time_added);
    assert (bm1->time_last_visited == bm2->time_last_visited);
  }

  if (!bm1->name)
    assert(!bm2->name);
  if (!bm2->name)
    assert(!bm1->name);

  if (bm1->name && bm2->name)
    assert(!strcmp(bm1->name, bm2->name));

  if (!bm1->url)
    assert(!bm2->url);
  if (!bm2->url)
    assert(!bm1->url);

  if (bm1->url && bm2->url)
    assert(!strcmp(bm1->url, bm2->url));

  if (bm1->isFolder)
  {
    GSList *l1 = bm1->list;
    GSList *l2 = bm2->list;

    while(1)
    {
      compare(l1->data, l2->data, compare_times);
      l1 = l1->next;
      l2 = l2->next;

      if (l1)
        assert(l2);

      if (l2)
        assert(l1);

      if (!l1)
        break;
    }
  }
}

int main()
{

  BookmarkItem *bm1 = NULL, *bm2 = NULL;

  if (!gnome_vfs_init()) {
      printf ("Could not initialize GnomeVFS\n");
      return 1;
    }

  /*__bookmark_import("/tmp/bookmarks.html", NULL, &bm1);
  bookmark_import("/tmp/bookmarks.html", NULL, &bm2);

  compare(bm1, bm2, FALSE);*/

  __get_root_bookmark(&bm1, MYBOOKMARKS);
  get_root_bookmark(&bm2, MYBOOKMARKS);

  compare(bm1, bm2, TRUE);
/*
  GSList *l = osso_bookmark_get_folders_list();
*/

  //__netscape_export_bookmarks("/tmp/export2.html", bm1->list, "PARENT_NAME");

  return 0;
}
#endif
