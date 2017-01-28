/**
  * Copyright (C) 2005 Nokia Corporation.
  *
  * Contact: Leonid Zolotarev <leonid.zolotarev@nokia.com>
  *
  * This library is free software; you can redistribute it and/or
  * modify it under the terms of the GNU Lesser General Public License
  * as published by the Free Software Foundation; either version 2.1 of
  * the License, or (at your option) any later version.
  *
  * This library is distributed in the hope that it will be useful, but
  * WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  * Lesser General Public License for more details.
  *
  * You should have received a copy of the GNU Lesser General Public
  * License along with this library; if not, write to the Free Software
  * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
  * 02110-1301 USA
  *
  */

#ifndef __OSSO_BOOKMARK_PARSER_H__
#define __OSSO_BOOKMARK_PARSER_H__

/* System header file*/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <time.h>
#include <libxml/xmlreader.h>

G_BEGIN_DECLS

#define FAVICONS_PATH		   	"/.bookmarks/favicons"
#define THUMBNAIL_PATH          "/.bookmarks/thumbnails"
#define BOOKMARKLOCK_PATH		"/.bookmarks/.lock"
#define HOME_ENV   		        "HOME"
#define BOOKMARK_GCONF_SORT_PATH        "/apps/osso/bookmark/sort"

#define CHECK_PARAM(condition, message,  iffail)   \
{                                                   \
   if(condition)  {                               \
       g_print(" %s\n",message);                    \
       iffail;                                     \
   }                                              \
}

typedef enum {
    NS_SITE,
    NS_NOTES,
    NS_FOLDER,
    NS_FOLDER_END,
    NS_SEPARATOR,
    NS_UNKNOWN,
    NS_END
} NSItemType;


typedef enum {
    BM_FOLDER,
    BM_SITE
} BookmarkType;

typedef struct _BookmarkItem BookmarkItem;
struct _BookmarkItem {
    /* The type of this bookmark */
    gboolean isFolder;
    /* The user provided name of the bookmark */
    /* This name will be shown in File Selection View */
    gchar *name;
    /* The location */
    gchar *url;
    /* The favicon file used to represent it */
    /* If it is not available, normal folder or file icon will be used */
    /*Open Issue: How to get this favicon item */
    gchar *favicon_file;
    /* the list of children if this is a folder */
    GSList *list;
    /* parent folder (NULL for root bookmark) */
    BookmarkItem *parent;

    /* The time info */
    GTime time_added;
    GTime time_last_visited;

    gchar *thumbnail_file;
    
    guint visit_count;
    /* Flag for recognizing operator bookmark */
    gboolean isOperatorBookmark;
    /* Flag for deleted operator bookmarks */
    gboolean isDeleted;
};

/* Sorting order Ascending or Descending*/
typedef enum {
    BM_ASC = 0,
    BM_DSC
} SortOrder;

typedef enum
{
   SORT_BY_NAME_ASC,
   SORT_BY_NAME_DSC,
   SORT_BY_LASTVISIT_ASC,
   SORT_BY_LASTVISIT_DSC,
   SORT_BY_VISITCOUNT_ASC
} SortType;

/* Inserting node depend on sort type:
 * Sort by name, added time and visited time */
typedef enum
{
   INSERT_BY_NAME,
   INSERT_BY_VISIT_TIME,
   INSERT_BY_VISIT_COUNT
}insertParam;

/* Bookmark Engine Errors*/
typedef enum {
    BM_OK,
    BM_INVALID_FILE,
    BM_LOW_MEM,
    BM_INVALID_PARAMETER
} BMError;

#define MYBOOKMARK_PREFIX "MY:"
#define USERBOOKMARK_PREFIX "USER:"

/**
 * create_bookmark_new:
 * @param None
 * @return Return bookmark item after allocating memory
 */
BookmarkItem *create_bookmark_new(void);

/**
 * bookmark_add_child:
 * @param parent: Parent Bookmark item
 * @param c: Bookmark item which will added under parent
 * @param position: Position at which item will be added, -1 to add the item 
 * at the end.
 * @return BM_OK if Succesfull, otherwise return type of error.
 *
 * This function adds the bookmark in XML file under parent item.
 */
BMError bookmark_add_child(BookmarkItem * parent, BookmarkItem * bm_item,
			   gint position, xmlNode * root_element);

/**
 * bookmark_set_name:
 * @param node: Bookmark item whose name has to be modified
 * @param val: New name for the bookamrk item
 * @param doc: XML document tree(Return value of xmlParseFile)
 * @param root_element:  Root element of the XML document 
 *
 * This function sets new name for the bookmark item.
 */
gboolean bookmark_set_name(BookmarkItem * node, const gchar * val, xmlDocPtr doc, xmlNode *root_element);

/**
 * bookmark_set_url:
 * @param node: Bookmark item whose URL has to be modified
 * @param val: New URL  for the bookamrk item
 * @param doc: XML document tree(Return value of xmlParseFile)
 * @param root_element:  Root element of the XML document 
 *
 * This function sets new URL  for the bookmark item.
 */
gboolean bookmark_set_url(BookmarkItem * node, const gchar * val, xmlDocPtr doc, xmlNode *root_element);

/**
 * bookmark_set_thumbnail:
 * @param node: Bookmark item whose thumbnail has to be modified
 * @param val: New thumbnail filename  for the bookmark item
 * @param doc: XML document tree(Return value of xmlParseFile)
 * @param root_element:  Root element of the XML document 
 *
 * This function sets new thumbnail filename  for the bookmark item.
 */
gboolean bookmark_set_thumbnail(BookmarkItem * node, const gchar * val, xmlDocPtr doc, xmlNode *root_element);

gchar *get_base_url_name(const gchar * url);

gboolean opened_bookmark_remove(BookmarkItem * node,
				xmlNodePtr root_element);

BMError opened_bookmark_add_child(BookmarkItem * parent,
				  BookmarkItem * bm_item, gint position,
				  xmlNode * root_element);

BMError opened_bm_engine_insert_node_at_sorted_position(BookmarkItem *
							parent,
							BookmarkItem *
							bm_item,
							SortOrder
							sort_order,
							xmlNode *
							root_element);

/**
 *  netscape_export_bookmarks:
 *  @param filename: the file in which bookmark gets exported
 *  @param root: List of bookmark items which needs to be exported
 *  @use_locale:
 *  @return TRUE if success , FALSE otherwise
 */
gboolean netscape_export_bookmarks(const gchar * filename, GSList * root,
				   const gchar *bm_parent_name);

/**
 * bm_engine_insert_node_at_sorted_position:
 * @param parent: Bookmark item
 * @param bm_item: Item need to be inserted at sorted postion
 * @sortoreder: Ascending or Descending
 * @param doc: XML document tree(Return value of xmlParseFile)
 * @param root_element:  Root element of the XML document 
 *
 * This function adds the item in the sorted position.
 * @return Return BM_OK if successful or error
 */
BMError bm_engine_insert_node_at_sorted_position(BookmarkItem * parent,
						 BookmarkItem * bm_item,
						 SortOrder sort_order,
						 xmlDocPtr doc,
						 xmlNode *root_element);

/**
 * bm_engine_add_duplicate_item:
 * @param parent: parent where duplicate item will be added
 * @param c: duplicate bookmark item
 *
 * This function adds duplicate bookmark item
 */
gboolean bm_engine_add_duplicate_item(BookmarkItem * parent,
				      BookmarkItem * bm_item);

/**
 * bookmark_import:
 * @param path: Path of the file to be imported
 * @param importFoldername: Name of the import folder
 * @param import_folder: List of import items
 * @return TRUE if file is valid bookmark file, FALSE otherwise
 *
 * This function imports Nescape navigator bookamrk format file.
 */
gboolean bookmark_import(const gchar * path, gchar * importFolderName,
			 BookmarkItem ** import_folder);
/**
 *  osso_bookmark_get_folders_list:
 *  @param None
 *  @return List of first level folders from
 *  My bookmarks file.
 *  
 *  Note: Caller should free the return list.
 */
GSList *osso_bookmark_get_folders_list(void);

/**
 * set_bookmark_files_path:
 * @param None
 * 
 * This function uses to copy the bookmark related files(favicons & xml files) from
 * system directory to user's home.
 */
void set_bookmark_files_path (void);
/**
 * get_complete_path:
 * @param parentItem: Bookmark item
 *
 * Get the complete path of the bookmark parent. 
 * @return full path in GSList
 */
GSList *get_complete_path(BookmarkItem * parentItem);

/**
 * bookmark_gslist_find:
 * @param parent_list: List of items where newItem will be inserted
 * @param newItem: New bookmark item
 * @return Return bookmark item which will be pevious item of new item as per
 * the sorting order
 */
BookmarkItem *bookmark_gslist_find(GSList * parent_list,
                        BookmarkItem * newItem, SortType presentSortType);

/**
 * bookmark_gslist_find_by_addeddate:
 * @param parent_list: List of items where search will be carried out
 * @param newItem: Search new item in the list to find out wheren it can be inserted.
 * @param ins_param: Insert by addition date, modification date or name
 * @return Return the bookmark item which will be before the newItem as per the sorting order
 */
BookmarkItem *bookmark_gslist_find_by_addeddate(GSList * parent_list,
                                           BookmarkItem * newItem,
                                           insertParam ins_param, SortType presentSortType);

/**
 * bookmark_get_sorting_order:
 * @param sortkey: sort type
 * @return sort order info depending on sort order
 */
SortOrder bookmark_get_sorting_order(void);

/**
 * osso_bookmark_gconf_set_int:
 * @param key: GConf key value.
 * @param val: Value of the Sort type
 *
 * Store the SortType  value in GConf.
 * @return Return TRUE if success , FALSE otherwise
 */
gboolean osso_bookmark_gconf_set_int(gchar * key, gint val);

/**
 * osso_bookmark_gconf_get_int:
 * @param key: Gconf key.
 *
 * Get the SortType value from the GConf.
 * @return sort type info depending on sort type
 */
SortType osso_bookmark_gconf_get_int(gchar * key);

/**
 * bookmark_add_child_at_sorted_position:
 * @param parent: Bookmark item parent
 * @param bm_item: Bookmark item
 * @param presentSortType: Bookmark Sort type
 * @param doc: XML document tree(Return value of xmlParseFile)
 * @param root_element:  Root element of the XML document 
 *
 * This function adds the item in the sorted position.
 * @return Return BM_OK if successful or error
 */
BMError bookmark_add_child_at_sorted_position(BookmarkItem * parent,
		BookmarkItem * bm_item,
		SortType presentSortType,
		xmlDocPtr doc,
		xmlNode * root_element);
/**
 * set_lock:
 * @param lock_file_name: Lock file name.
 *
 * Create the lock file.
 * @return Return TRUE if success , FALSE otherwise
 */
gboolean set_lock(gchar * lock_file_name);

/**
 * set_lock:
 * @param lock_file_name: Lock file name.
 *
 * Delete the lock file.
 * @return Return TRUE if success , FALSE otherwise
 */
gboolean del_lock(gchar * lock_file_name);

#ifdef BOOKMARK_ENGINE_DISABLE_DEPRECATED
/**
 *  get_root_bookmark:
 *  @param bookmark_root: Returns List of bookmark items
 *  @return Return TRUE if success , FALSE otherwise
 *
 *  This function parses XML file and return list of BookmarkItems.
 */
gboolean
get_root_bookmark (BookmarkItem **bookmark_root);

/**
 * bookmark_remove:
 * @param node: The bookmark node to delete
 * @return TRUE if success, FALSE otherwise
 * 
 * This function removes the specified bookmark item from the XML file.
 */
gboolean
bookmark_remove (BookmarkItem *node);

/**
 * bookmark_set_time_last_visited:
 * @param node: Bookmark item whose last visited time needs to be modified
 * @param val: Last visited time value
 * @param doc: XML document tree(Return value of xmlParseFile)
 * @param root_element:  Root element of the XML document 
 *
 * This function sets the last visited time for the bookmark item to a new value.
 */
gboolean
bookmark_set_time_last_visited (BookmarkItem *node,
                                const gchar *val,
                                xmlDocPtr doc,
                                xmlNode *root_element);

/**
 * bookmark_set_visit_count:
 * @param bm_item Bookmark item whose visit count needs to be modified.
 * @param val New visit count value.
 * @param doc: XML document tree(Return value of xmlParseFile)
 * @param root_element:  Root element of the XML document 
 * 
 * This function sets the visit count for the bookmark item to a new value.
 */
gboolean
bookmark_set_visit_count (BookmarkItem *bm_item,
                          const gchar *val, 
                          xmlDocPtr doc,
                          xmlNode *root_element);

/**
 * bm_engine_add_folder:
 * @param parent: parent folder where bookmark item will be added
 * @param bm_item: Bookmark item
 *
 * This function adds folder to parent item.
 */
gboolean
bm_engine_add_folder (BookmarkItem *parent,
                      BookmarkItem *bm_item);

/**
 * create_bookmarks_backup:
 * 
 * This function create empty bookmark template.
 */
gboolean
create_bookmarks_backup ();

/**
 * get_bookmark_from_backup:
 * @param bookmark_root: The list of bookmark items.
 *
 * This function returns list of My bookmarks
 * from new created the XML files.
 */
gboolean
get_bookmark_from_backup (BookmarkItem **bookmark_root);

#else // BOOKMARK_ENGINE_DISABLE_DEPRECATED

#define MYBOOKMARKS                 "/.bookmarks/MyBookmarks.xml"
#define MYBOOKMARKSFILEBACKUP       "/.bookmarks/MyBookmarks.xml.backup"

gboolean get_root_bookmark (BookmarkItem **bookmark_root,
                            gchar *file_name);

/**
 *  get_root_bookmark_absolute_path:
 *  @param bookmark_root: Returns List of bookmark items  
 *  @param file_name: Absolute path to bookmark XML file
 *  @return Return TRUE if success , FALSE otherwise
 *
 *  This function parses XML file and return list of BookmarkItems.
 */
gboolean get_root_bookmark_absolute_path(BookmarkItem ** bookmark_root,
                                         gchar * file_name);

gboolean bookmark_remove (BookmarkItem *node,
                          gchar *file_name);

gboolean bookmark_remove_list (GSList *item_list);

gboolean bookmark_set_time_last_visited (BookmarkItem *node,
                                         const gchar *val,
                                         const gchar *file_name, 
                                         xmlDocPtr doc,
                                         xmlNode *root_element);
gboolean bookmark_set_visit_count (BookmarkItem *bm_item,
                                   const gchar *val,
                                   const gchar *file_name, 
                                   xmlDocPtr doc,
                                   xmlNode *root_element);

/**
 * bookmark_set_operator_bookmark_as_deleted:
 * @param bm_item Bookmark item whose visit count needs to be modified.
 * @param file_name: which file ? operator or My bookmarks
 * @param doc: XML document tree(Return value of xmlParseFile)
 * @param root_element:  Root element of the XML document 
 * 
 * This function sets the given bookmark as deleted if it is a operator bookmark.
 */
gboolean bookmark_set_operator_bookmark_as_deleted(BookmarkItem * bm_item,
                                                   gchar * file_name, 
                                                   xmlDocPtr doc,
                                                   xmlNode *root_element);

gboolean bm_engine_add_folder (BookmarkItem *parent,
                               BookmarkItem *bm_item,
                               const gchar *file_name);
gboolean create_bookmarks_backup (const gchar *file_name);
gboolean get_bookmark_from_backup (BookmarkItem **bookmark_root,
                                   const gchar *file_name);

#endif // BOOKMARK_ENGINE_DISABLE_DEPRECATED

G_END_DECLS
#endif				/* __OSSO_BOOKMARK_PARSER_H__ */
