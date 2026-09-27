#ifndef OPENSEARCH_DELETE_H
#define OPENSEARCH_DELETE_H

/*
 * OpenSearchから期限切れのYouTubeコンテンツを定期削除する関数
 * insertedAtが1分以上前の文書を削除する
 */
int deleteExpiredYoutubeContentsFromOpenSearch(void);

#endif
