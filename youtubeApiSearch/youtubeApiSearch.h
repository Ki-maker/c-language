#ifndef SEARCH_H
#define SEARCH_H

#include <stddef.h>

typedef int (*response_writer)(void *context, const char *data,
							   size_t length);

typedef struct {
    char *videoId;
    char *channelId;
    char *title;
    char *channelName;
    char *imageLarge;
    char *imageMiddle;
    char *publishedAt;
    long long viewCount;
    long long likeCount;
    long long commentCount;
    char *videoTime;
    long long subscriberCount;
} YouTubeApiContents;

typedef struct {
    YouTubeApiContents *items;
    size_t count;
} YouTubeApiContentsList;

/*
 * API検索して、取得した構造をまとめて返す.
 * main.c から呼び出される.
 */
YouTubeApiContentsList *getYoutubeContents(const char *keyword);
void freeYoutubeApiContents(YouTubeApiContentsList *contents);

#endif


