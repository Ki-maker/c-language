#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "search.h"

// TODO このメモリサイズは元に戻す
#define BUFFER_API_RESPONSE 2560
#define BUFFER_END 1

#define YOUTUBE_API_DOMAIN "https://www.googleapis.com/youtube/v3"
#define YOUTUBE_SEARCH_PATH "/search"
#define YOUTUBE_SEARCH_QUERY_FORMAT \
    "?part=snippet&q=%s&type=video&maxResults=%d&key=%s"
#define MAX_SEARCH_RESULTS 50

typedef struct {
    char *buffer;
    size_t length;
    size_t capacity;
} response_buffer;

/*
 * sizeとcountをかけたバイト数を返すコールバック関数.
 * Returns total_size
 */
static size_t write_callback(void *contents, size_t size,
                             size_t count, void *body_data)
{
    response_buffer *buffer = body_data;
    size_t api_response_size = size * count;
    size_t required_capacity = buffer->length + api_response_size + 1;

    if (required_capacity > buffer->capacity) {
        size_t new_capacity = buffer->capacity == 0 ?
                                  BUFFER_API_RESPONSE + BUFFER_END :
                                  buffer->capacity;

        while (new_capacity < required_capacity) {
            new_capacity *= 2;
        }

        char *new_buffer = realloc(buffer->buffer, new_capacity);
        if (new_buffer == NULL) {
            fprintf(stderr, "APIレスポンスのメモリ確保に失敗しました\n");
            return 0;
        }

        buffer->buffer = new_buffer;
        buffer->capacity = new_capacity;
    }

    memcpy(buffer->buffer + buffer->length, contents, api_response_size);
    buffer->length += api_response_size;
    buffer->buffer[buffer->length] = '\0';

    return api_response_size;
}

/*
 * 検索して、messageとして返す.
 * Returns the message 検索結果, NULL 検索に失敗.
 */
const char *handle_search_request(const char *request)
{
    //strncmp構文
    // strncmp(文字列1, 文字列2, 比較する文字数)
    // 0の結果は文字列1と文字列2が同じであることを意味する
    if (strncmp(request, "GET /search?", strlen("GET /search?")) == 0 ||
        strncmp(request, "GET /search ", strlen("GET /search ")) == 0) {
#ifdef _WIN32
        //windows環境の場合、文字コードにUTF-8を使用する
        SetConsoleOutputCP(CP_UTF8);
#endif
        printf("検索ボタンが押されました\n");
        fflush(stdout);
        return "検索ボタンが押されました";
    }

    return NULL;
}

/*
 * API検索して、JSON文字列を返す.
 * 呼び出し元は free() で解放する必要がある.
 */
char *getYoutubeContents(const char *keyword)
{
    CURL *curl = curl_easy_init();
    const char *api_key = getenv("YOUTUBE_API_KEY");
    char *encoded_query = NULL;
    char url[1024];
    response_buffer response = {0};

    if (curl == NULL) {
        fprintf(stderr, "curlの初期化に失敗しました\n");
        return NULL;
    }

    if (keyword == NULL || keyword[0] == '\0') {
        fprintf(stderr, "検索キーワードが空です\n");
        curl_easy_cleanup(curl);
        return NULL;
    }

    if (api_key == NULL || api_key[0] == '\0') {
        fprintf(stderr,
                "YOUTUBE_API_KEYが設定されていません。\n");
        curl_easy_cleanup(curl);
        return NULL;
    }

    response.buffer = malloc(BUFFER_API_RESPONSE + BUFFER_END);
    if (response.buffer == NULL) {
        fprintf(stderr, "JSONバッファの初期化に失敗しました\n");
        curl_easy_cleanup(curl);
        return NULL;
    }
    response.buffer[0] = '\0';
    response.length = 0;
    response.capacity = BUFFER_API_RESPONSE + BUFFER_END;

    encoded_query = curl_easy_escape(curl, keyword, 0);
    if (encoded_query == NULL) {
        fprintf(stderr, "検索語のURLエンコードに失敗しました\n");
        free(response.buffer);
        curl_easy_cleanup(curl);
        return NULL;
    }

    snprintf(url, sizeof(url), "%s%s" YOUTUBE_SEARCH_QUERY_FORMAT,
             YOUTUBE_API_DOMAIN, YOUTUBE_SEARCH_PATH,
             encoded_query, MAX_SEARCH_RESULTS, api_key);

    // API を呼ぶ前に、レスポンスの受け取り方や保存先を先に設定する
    // CURLOPT_URL = URLを設定する
    curl_easy_setopt(curl, CURLOPT_URL, url);
    // CURLOPT_WRITEFUNCTION = レスポンスを受け取るコールバック関数
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    // CURLOPT_WRITEDATA = コールバック関数に渡すデータ
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode result_code = curl_easy_perform(curl);
    curl_free(encoded_query);
    curl_easy_cleanup(curl);

    if (result_code != CURLE_OK) {
        fprintf(stderr, "通信エラー: %s\n",
                curl_easy_strerror(result_code));
        free(response.buffer);
        return NULL;
    }

    if (response.buffer == NULL || response.buffer[0] == '\0') {
        free(response.buffer);
        return NULL;
    }

    return response.buffer;
}