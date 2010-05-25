/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2004 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.0 of the PHP license,       |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_0.txt.                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  |                                                                      |
  |                        **** WARNING ****                             |
  |                                                                      |
  | This module makes use of unRAR - free utility for RAR archives.      |
  | Its license states, that you MUST NOT use its code to develop        |
  | a RAR (WinRAR) compatible archiver.                                  |
  | Please, read unRAR license for full information.                     |
  | unRAR & RAR copyrights are owned by Eugene Roshal                    |
  +----------------------------------------------------------------------+
  | Author: Antony Dovgal <tony@daylessday.org>                          |
  | Author: Gustavo Lopes <cataphract@php.net>                           |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

/* TODO: correct handling of archives with entries with the same name */
/* TODO: metadata block reading */
/* TODO: correct support for symlinks inside RAR files. This includes:
 * - Respecting PHP_STREAM_URL_STAT_LINK in the url_stater
 * - Following the symlinks when asked to open one inside the RAR
 * Sym link support on windows will be more complicated */
/* TODO: add support for opening RAR files in a persisten fashion */
/* TODO: consider making struct rar opaque, outside rararch.c only
 * RarEntry::extract/getStream access the fields */
/* TODO: merge rar_file_t.entries_idx and rar_file_t.entries */
/* TODO: consider using a php memory/tmpfile stream to serve as buffer for
 * rar file streams */
/* TODO: improve RAR archive cache key for url_stater/dir_opener, so that it
 * can detect file modification */
/* TODO: make configurable the capacity of the usr_stater/dir_opener cache */

#ifndef PHP_RAR_H
#define PHP_RAR_H

extern zend_module_entry rar_module_entry;
#define phpext_rar_ptr &rar_module_entry

#define PHP_RAR_VERSION "2.1.0-dev"
#define PHP_RAR_REVISION "$Revision$"

#ifdef PHP_WIN32
#define PHP_RAR_API __declspec(dllexport)
#else
#define PHP_RAR_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

/* causes linking errors (multiple definitions) in functions
   that were requested inlining but were not inlined by the compiler */
/* #include "unrar/rar.hpp */
/* only these includes are necessary anyway: */
#include "unrar/raros.hpp"
/* no need to reinclude windows.h or new.h */
#define LEAN_RAR_INCLUDES
#include "unrar/os.hpp"
#include "unrar/dll.hpp"
#include "unrar/version.hpp"
/* These are in unrar/headers.hpp, but that header depends on several other */
enum HOST_SYSTEM {
  HOST_MSDOS=0,HOST_OS2=1,HOST_WIN32=2,HOST_UNIX=3,HOST_MACOS=4,
  HOST_BEOS=5,HOST_MAX
};
#define  LHD_WINDOWMASK     0x00e0U
#define  LHD_DIRECTORY      0x00e0U

//maximum comment size if 64KB
#define RAR_MAX_COMMENT_SIZE 65536

typedef struct _rar_cb_user_data {
	char					*password;	//can be NULL
	zval					*callable;  //can be NULL
} rar_cb_user_data;

typedef struct rar {
	zend_object_handle			id;
	int							entry_count; //>= number of files
	struct RARHeaderDataEx		**entries;
	//key: entry name, value: index in entries
	HashTable					*entries_idx; /* TODO: merge into entries */
	struct RAROpenArchiveDataEx	*list_open_data;
	struct RAROpenArchiveDataEx	*extract_open_data;
	//archive handle opened with RAR_OM_LIST_INCSPLIT open mode
	void						*arch_handle;
	//user data to pass the RAR callback
	rar_cb_user_data			cb_userdata;
} rar_file_t;

/* Per-request cache or make last the duration of the PHP lifespan?
 * - per-request advantages: we can re-use rar_open and store close RarArchive
 *   objects. We store either pointers to the objects directly and manipulate
 *	 the refcount in the store or we store zvals. Either way, we must decrement
 *	 the refcounts on request shutdown. Also, the memory usage is best kept
 *	 in check because the memory is freed after each request.
 * - per PHP lifespan advantages: more cache hits. We can also re-use rar_open,
 *	 but then we have to copy rar->entries and rar->entries_idx into
 *	 persistently allocated buffers since the RarArchive objects cannot be made
 *	 persistent themselves.
 *
 * I'll go with per-request and store zval pointers together with modification
 * time.
 * I'll also go with a FIFO eviction policy because it's simpler to implement
 * (just delete the first element of the HashTable).
 */
#ifdef ZTS
# define RAR_TSRMLS_TC	, void ***
#else
# define RAR_TSRMLS_TC
#endif

typedef struct _rar_contents_cache {
	int			max_size;
	HashTable	*data;		//persistent HashTable, will hold rar_cache_entry
	/* args: cache key, cache key size, cached object) */
	void (*put)(const char *, uint, zval * RAR_TSRMLS_TC);
	zval *(*get)(const char *, uint RAR_TSRMLS_TC);
} rar_contents_cache;

/* Module globals, currently used for dir wrappers cache */
ZEND_BEGIN_MODULE_GLOBALS(rar)
	rar_contents_cache contents_cache;
ZEND_END_MODULE_GLOBALS(rar)

ZEND_EXTERN_MODULE_GLOBALS(rar);

#ifdef ZTS
# define RAR_G(v) TSRMG(rar_globals_id, zend_rar_globals *, v)
#else
# define RAR_G(v) (rar_globals.v)
#endif

//PHP 5.2 compatibility
#if PHP_MAJOR_VERSION == 5 && PHP_MINOR_VERSION < 3
# define STREAM_ASSUME_REALPATH 0
# define ALLOC_PERMANENT_ZVAL(z) \
        (z) = (zval*) malloc(sizeof(zval));
# define OPENBASEDIR_CHECKPATH(filename) \
	(PG(safe_mode) && \
	(!php_checkuid(filename, NULL, CHECKUID_CHECK_FILE_AND_DIR))) \
	|| php_check_open_basedir(filename TSRMLS_CC)
# undef ZEND_BEGIN_ARG_INFO_EX
# define ZEND_BEGIN_ARG_INFO_EX(name, pass_rest_by_reference, return_reference, required_num_args) \
	static const zend_arg_info name[] = { \
		{ NULL, 0, NULL, 0, 0, 0, pass_rest_by_reference, return_reference, required_num_args },
#endif

/* rar.c */
PHP_MINIT_FUNCTION(rar);
PHP_MSHUTDOWN_FUNCTION(rar);
PHP_RINIT_FUNCTION(rar);
PHP_RSHUTDOWN_FUNCTION(rar);
PHP_MINFO_FUNCTION(rar);

PHP_FUNCTION(rar_bogus_ctor);

void _rar_wide_to_utf(const wchar_t *src, char *dest, size_t dest_size);
void _rar_utf_to_wide(const char *src, wchar_t *dest, size_t dest_size);
void _rar_destroy_userdata(rar_cb_user_data *udata);
int _rar_find_file(struct RAROpenArchiveDataEx *open_data, /* IN */
				   const char *const utf_file_name, /* IN */
				   rar_cb_user_data *cb_udata, /* IN, must be managed outside */
				   void **arc_handle, /* OUT: where to store rar archive handle  */
				   int *found, /* OUT */
				   struct RARHeaderDataEx *header_data /* OUT, can be null */
				   );
int _rar_find_file_w(struct RAROpenArchiveDataEx *open_data, /* IN */
					 const wchar_t *const file_name, /* IN */
					 rar_cb_user_data *cb_udata, /* IN, must be managed outside */
					 void **arc_handle, /* OUT: where to store rar archive handle  */
					 int *found, /* OUT */
					 struct RARHeaderDataEx *header_data /* OUT, can be null */
					 );
int CALLBACK _rar_unrar_callback(UINT msg, LPARAM UserData, LPARAM P1, LPARAM P2);

/* rar_error.c */
zend_class_entry *rarexception_ce_ptr;
int _rar_handle_error(int errcode TSRMLS_DC);
int _rar_handle_error_ex(const char *preamble, int errcode TSRMLS_DC);
void _rar_handle_ext_error(const char *format TSRMLS_DC, ...);
int _rar_using_exceptions(TSRMLS_D);
const char * _rar_error_to_string(int errcode);
void minit_rarerror(TSRMLS_D);

/* rararch.c */
int _rar_create_rararch_obj(const char* resolved_path,
							const char* open_password,
							zval *volume_callback, //must be callable or NULL
							zval *object,
							int *err_code TSRMLS_DC);
void _rar_close_file_resource(rar_file_t *rar);
int _rar_index_entries(rar_file_t *rar_file TSRMLS_DC);

/* entry search API {{{ */
typedef struct _rar_find_output {
	int							found;
	struct RARHeaderDataEx *	header;
	unsigned long				packed_size;
	int							eof;
} rar_find_output;
void _rar_entry_search_start(rar_file_t *rar, rar_find_output **state);
void _rar_entry_search_end(rar_find_output *state);
void _rar_entry_search_rewind(rar_find_output *state);
void _rar_entry_search_advance(rar_find_output *state,
							   const wchar_t * const file, //NULL = give next
							   size_t file_size,
							   int directory_match);
/* end entry search API }}} */

/* Fetches the rar_file_t part of the RarArchive object in order to use the
 * operations above and (discouraged) to have direct access to the fields
 * RarEntry::extract/getStream access extract_open_dat and cb_userdata */
int _rar_get_file_resource(zval *zval_file, rar_file_t **rar_file TSRMLS_DC);
int _rar_get_file_resource_ex(zval *zval_file, rar_file_t **rar_file, int silent TSRMLS_DC);
void minit_rararch(TSRMLS_D);

PHP_FUNCTION(rar_open);
PHP_FUNCTION(rar_list);
PHP_FUNCTION(rar_entry_get);
PHP_FUNCTION(rar_solid_is);
PHP_FUNCTION(rar_comment_get);
PHP_FUNCTION(rar_close);

/* rarentry.c */
extern zend_class_entry *rar_class_entry_ptr;
void minit_rarentry(TSRMLS_D);
void _rar_entry_to_zval(zval *parent,
						struct RARHeaderDataEx *entry,
						unsigned long packed_size,
						zval *entry_object TSRMLS_DC);

/* rar_stream.c */
php_stream *php_stream_rar_open(char *arc_name,
								char *utf_file_name,
								rar_cb_user_data *cb_udata_ptr, /* will be copied */
								char *mode STREAMS_DC TSRMLS_DC);
extern php_stream_wrapper php_stream_rar_wrapper;

#endif	/* PHP_RAR_H */


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
