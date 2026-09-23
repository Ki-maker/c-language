#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "search.h"

// TODO このメモリサイズは元に戻す
#define BUFFER_API_RESPONSE 35000
#define BUFFER_END 1

#define YOUTUBE_API_DOMAIN "https://www.googleapis.com/youtube/v3"
#define YOUTUBE_SEARCH_PATH "/search"
#define YOUTUBE_SEARCH_QUERY_FORMAT \
    "?part=snippet&q=%s&type=video&maxResults=%d&fields=items(id/kind,id/videoId,snippet/title,snippet/channelId,snippet/channelTitle,snippet/publishedAt,snippet/thumbnails/medium/url,snippet/thumbnails/high/url)&key=%s"
#define MAX_SEARCH_RESULTS 50

typedef struct {
    char *buffer;
    size_t length;
    size_t capacity;
} response_buffer;

/**
 * 文字列を複製する.
 */
static char *duplicate_string(const char *value)
{
    size_t length;
    char *copy;

    if (value == NULL) {
        return NULL;
    }

    length = strlen(value);
    copy = malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, value, length + 1);
    return copy;
}

static char *copy_json_string(const cJSON *object, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);

    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return NULL;
    }

    return duplicate_string(item->valuestring);
}

static long long copy_json_number(const cJSON *object, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);

    if (cJSON_IsNumber(item)) {
        return (long long)item->valuedouble;
    }
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        return strtoll(item->valuestring, NULL, 10);
    }
    return 0;
}

static char *build_content_json(const YouTubeApiContents *contents)
{
    cJSON *object;
    cJSON *image;
    char *result;

    if (contents == NULL) {
        return NULL;
    }

    object = cJSON_CreateObject();
    image = cJSON_CreateObject();
    if (object == NULL || image == NULL) {
        cJSON_Delete(object);
        cJSON_Delete(image);
        return NULL;
    }

    cJSON_AddStringToObject(object, "videoId", contents->videoId == NULL ? "" : contents->videoId);
    cJSON_AddStringToObject(object, "channelId", contents->channelId == NULL ? "" : contents->channelId);
    cJSON_AddStringToObject(object, "title", contents->title == NULL ? "" : contents->title);
    cJSON_AddStringToObject(object, "channelName", contents->channelName == NULL ? "" : contents->channelName);
    cJSON_AddStringToObject(image, "large", contents->imageLarge == NULL ? "" : contents->imageLarge);
    cJSON_AddStringToObject(image, "middle", contents->imageMiddle == NULL ? "" : contents->imageMiddle);
    cJSON_AddItemToObject(object, "image", image);
    cJSON_AddStringToObject(object, "publishedAt", contents->publishedAt == NULL ? "" : contents->publishedAt);
    cJSON_AddNumberToObject(object, "viewCount", (double)contents->viewCount);
    cJSON_AddNumberToObject(object, "likeCount", (double)contents->likeCount);
    cJSON_AddNumberToObject(object, "commentCount", (double)contents->commentCount);
    cJSON_AddStringToObject(object, "videoTime", contents->videoTime == NULL ? "" : contents->videoTime);
    cJSON_AddNumberToObject(object, "subscriberCount", (double)contents->subscriberCount);

    result = cJSON_PrintUnformatted(object);
    cJSON_Delete(object);
    return result;
}

static char *build_aggregate_json(const YouTubeApiContentsList *contents)
{
    cJSON *array;
    char *result;
    size_t index;

    if (contents == NULL) {
        return NULL;
    }

    array = cJSON_CreateArray();
    if (array == NULL) {
        return NULL;
    }

    for (index = 0; index < contents->count; index++) {
        char *item_json = build_content_json(&contents->items[index]);
        cJSON *item;

        if (item_json == NULL) {
            cJSON_Delete(array);
            return NULL;
        }
        item = cJSON_Parse(item_json);
        free(item_json);
        if (item == NULL) {
            cJSON_Delete(array);
            return NULL;
        }
        cJSON_AddItemToArray(array, item);
    }

    result = cJSON_PrintUnformatted(array);
    cJSON_Delete(array);
    return result;
}

static size_t write_callback(void *contents, size_t size,
                             size_t count, void *body_data);

/*
 * YouTube APIからデータを取得する.
 * 1: 成功, 0: 失敗
 */
static int fetch_youtube_api_data(const char *label,
                                  const char *url,
                                  response_buffer *response)
{
    CURL *curl = curl_easy_init();
    CURLcode result_code;

    if (curl == NULL) {
        fprintf(stderr, "%s の初期化に失敗しました\n", label);
        return 0;
    }

    if (response == NULL || url == NULL || url[0] == '\0') {
        curl_easy_cleanup(curl);
        return 0;
    }

    response->buffer = malloc(BUFFER_API_RESPONSE + BUFFER_END);
    if (response->buffer == NULL) {
        fprintf(stderr, "%s のバッファ確保に失敗しました\n", label);
        curl_easy_cleanup(curl);
        return 0;
    }

    response->buffer[0] = '\0';
    response->length = 0;
    response->capacity = BUFFER_API_RESPONSE + BUFFER_END;

    fprintf(stdout, "[%s API URL]\n%s\n", label, url);
    fflush(stdout);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);

    result_code = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (result_code != CURLE_OK || response->buffer == NULL ||
        response->buffer[0] == '\0') {
        fprintf(stderr, "%s の取得に失敗しました\n", label);
        free(response->buffer);
        response->buffer = NULL;
        return 0;
    }

    return 1;
}

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
     fprintf(stderr, "APIレスポンスがバッファサイズを超えました\n");
     return 0;
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
 * API検索して、検索結果と追加取得したデータをまとめた構造体を返す.
 * 呼び出し元は freeYoutubeApiContents() で解放する必要がある.
 */
YouTubeApiContentsList *getYoutubeContents(const char *keyword)
{
    CURL *curl = curl_easy_init();
    const char *api_key = getenv("YOUTUBE_API_KEY");
    char *encoded_query = NULL;
    char search_url[1052];
    response_buffer response = {0};
    YouTubeApiContentsList *youtubeContents = NULL;

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

    encoded_query = curl_easy_escape(curl, keyword, 0);
    if (encoded_query == NULL) {
        fprintf(stderr, "検索語のURLエンコードに失敗しました\n");
        free(response.buffer);
        curl_easy_cleanup(curl);
        return NULL;
    }

    snprintf(search_url, sizeof(search_url), "%s%s" YOUTUBE_SEARCH_QUERY_FORMAT,
             YOUTUBE_API_DOMAIN, YOUTUBE_SEARCH_PATH,
             encoded_query, MAX_SEARCH_RESULTS, api_key);

    if (strlen(search_url) >= sizeof(search_url)) {
        fprintf(stderr, "検索URLがバッファサイズを超えました\n");
        curl_free(encoded_query);
        curl_easy_cleanup(curl);
        free(response.buffer);
        return NULL;
    }

    // YouTube APIから検索結果を取得する
    if (!fetch_youtube_api_data("YouTube Search", search_url, &response)) {
        curl_free(encoded_query);
        curl_easy_cleanup(curl);
        free(response.buffer);
        return NULL;
    }

    curl_free(encoded_query);
    curl_easy_cleanup(curl);

    // cJSONを使って検索結果のJSONをstructに変換する
    {
        cJSON *search_root = cJSON_Parse(response.buffer);
        cJSON *search_items;
        char video_ids[4096] = {0};
        char channel_ids[4096] = {0};
        size_t index;
        cJSON *search_item;
        int item_index;

        if (search_root == NULL) {
            free(response.buffer);
            return NULL;
        }

        search_items = cJSON_GetObjectItemCaseSensitive(search_root, "items");
        youtubeContents = calloc(1, sizeof(*youtubeContents));
        if (youtubeContents == NULL || !cJSON_IsArray(search_items)) {
            cJSON_Delete(search_root);
            free(response.buffer);
            free(youtubeContents);
            return NULL;
        }

        // youtube#video以外の項目を検索配列から削除する
        for (item_index = cJSON_GetArraySize(search_items) - 1;
             item_index >= 0;
             item_index--) {
            search_item = cJSON_GetArrayItem(search_items, item_index);
            cJSON *search_id = cJSON_GetObjectItemCaseSensitive(search_item, "id");
            cJSON *kind = cJSON_GetObjectItemCaseSensitive(search_id, "kind");

            if (!cJSON_IsString(kind) ||
                strcmp(kind->valuestring, "youtube#video") != 0) {
                cJSON_DeleteItemFromArray(search_items, item_index);
            }
        }

        youtubeContents->count = (size_t)cJSON_GetArraySize(search_items);
        youtubeContents->items = calloc(youtubeContents->count,
                                         sizeof(*youtubeContents->items));
        if (youtubeContents->items == NULL && youtubeContents->count > 0) {
            cJSON_Delete(search_root);
            free(response.buffer);
            free(youtubeContents);
            return NULL;
        }

        // 再生数、コメント数などの情報を動画objectに格納する
        for (index = 0; index < youtubeContents->count; index++) {
            search_item = cJSON_GetArrayItem(search_items, (int)index);
            cJSON *search_id = cJSON_GetObjectItemCaseSensitive(search_item, "id");
            cJSON *snippet = cJSON_GetObjectItemCaseSensitive(search_item, "snippet");
            cJSON *thumbnails = cJSON_GetObjectItemCaseSensitive(snippet, "thumbnails");
            cJSON *medium = cJSON_GetObjectItemCaseSensitive(thumbnails, "medium");
            cJSON *high = cJSON_GetObjectItemCaseSensitive(thumbnails, "high");
            YouTubeApiContents *item = &youtubeContents->items[index];
            size_t video_length;
            size_t channel_length;

            item->videoId = copy_json_string(search_id, "videoId");
            item->channelId = copy_json_string(snippet, "channelId");
            item->title = copy_json_string(snippet, "title");
            item->channelName = copy_json_string(snippet, "channelTitle");
            item->publishedAt = copy_json_string(snippet, "publishedAt");
            item->imageMiddle = copy_json_string(medium, "url");
            item->imageLarge = copy_json_string(high, "url");

            video_length = strlen(video_ids);
            channel_length = strlen(channel_ids);
            if (item->videoId != NULL && video_length + strlen(item->videoId) + 2 < sizeof(video_ids)) {
                snprintf(video_ids + video_length, sizeof(video_ids) - video_length,
                         "%s%s", video_length == 0 ? "" : ",", item->videoId);
            }
            if (item->channelId != NULL && channel_length + strlen(item->channelId) + 2 < sizeof(channel_ids)) {
                snprintf(channel_ids + channel_length, sizeof(channel_ids) - channel_length,
                         "%s%s", channel_length == 0 ? "" : ",", item->channelId);
            }
        }

        cJSON_Delete(search_root);
        free(response.buffer);
        response.buffer = NULL;

        
        {
            char stats_url[900];
            char content_url[900];
            char channel_url[2000];
            response_buffer stats_response = {0};
            response_buffer content_response = {0};
            response_buffer channel_response = {0};

            // 再生数、コメント数、いいね数を取得するためのAPIリクエストを行う
            snprintf(stats_url, sizeof(stats_url),
                     "%s/videos?part=statistics&id=%s&key=%s&fields=items(id,statistics(viewCount,likeCount,commentCount))",
                     YOUTUBE_API_DOMAIN, video_ids, api_key);
            if (fetch_youtube_api_data("YouTube Statistics", stats_url, &stats_response)) {
                cJSON *root = cJSON_Parse(stats_response.buffer);
                cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "items");
                cJSON *entry;
                cJSON_ArrayForEach(entry, items) {
                    cJSON *id = cJSON_GetObjectItemCaseSensitive(entry, "id");
                    cJSON *statistics = cJSON_GetObjectItemCaseSensitive(entry, "statistics");
                    if (cJSON_IsString(id)) {
                        for (index = 0; index < youtubeContents->count; index++) {
                            if (youtubeContents->items[index].videoId != NULL &&
                                strcmp(youtubeContents->items[index].videoId, id->valuestring) == 0) {
                                youtubeContents->items[index].viewCount = copy_json_number(statistics, "viewCount");
                                youtubeContents->items[index].likeCount = copy_json_number(statistics, "likeCount");
                                youtubeContents->items[index].commentCount = copy_json_number(statistics, "commentCount");
                                break;
                            }
                        }
                    }
                }
                cJSON_Delete(root);
                free(stats_response.buffer);
            }
            // 動画の再生時間を取得するためのAPIリクエストを行う
            snprintf(content_url, sizeof(content_url),
                     "%s/videos?part=contentDetails&id=%s&key=%s&fields=items(id,contentDetails(duration))",
                     YOUTUBE_API_DOMAIN, video_ids, api_key);
            if (fetch_youtube_api_data("YouTube Content Details", content_url, &content_response)) {
                cJSON *root = cJSON_Parse(content_response.buffer);
                cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "items");
                cJSON *entry;
                cJSON_ArrayForEach(entry, items) {
                    cJSON *id = cJSON_GetObjectItemCaseSensitive(entry, "id");
                    cJSON *details = cJSON_GetObjectItemCaseSensitive(entry, "contentDetails");
                    if (cJSON_IsString(id)) {
                        for (index = 0; index < youtubeContents->count; index++) {
                            if (youtubeContents->items[index].videoId != NULL &&
                                strcmp(youtubeContents->items[index].videoId, id->valuestring) == 0) {
                                youtubeContents->items[index].videoTime = copy_json_string(details, "duration");
                                break;
                            }
                        }
                    }
                }
                cJSON_Delete(root);
                free(content_response.buffer);
            }

            // チャンネル登録者数を取得するためのAPIリクエストを行う
            snprintf(channel_url, sizeof(channel_url),
                     "%s/channels?part=statistics&id=%s&key=%s&fields=items(id,statistics(subscriberCount,videoCount,viewCount))",
                     YOUTUBE_API_DOMAIN, channel_ids, api_key);
            if (fetch_youtube_api_data("YouTube Channel Statistics", channel_url, &channel_response)) {
                cJSON *root = cJSON_Parse(channel_response.buffer);
                cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "items");
                cJSON *entry;
                cJSON_ArrayForEach(entry, items) {
                    cJSON *id = cJSON_GetObjectItemCaseSensitive(entry, "id");
                    cJSON *statistics = cJSON_GetObjectItemCaseSensitive(entry, "statistics");
                    if (cJSON_IsString(id)) {
                        for (index = 0; index < youtubeContents->count; index++) {
                            if (youtubeContents->items[index].channelId != NULL &&
                                strcmp(youtubeContents->items[index].channelId, id->valuestring) == 0) {
                                youtubeContents->items[index].subscriberCount = copy_json_number(statistics, "subscriberCount");
                                break;
                            }
                        }
                    }
                }
                cJSON_Delete(root);
                free(channel_response.buffer);
            }
        }
    }

    return youtubeContents;
}

/*
 * まとめた結果を解放する.
 */
void freeYoutubeApiContents(YouTubeApiContentsList *contents)
{
    size_t index;

    if (contents == NULL) {
        return;
    }

    for (index = 0; index < contents->count; index++) {
        free(contents->items[index].videoId);
        free(contents->items[index].channelId);
        free(contents->items[index].title);
        free(contents->items[index].channelName);
        free(contents->items[index].imageLarge);
        free(contents->items[index].imageMiddle);
        free(contents->items[index].publishedAt);
        free(contents->items[index].videoTime);
    }
    free(contents->items);
    free(contents);
}

/*
 * 取得済みの構造体リストを整形済みのJSON文字列へ変換する.
 * 構造体リストの所有権は呼び出し元が保持する.
 */
char *getFormattedYoutubeContents(const YouTubeApiContentsList *contents)
{
    if (contents == NULL) {
        return NULL;
    }

    return build_aggregate_json(contents);
}