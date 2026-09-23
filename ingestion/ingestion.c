#include <stdio.h>
#include <stdlib.h>

#include <curl/curl.h>

#include "ingestion.h"

static size_t discard_response(void *data, size_t size, size_t count)
{
    (void)data;
    return size * count;
}

/*
 * APIのデータをOpenSearchに登録する.
 * 1: 成功, 0: 失敗
 */
int setYoutubeContentsToOpenSearch(const YouTubeApiContentsList *contents)
{
    const char *opensearch_url;
    CURL *curl;
    CURLcode curl_result;
    long http_status = 0;

    if (contents == NULL) {
        return 0;
    }

    printf("OpenSearchにデータを登録します: %zu件\n", contents->count);

    opensearch_url = getenv("OPENSEARCH_URL");
    if (opensearch_url == NULL || opensearch_url[0] == '\0') {
        opensearch_url = "http://localhost:9200/";
    }

    curl = curl_easy_init();
    if (curl == NULL) {
        fprintf(stderr, "libcurlの初期化に失敗しました\n");
        return 0;
    }

    curl_easy_setopt(curl, CURLOPT_URL, opensearch_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    // OpenSearchにデータを送信する
    curl_result = curl_easy_perform(curl);
    if (curl_result == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    }

    curl_easy_cleanup(curl);
    fflush(stdout);

    if (curl_result != CURLE_OK) {
        fprintf(stderr, "OpenSearchへの接続に失敗しました: %s\n",
                curl_easy_strerror(curl_result));
        return 0;
    }

    if (http_status < 200 || http_status >= 300) {
        fprintf(stderr, "OpenSearchからHTTPステータス %ld が返されました\n",
                http_status);
        return 0;
    }

    printf("OpenSearchへの接続に成功しました\n");
    return 1;
}