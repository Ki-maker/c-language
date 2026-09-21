#ifndef SEARCH_H
#define SEARCH_H

#include <stddef.h>

typedef int (*response_writer)(void *context, const char *data,
							   size_t length);

/*
 * API検索して、OpenSearchにデータを入れる処理を行うためのsearch.cファイルへの橋渡し.
 */
const char *handle_search_request(const char *request);
int getYoutubeContents(const char *keyword, response_writer writer,
					   void *writer_context);

#endif
