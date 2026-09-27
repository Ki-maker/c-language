#include <stdio.h>
#include <stdlib.h>
#include <cjson/cJSON.h>
#include "opensearchDelete.h"
#include "../opensearchNetwork.h"

/*
 * OpenSearchから期限切れのYouTubeコンテンツを定期削除する関数
 * insertedAtが1分以上前の文書を削除する
 */
int deleteExpiredYoutubeContentsFromOpenSearch(void)
{
    const char *index_name = getenv("OPENSEARCH_INDEX");
    char path[512];
    char *response = NULL;
    long http_status = 0;
    cJSON *root = NULL;
    const cJSON *deleted, *failures, *timed_out;
    int success = 0;

    if (index_name == NULL || index_name[0] == '\0') index_name = "youtube_search";
    if (snprintf(path, sizeof(path), "/%s/_delete_by_query", index_name) >= (int)sizeof(path)) return 0;
    // deleteのクエリの作成し、OpenSearchに送信する
    if (!executeOpenSearchRequest("POST", path,
        "{\"query\":{\"range\":{\"insertedAt\":{\"lt\":\"now-1m\"}}}}",
        "application/json", &response, &http_status)) goto cleanup;
    if (http_status < 200 || http_status >= 300) goto cleanup;
    root = cJSON_Parse(response == NULL ? "" : response);
    if (!cJSON_IsObject(root)) goto cleanup;
    deleted = cJSON_GetObjectItemCaseSensitive(root, "deleted");
    failures = cJSON_GetObjectItemCaseSensitive(root, "failures");
    timed_out = cJSON_GetObjectItemCaseSensitive(root, "timed_out");
    if (!cJSON_IsArray(failures) || cJSON_IsTrue(timed_out) || cJSON_GetArraySize(failures) > 0) goto cleanup;
    printf("定期削除: insertedAtが1分以上前の文書を%d件削除しました\n",
           cJSON_IsNumber(deleted) ? deleted->valueint : 0);
    success = 1;
cleanup:
    cJSON_Delete(root); free(response); return success;
}