#ifndef SEARCH_H
#define SEARCH_H

#include <stddef.h>

typedef int (*response_writer)(void *context, const char *data,
							   size_t length);

/*
 * API検索して、JSON文字列を返す.
 * 呼び出し元は返されたメモリを free() する必要がある.
 */
char *getYoutubeContents(const char *keyword);

/*
 * YouTube検索結果を OpenSearch に登録する.
 * 1: 成功, 0: 失敗
 */
int setYoutubeContentsToOpenSearch(const char *keyword);
#endif
