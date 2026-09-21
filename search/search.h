#ifndef SEARCH_H
#define SEARCH_H

#include <stddef.h>

typedef int (*response_writer)(void *context, const char *data,
							   size_t length);

typedef struct {
    char *search_json;
    char *stats_json;
    char *content_json;
    char *channel_json;
} YouTubeApiContents;

/*
 * API検索して、整形済みのJSON文字列を返す.
 * main.c から呼び出される.
 * 呼び出し元は返されたメモリを free() する必要がある.
 */
char *getFormattedYoutubeContents(const char *keyword);

/*
 * YouTube検索結果を OpenSearch に登録する.
 * 1: 成功, 0: 失敗
 */
int setYoutubeContentsToOpenSearch(const char *keyword);
#endif

/*
 * API検索して、取得した構造をまとめて返す.
 * getFormattedYoutubeContents() で呼び出される.
 */
YouTubeApiContents *getYoutubeContents(const char *keyword);
void freeYoutubeApiContents(YouTubeApiContents *contents);


