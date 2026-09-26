#ifndef INGESTION_H
#define INGESTION_H

#include "../search/search.h"

/*
 * APIのデータをOpenSearchに登録する.
 * 1: 成功, 0: 失敗.
 */
int setYoutubeContentsToOpenSearch(const YouTubeApiContentsList *contents);

/* insertedAtが24時間より古い文書をOpenSearchから削除する. */
int deleteExpiredYoutubeContentsFromOpenSearch(void);

#endif