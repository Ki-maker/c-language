#ifndef OPENSEARCH_NETWORK_H
#define OPENSEARCH_NETWORK_H

/* OpenSearchへHTTPリクエストを送り、応答本文を呼び出し元へ返す. */
int executeOpenSearchRequest(const char *method, const char *path,
                             const char *body, const char *content_type,
                             char **response_body, long *http_status);

#endif