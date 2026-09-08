#ifndef SEARCH_H
#define SEARCH_H

#include <stddef.h>

typedef int (*response_writer)(void *context, const char *data,
							   size_t length);

/*
 * 検索して、messageとして返すためのsearch.cファイルへの橋渡し.
 */
const char *handle_search_request(const char *request);
int get_searchResults(const char *request, response_writer writer,
					  void *writer_context);

#endif
