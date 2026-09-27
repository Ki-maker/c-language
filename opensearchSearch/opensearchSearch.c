#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <cjson/cJSON.h>

#include "../opensearchNetwork.h"
#include "opensearchSearch.h"

static const char *get_sort_script(int sort_order)
{
    switch (sort_order) {
    case 2:
        return "if (doc['likeCount'].size() == 0 || doc['viewCount'].size() == 0) { return 0; } return 1.0 * doc['likeCount'].value / doc['viewCount'].value;";
    case 3:
        return "if (doc['commentCount'].size() == 0 || doc['viewCount'].size() == 0) { return 0; } return 1.0 * doc['commentCount'].value / doc['viewCount'].value;";
    case 4:
        return "if (doc['publishedAt'].size() == 0 || doc['viewCount'].size() == 0) { return 0; } return doc['viewCount'].value / (((params['now'] - doc['publishedAt'].value.toInstant().toEpochMilli()) / 86400000.0) + 1.0);";
    case 5:
        // チャンネル登録者数の多い順はスクリプトのクエリの作成が不要
        return NULL;
    case 6:
        return "if (doc['likeCount'].size() == 0 || doc['videoTime'].size() == 0) { return 0; } return 1.0 * doc['likeCount'].value / doc['videoTime'].value;";
    case 7:
        return "if (doc['viewCount'].size() == 0 || doc['subscriberCount'].size() == 0) { return 0; } return 1.0 * doc['viewCount'].value / doc['subscriberCount'].value;";
    case 1:
    default:
        return "if (doc['viewCount'].size() == 0 || doc['likeCount'].size() == 0) { return 0; } return doc['viewCount'].value * doc['likeCount'].value;";
    }
}

static cJSON *copy_json_value_or_null(const cJSON *object, const char *key)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    cJSON *copy = cJSON_Duplicate(value, 1);

    return copy == NULL ? cJSON_CreateNull() : copy;
}

char *searchOpenSearch(const YouTubeApiContentsList *contents, int sort_order)
{
    const char *index_name = getenv("OPENSEARCH_INDEX");
    cJSON *request = NULL;
    cJSON *query = NULL;
    cJSON *terms = NULL;
    cJSON *video_ids = NULL;
    cJSON *sort = NULL;
    cJSON *sort_definition = NULL;
    cJSON *script = NULL;
    cJSON *script_body = NULL;
    cJSON *source_fields = NULL;
    char path[25];
    char *body = NULL;
    char *response = NULL;
    char *result = NULL;
    long http_status = 0;
    size_t index;

    printf("OpenSearchへの検索を開始します（videoId: %zu件、並び順: %d）\n",
           contents == NULL ? 0 : contents->count, sort_order);
    if (contents == NULL || contents->count == 0 || contents->items == NULL ||
        sort_order < 1 || sort_order > 7) {
        return NULL;
    }
    if (index_name == NULL || index_name[0] == '\0') index_name = "youtube_search";
    if (snprintf(path, sizeof(path), "/%s/_search", index_name) >= (int)sizeof(path)) return NULL;

    // cJSONが自動的にメモリ確保を行うため、明示不要
    // 確保メモリ = cJSONノード本体 + キー文字列 + 値文字列 + 子ノード + 配列要素
    request = cJSON_CreateObject();
    query = cJSON_CreateObject();
    terms = cJSON_CreateObject();
    video_ids = cJSON_CreateArray();
    sort = cJSON_CreateArray();
    sort_definition = cJSON_CreateObject();
    source_fields = cJSON_CreateArray();
    if (request == NULL || query == NULL || terms == NULL || video_ids == NULL ||
        sort == NULL || sort_definition == NULL || source_fields == NULL) goto cleanup;
    for (index = 0; index < contents->count; index++) {
        const char *video_id = contents->items[index].videoId;
        if (video_id != NULL && video_id[0] != '\0') {
            cJSON *video_id_json = cJSON_CreateString(video_id);
            if (video_id_json == NULL) goto cleanup;
            cJSON_AddItemToArray(video_ids, video_id_json);
        }
    }
    cJSON_AddItemToObject(terms, "_id", video_ids);
    cJSON_AddItemToObject(query, "terms", terms);
    // チャンネル登録者数の多い順
    if (sort_order == 5) {
        cJSON *subscriber_sort = cJSON_CreateObject();
        cJSON *view_sort = cJSON_CreateObject();
        if (subscriber_sort == NULL || view_sort == NULL ||
            cJSON_AddStringToObject(subscriber_sort, "order", "desc") == NULL ||
            cJSON_AddNumberToObject(subscriber_sort, "missing", 0) == NULL ||
            cJSON_AddStringToObject(view_sort, "order", "desc") == NULL ||
            cJSON_AddNumberToObject(view_sort, "missing", 0) == NULL) {
            cJSON_Delete(subscriber_sort);
            cJSON_Delete(view_sort);
            goto cleanup;
        }
        cJSON_AddItemToObject(sort_definition, "subscriberCount", subscriber_sort);
        cJSON_AddItemToArray(sort, sort_definition);
        sort_definition = cJSON_CreateObject();
        if (sort_definition == NULL) goto cleanup;
        cJSON_AddItemToObject(sort_definition, "viewCount", view_sort);
        cJSON_AddItemToArray(sort, sort_definition);
    } else {
        script = cJSON_CreateObject();
        script_body = cJSON_CreateObject();
        if (script == NULL || script_body == NULL) goto cleanup;
        cJSON_AddStringToObject(script_body, "source", get_sort_script(sort_order));
        cJSON_AddItemToObject(script, "script", script_body);
        cJSON_AddStringToObject(script, "type", "number");
        cJSON_AddStringToObject(script, "order", "desc");
        cJSON_AddItemToObject(sort_definition, "_script", script);
        cJSON_AddItemToArray(sort, sort_definition);
    }
    cJSON_AddItemToObject(request, "query", query);
    cJSON_AddItemToObject(request, "sort", sort);
    cJSON_AddNumberToObject(request, "size", 50);
    cJSON_AddItemToArray(source_fields, cJSON_CreateString("videoId"));
    cJSON_AddItemToArray(source_fields, cJSON_CreateString("image.large"));
    cJSON_AddItemToArray(source_fields, cJSON_CreateString("image.middle"));
    cJSON_AddItemToArray(source_fields, cJSON_CreateString("title"));
    cJSON_AddItemToArray(source_fields, cJSON_CreateString("channelName"));
    cJSON_AddItemToArray(source_fields, cJSON_CreateString("videoTime"));
    cJSON_AddItemToArray(source_fields, cJSON_CreateString("viewCount"));
    cJSON_AddItemToArray(source_fields, cJSON_CreateString("publishedAt"));
    cJSON_AddItemToObject(request, "_source", source_fields);

    // cJSONが自動的にメモリ確保を行うため、明示不要
    // 確保メモリ = cJSONノード本体 + キー文字列 + 値文字列 + 子ノード + 配列要素
    body = cJSON_PrintUnformatted(request);
    if (body != NULL) {
        printf("OpenSearchへ送信する検索クエリ:\n%s\n", body);
    }
    // OpenSearchへ検索リクエストを送信
    if (body == NULL || !executeOpenSearchRequest("POST", path, body,
                                                   "application/json",
                                                   &response, &http_status)) {
        fprintf(stderr, "OpenSearch検索リクエストの送信に失敗しました\n");
        goto cleanup;
    }
    printf("OpenSearch検索のHTTPステータス: %ld\n", http_status);
    if (http_status < 200 || http_status >= 300) {
        fprintf(stderr, "OpenSearch検索エラー応答:\n%s\n",
                response == NULL ? "(空のレスポンス)" : response);
        goto cleanup;
    }
    printf("OpenSearchから取得した検索結果:\n%s\n",
           response == NULL ? "(空のレスポンス)" : response);

    {
        cJSON *response_json = cJSON_Parse(response == NULL ? "" : response);
        const cJSON *hits = cJSON_GetObjectItemCaseSensitive(response_json, "hits");
        const cJSON *hit_items = cJSON_GetObjectItemCaseSensitive(hits, "hits");
        cJSON *source_array = cJSON_CreateArray();
        const cJSON *hit;

        if (!cJSON_IsArray(hit_items) || source_array == NULL) {
            fprintf(stderr, "OpenSearch検索結果にhits.hits配列がありません\n");
            cJSON_Delete(source_array);
            cJSON_Delete(response_json);
            goto cleanup;
        }
        cJSON_ArrayForEach(hit, hit_items) {
            const cJSON *source = cJSON_GetObjectItemCaseSensitive(hit, "_source");
            const cJSON *image;
            cJSON *selected_source;
            cJSON *selected_image;

            if (cJSON_IsObject(source)) {
                image = cJSON_GetObjectItemCaseSensitive(source, "image");
                selected_source = cJSON_CreateObject();
                selected_image = cJSON_CreateObject();
                if (selected_source == NULL || selected_image == NULL) {
                    cJSON_Delete(selected_source);
                    cJSON_Delete(selected_image);
                    cJSON_Delete(source_array);
                    cJSON_Delete(response_json);
                    goto cleanup;
                }
                if (cJSON_IsObject(image)) {
                    const cJSON *large = cJSON_GetObjectItemCaseSensitive(image, "large");
                    const cJSON *middle = cJSON_GetObjectItemCaseSensitive(image, "middle");
                    if (cJSON_IsString(large)) {
                        cJSON_AddStringToObject(selected_image, "large", large->valuestring);
                    }
                    if (cJSON_IsString(middle)) {
                        cJSON_AddStringToObject(selected_image, "middle", middle->valuestring);
                    }
                }
                
                cJSON_AddItemToObject(selected_source, "image", selected_image);
                cJSON_AddItemToObject(selected_source, "videoId",
                                      copy_json_value_or_null(source, "videoId"));
                cJSON_AddItemToObject(selected_source, "title",
                                      copy_json_value_or_null(source, "title"));
                cJSON_AddItemToObject(selected_source, "channelName",
                                      copy_json_value_or_null(source, "channelName"));
                cJSON_AddItemToObject(selected_source, "videoTime",
                                      copy_json_value_or_null(source, "videoTime"));
                cJSON_AddItemToObject(selected_source, "viewCount",
                                      copy_json_value_or_null(source, "viewCount"));
                cJSON_AddItemToObject(selected_source, "publishedAt",
                                      copy_json_value_or_null(source, "publishedAt"));
                cJSON_AddItemToArray(source_array, selected_source);
            }
        }
        result = cJSON_PrintUnformatted(source_array);
        cJSON_Delete(source_array);
        cJSON_Delete(response_json);
    }
    if (result == NULL) {
        fprintf(stderr, "OpenSearch検索結果のJSON配列を作成できませんでした\n");
        goto cleanup;
    }
    cJSON_Delete(request);
    free(body);
    free(response);
    return result;

cleanup:
    cJSON_Delete(request);
    free(body);
    free(response);
    free(result);
    return NULL;
}


