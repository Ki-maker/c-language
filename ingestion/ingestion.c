#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <windows.h>
#include <bcrypt.h>

#include <curl/curl.h>
#include <cjson/cJSON.h>

#include "ingestion.h"

#define SHA256_SIZE 32
#define SHA256_HEX_SIZE 65

typedef struct {
    char *data;
    size_t length;
} ResponseBuffer;

typedef struct {
    char *access_key;
    char *secret_key;
    char *session_token;
} AwsCredentials;

static size_t capture_response(void *data, size_t size, size_t count,
                               void *user_data)
{
    ResponseBuffer *response = user_data;
    size_t bytes;
    char *resized;

    if (size != 0 && count > SIZE_MAX / size) {
        return 0;
    }
    bytes = size * count;
    if (bytes > SIZE_MAX - response->length - 1) {
        return 0;
    }
    resized = realloc(response->data, response->length + bytes + 1);
    if (resized == NULL) {
        return 0;
    }
    memcpy(resized + response->length, data, bytes);
    response->length += bytes;
    resized[response->length] = '\0';
    response->data = resized;
    return bytes;
}

static void clear_credentials(AwsCredentials *credentials)
{
    free(credentials->access_key);
    free(credentials->secret_key);
    free(credentials->session_token);
    memset(credentials, 0, sizeof(*credentials));
}

static int load_ecs_credentials(AwsCredentials *credentials)
{
    const char *full_uri = getenv("AWS_CONTAINER_CREDENTIALS_FULL_URI");
    const char *relative_uri = getenv("AWS_CONTAINER_CREDENTIALS_RELATIVE_URI");
    const char *authorization_token = getenv("AWS_CONTAINER_AUTHORIZATION_TOKEN");
    char *url = NULL;
    char *authorization_header = NULL;
    struct curl_slist *headers = NULL;
    CURL *curl = NULL;
    CURLcode curl_result;
    long http_status = 0;
    ResponseBuffer response = {NULL, 0};
    cJSON *root = NULL;
    const cJSON *access_key;
    const cJSON *secret_key;
    const cJSON *session_token;
    int result = -1;

    if (full_uri != NULL && full_uri[0] != '\0') {
        url = _strdup(full_uri);
    } else if (relative_uri != NULL && relative_uri[0] == '/') {
        size_t url_length = strlen("http://169.254.170.2") + strlen(relative_uri) + 1;
        url = malloc(url_length);
        if (url != NULL) {
            snprintf(url, url_length, "http://169.254.170.2%s", relative_uri);
        }
    } else {
        return 0;
    }
    if (url == NULL) {
        return -1;
    }

    curl = curl_easy_init();
    if (curl == NULL) {
        goto cleanup;
    }
    if (authorization_token != NULL && authorization_token[0] != '\0') {
        size_t header_length = strlen(authorization_token) + sizeof("Authorization: ");
        authorization_header = malloc(header_length);
        if (authorization_header == NULL) {
            goto cleanup;
        }
        snprintf(authorization_header, header_length, "Authorization: %s",
                 authorization_token);
        headers = curl_slist_append(headers, authorization_header);
        if (headers == NULL) {
            goto cleanup;
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, capture_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_result = curl_easy_perform(curl);
    if (curl_result != CURLE_OK ||
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status) != CURLE_OK ||
        http_status < 200 || http_status >= 300 || response.data == NULL) {
        goto cleanup;
    }

    root = cJSON_Parse(response.data);
    access_key = cJSON_GetObjectItemCaseSensitive(root, "AccessKeyId");
    secret_key = cJSON_GetObjectItemCaseSensitive(root, "SecretAccessKey");
    session_token = cJSON_GetObjectItemCaseSensitive(root, "Token");
    if (!cJSON_IsString(access_key) || access_key->valuestring == NULL ||
        !cJSON_IsString(secret_key) || secret_key->valuestring == NULL ||
        !cJSON_IsString(session_token) || session_token->valuestring == NULL) {
        goto cleanup;
    }

    credentials->access_key = _strdup(access_key->valuestring);
    credentials->secret_key = _strdup(secret_key->valuestring);
    credentials->session_token = _strdup(session_token->valuestring);
    if (credentials->access_key == NULL || credentials->secret_key == NULL ||
        credentials->session_token == NULL) {
        clear_credentials(credentials);
        goto cleanup;
    }
    result = 1;

cleanup:
    cJSON_Delete(root);
    if (curl != NULL) {
        curl_easy_cleanup(curl);
    }
    curl_slist_free_all(headers);
    free(authorization_header);
    free(response.data);
    free(url);
    return result;
}

static int resolve_credentials(AwsCredentials *credentials)
{
    const char *access_key = getenv("AWS_ACCESS_KEY_ID");
    const char *secret_key = getenv("AWS_SECRET_ACCESS_KEY");
    const char *session_token = getenv("AWS_SESSION_TOKEN");
    int has_access_key = access_key != NULL && access_key[0] != '\0';
    int has_secret_key = secret_key != NULL && secret_key[0] != '\0';

    if (has_access_key != has_secret_key) {
        fprintf(stderr, "AWS_ACCESS_KEY_IDとAWS_SECRET_ACCESS_KEYの両方を設定してください\n");
        return -1;
    }
    if (has_access_key) {
        credentials->access_key = _strdup(access_key);
        credentials->secret_key = _strdup(secret_key);
        credentials->session_token = session_token != NULL && session_token[0] != '\0'
                                         ? _strdup(session_token)
                                         : NULL;
        if (credentials->access_key == NULL || credentials->secret_key == NULL ||
            (session_token != NULL && session_token[0] != '\0' &&
             credentials->session_token == NULL)) {
            clear_credentials(credentials);
            return -1;
        }
        return 1;
    }
    return load_ecs_credentials(credentials);
}

static int append_bytes(char **buffer, size_t *length,
                        const char *value, size_t value_length)
{
    char *resized;

    if (value_length > SIZE_MAX - *length - 1) {
        return 0;
    }
    resized = realloc(*buffer, *length + value_length + 1);
    if (resized == NULL) {
        return 0;
    }
    memcpy(resized + *length, value, value_length);
    *length += value_length;
    resized[*length] = '\0';
    *buffer = resized;
    return 1;
}

static int append_text(char **buffer, size_t *length, const char *value)
{
    return append_bytes(buffer, length, value, strlen(value));
}

static int parse_video_duration_seconds(const char *duration, long long *seconds)
{
    const char *cursor = duration;
    long long total = 0;
    int in_time = 0;
    int has_component = 0;

    if (duration == NULL || duration[0] != 'P' || seconds == NULL) {
        return 0;
    }
    cursor++;

    while (*cursor != '\0') {
        unsigned long long value = 0;
        long long multiplier;
        int has_digit = 0;
        char unit;

        if (*cursor == 'T' && !in_time) {
            in_time = 1;
            cursor++;
            continue;
        }
        while (*cursor >= '0' && *cursor <= '9') {
            unsigned int digit = (unsigned int)(*cursor - '0');
            if (value > ((unsigned long long)LLONG_MAX - digit) / 10) {
                return 0;
            }
            value = value * 10 + digit;
            has_digit = 1;
            cursor++;
        }
        if (!has_digit || *cursor == '\0') {
            return 0;
        }

        unit = *cursor++;
        if (unit == 'D' && !in_time) {
            multiplier = 86400;
        } else if (unit == 'W' && !in_time) {
            multiplier = 604800;
        } else if (unit == 'H' && in_time) {
            multiplier = 3600;
        } else if (unit == 'M' && in_time) {
            multiplier = 60;
        } else if (unit == 'S' && in_time) {
            multiplier = 1;
        } else {
            return 0;
        }

        if (value > ((unsigned long long)LLONG_MAX - (unsigned long long)total) /
                        (unsigned long long)multiplier) {
            return 0;
        }
        total += (long long)(value * (unsigned long long)multiplier);
        has_component = 1;
    }

    if (!has_component) {
        return 0;
    }
    *seconds = total;
    return 1;
}

static int add_video_fields(cJSON *object, const YouTubeApiContents *video)
{
    cJSON *image = cJSON_CreateObject();
    long long video_time_seconds = 0;
    int has_video_time = video->videoTime != NULL && video->videoTime[0] != '\0';

    if (has_video_time &&
        !parse_video_duration_seconds(video->videoTime, &video_time_seconds)) {
        cJSON_Delete(image);
        return 0;
    }

    if (image == NULL ||
        cJSON_AddStringToObject(object, "videoId", video->videoId == NULL ? "" : video->videoId) == NULL ||
        cJSON_AddStringToObject(object, "channelId", video->channelId == NULL ? "" : video->channelId) == NULL ||
        cJSON_AddStringToObject(object, "title", video->title == NULL ? "" : video->title) == NULL ||
        cJSON_AddStringToObject(object, "channelName", video->channelName == NULL ? "" : video->channelName) == NULL ||
        cJSON_AddStringToObject(image, "large", video->imageLarge == NULL ? "" : video->imageLarge) == NULL ||
        cJSON_AddStringToObject(image, "middle", video->imageMiddle == NULL ? "" : video->imageMiddle) == NULL) {
        cJSON_Delete(image);
        return 0;
    }

    cJSON_AddItemToObject(object, "image", image);
    return cJSON_AddStringToObject(object, "publishedAt", video->publishedAt == NULL ? "" : video->publishedAt) != NULL &&
           cJSON_AddNumberToObject(object, "viewCount", (double)video->viewCount) != NULL &&
           cJSON_AddNumberToObject(object, "likeCount", (double)video->likeCount) != NULL &&
           cJSON_AddNumberToObject(object, "commentCount", (double)video->commentCount) != NULL &&
           (has_video_time
                ? cJSON_AddNumberToObject(object, "videoTime", (double)video_time_seconds) != NULL
                : cJSON_AddNullToObject(object, "videoTime") != NULL) &&
           cJSON_AddNumberToObject(object, "subscriberCount", (double)video->subscriberCount) != NULL;
}

static void print_bulk_item_errors(const cJSON *bulk_response)
{
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(bulk_response, "items");
    const cJSON *item;

    cJSON_ArrayForEach(item, items) {
        const cJSON *operation = item->child;
        const cJSON *document_id;
        const cJSON *status;
        const cJSON *error;
        const cJSON *reason;

        if (operation == NULL) {
            continue;
        }
        error = cJSON_GetObjectItemCaseSensitive(operation, "error");
        if (error == NULL) {
            continue;
        }
        document_id = cJSON_GetObjectItemCaseSensitive(operation, "_id");
        status = cJSON_GetObjectItemCaseSensitive(operation, "status");
        reason = cJSON_GetObjectItemCaseSensitive(error, "reason");
        fprintf(stderr, "Bulk登録失敗: id=%s status=%d reason=%s\n",
                cJSON_IsString(document_id) && document_id->valuestring != NULL
                    ? document_id->valuestring : "(unknown)",
                cJSON_IsNumber(status) ? status->valueint : 0,
                cJSON_IsString(reason) && reason->valuestring != NULL
                    ? reason->valuestring : "(詳細なし)");
    }
}

static char *build_bulk_body(const YouTubeApiContentsList *contents,
                             const char *index_name)
{
    char *body = NULL;
    size_t body_length = 0;
    size_t index;

    if (contents->count > 0 && contents->items == NULL) {
        return NULL;
    }

    for (index = 0; index < contents->count; index++) {
        const YouTubeApiContents *video = &contents->items[index];
        cJSON *action = cJSON_CreateObject();
        cJSON *metadata = cJSON_CreateObject();
        cJSON *document = cJSON_CreateObject();
        char *action_json = NULL;
        char *document_json = NULL;
        int appended;

        if (action == NULL || metadata == NULL || document == NULL ||
            cJSON_AddStringToObject(metadata, "_index", index_name) == NULL ||
            (video->videoId != NULL && video->videoId[0] != '\0' &&
             cJSON_AddStringToObject(metadata, "_id", video->videoId) == NULL) ||
            !add_video_fields(document, video)) {
            cJSON_Delete(action);
            cJSON_Delete(metadata);
            cJSON_Delete(document);
            free(body);
            return NULL;
        }

        cJSON_AddItemToObject(action, "index", metadata);
        action_json = cJSON_PrintUnformatted(action);
        document_json = cJSON_PrintUnformatted(document);
        cJSON_Delete(action);
        cJSON_Delete(document);
        if (action_json == NULL || document_json == NULL) {
            free(action_json);
            free(document_json);
            free(body);
            return NULL;
        }

        appended = append_text(&body, &body_length, action_json) &&
                   append_text(&body, &body_length, "\n") &&
                   append_text(&body, &body_length, document_json) &&
                   append_text(&body, &body_length, "\n");
        free(action_json);
        free(document_json);
        if (!appended) {
            free(body);
            return NULL;
        }
    }

    if (body == NULL) {
        body = calloc(1, 1);
    }
    return body;
}

static int calculate_sha256(const unsigned char *data, size_t data_length,
                            unsigned char output[SHA256_SIZE], int use_hmac,
                            const unsigned char *key, size_t key_length)
{
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    unsigned char *hash_object = NULL;
    DWORD object_length = 0;
    DWORD result_length = 0;
    NTSTATUS status;
    int success = 0;

    if (data_length > ULONG_MAX || key_length > ULONG_MAX ||
        (use_hmac && key == NULL)) {
        return 0;
    }

    status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                         NULL, use_hmac ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0);
    if (status < 0) {
        goto cleanup;
    }
    status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                               (PUCHAR)&object_length, sizeof(object_length),
                               &result_length, 0);
    if (status < 0) {
        goto cleanup;
    }
    hash_object = malloc(object_length);
    if (hash_object == NULL) {
        goto cleanup;
    }
    status = BCryptCreateHash(algorithm, &hash, hash_object, object_length,
                              use_hmac ? (PUCHAR)key : NULL,
                              use_hmac ? (ULONG)key_length : 0, 0);
    if (status < 0) {
        goto cleanup;
    }
    status = BCryptHashData(hash, (PUCHAR)data, (ULONG)data_length, 0);
    if (status < 0) {
        goto cleanup;
    }
    status = BCryptFinishHash(hash, output, SHA256_SIZE, 0);
    success = status >= 0;

cleanup:
    if (hash != NULL) {
        BCryptDestroyHash(hash);
    }
    if (algorithm != NULL) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    free(hash_object);
    return success;
}

static void encode_hex(const unsigned char *data, size_t data_length,
                       char *hex_output)
{
    static const char digits[] = "0123456789abcdef";
    size_t index;

    for (index = 0; index < data_length; index++) {
        hex_output[index * 2] = digits[data[index] >> 4];
        hex_output[index * 2 + 1] = digits[data[index] & 0x0f];
    }
    hex_output[data_length * 2] = '\0';
}

static int sha256_hex(const char *value, size_t value_length,
                      char output[SHA256_HEX_SIZE])
{
    unsigned char digest[SHA256_SIZE];

    if (!calculate_sha256((const unsigned char *)value, value_length,
                          digest, 0, NULL, 0)) {
        return 0;
    }
    encode_hex(digest, sizeof(digest), output);
    return 1;
}

static int hmac_sha256(const unsigned char *key, size_t key_length,
                       const char *value, size_t value_length,
                       unsigned char output[SHA256_SIZE])
{
    return calculate_sha256((const unsigned char *)value, value_length,
                            output, 1, key, key_length);
}

static void print_redacted_response(const char *response)
{
    cJSON *root = cJSON_Parse(response);
    const cJSON *message = cJSON_GetObjectItemCaseSensitive(root, "message");
    const char *text = cJSON_IsString(message) && message->valuestring != NULL
                           ? message->valuestring
                           : response;
    const char *token_header = strstr(text, "x-amz-security-token:");
    const char *line_end;

    if (token_header == NULL) {
        fprintf(stderr, "OpenSearchの応答: %s\n", text);
    } else {
        fwrite(text, 1, (size_t)(token_header - text), stderr);
        fputs("x-amz-security-token: <redacted>\n", stderr);
        line_end = strchr(token_header, '\n');
        if (line_end != NULL) {
            fputs(line_end + 1, stderr);
        }
    }

    if (root != NULL) {
        cJSON_Delete(root);
    }
}

static char *build_authorization(const char *host, const char *uri,
                                 const char *body, size_t body_length,
                                 const char *access_key, const char *secret_key,
                                 const char *region, const char *service,
                                 const char *session_token,
                                 const char *timestamp, const char *date,
                                 char canonical_hash_output[SHA256_HEX_SIZE])
{
    unsigned char date_key[SHA256_SIZE];
    unsigned char region_key[SHA256_SIZE];
    unsigned char service_key[SHA256_SIZE];
    unsigned char signing_key[SHA256_SIZE];
    unsigned char signature_bytes[SHA256_SIZE];
    char payload_hash[SHA256_HEX_SIZE];
    char canonical_hash[SHA256_HEX_SIZE];
    char signature[SHA256_HEX_SIZE];
    char *secret_prefix = NULL;
    char *canonical_headers = NULL;
    char *canonical_request = NULL;
    char *scope = NULL;
    char *string_to_sign = NULL;
    char *authorization = NULL;
    const char *signed_headers;
    int length;

    if (!sha256_hex(body, body_length, payload_hash)) {
        goto cleanup;
    }
    signed_headers = session_token != NULL && session_token[0] != '\0'
                         ? "host;x-amz-content-sha256;x-amz-date;x-amz-security-token"
                         : "host;x-amz-content-sha256;x-amz-date";

    length = snprintf(NULL, 0,
                      session_token != NULL && session_token[0] != '\0'
                          ? "host:%s\nx-amz-content-sha256:%s\nx-amz-date:%s\nx-amz-security-token:%s\n"
                          : "host:%s\nx-amz-content-sha256:%s\nx-amz-date:%s\n",
                      host, payload_hash, timestamp,
                      session_token != NULL && session_token[0] != '\0' ? session_token : "");
    canonical_headers = malloc((size_t)length + 1);
    if (canonical_headers == NULL) {
        goto cleanup;
    }
    if (session_token != NULL && session_token[0] != '\0') {
        snprintf(canonical_headers, (size_t)length + 1,
                 "host:%s\nx-amz-content-sha256:%s\nx-amz-date:%s\nx-amz-security-token:%s\n",
                 host, payload_hash, timestamp, session_token);
    } else {
        snprintf(canonical_headers, (size_t)length + 1,
                 "host:%s\nx-amz-content-sha256:%s\nx-amz-date:%s\n",
                 host, payload_hash, timestamp);
    }

    length = snprintf(NULL, 0, "POST\n%s\n\n%s\n%s\n%s",
                      uri, canonical_headers, signed_headers, payload_hash);
    canonical_request = malloc((size_t)length + 1);
    if (canonical_request == NULL) {
        goto cleanup;
    }
    snprintf(canonical_request, (size_t)length + 1, "POST\n%s\n\n%s\n%s\n%s",
             uri, canonical_headers, signed_headers, payload_hash);
    if (!sha256_hex(canonical_request, strlen(canonical_request), canonical_hash)) {
        goto cleanup;
    }
    memcpy(canonical_hash_output, canonical_hash, SHA256_HEX_SIZE);

    length = snprintf(NULL, 0, "%s/%s/%s/aws4_request", date, region, service);
    scope = malloc((size_t)length + 1);
    if (scope == NULL) {
        goto cleanup;
    }
    snprintf(scope, (size_t)length + 1, "%s/%s/%s/aws4_request", date, region, service);
    length = snprintf(NULL, 0, "AWS4-HMAC-SHA256\n%s\n%s\n%s",
                      timestamp, scope, canonical_hash);
    string_to_sign = malloc((size_t)length + 1);
    if (string_to_sign == NULL) {
        goto cleanup;
    }
    snprintf(string_to_sign, (size_t)length + 1,
             "AWS4-HMAC-SHA256\n%s\n%s\n%s", timestamp, scope, canonical_hash);

    secret_prefix = malloc(strlen(secret_key) + 5);
    if (secret_prefix == NULL) {
        goto cleanup;
    }
    snprintf(secret_prefix, strlen(secret_key) + 5, "AWS4%s", secret_key);
    if (!hmac_sha256((const unsigned char *)secret_prefix, strlen(secret_prefix),
                     date, strlen(date), date_key) ||
        !hmac_sha256(date_key, sizeof(date_key), region, strlen(region), region_key) ||
        !hmac_sha256(region_key, sizeof(region_key), service, strlen(service), service_key) ||
        !hmac_sha256(service_key, sizeof(service_key), "aws4_request", 12, signing_key) ||
        !hmac_sha256(signing_key, sizeof(signing_key), string_to_sign,
                     strlen(string_to_sign), signature_bytes)) {
        goto cleanup;
    }
    encode_hex(signature_bytes, sizeof(signature_bytes), signature);

    length = snprintf(NULL, 0, "AWS4-HMAC-SHA256 Credential=%s/%s, SignedHeaders=%s, Signature=%s",
                      access_key, scope, signed_headers, signature);
    authorization = malloc((size_t)length + 1);
    if (authorization != NULL) {
        snprintf(authorization, (size_t)length + 1,
                 "AWS4-HMAC-SHA256 Credential=%s/%s, SignedHeaders=%s, Signature=%s",
                 access_key, scope, signed_headers, signature);
    }

cleanup:
    free(secret_prefix);
    free(canonical_headers);
    free(canonical_request);
    free(scope);
    free(string_to_sign);
    return authorization;
}

static int valid_index_name(const char *name)
{
    const unsigned char *character = (const unsigned char *)name;
    size_t length = strlen(name);

    if (length == 0 || length > 255 || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return 0;
    }
    while (*character != '\0') {
        if (!((*character >= 'a' && *character <= 'z') ||
              (*character >= '0' && *character <= '9') ||
              *character == '-' || *character == '_')) {
            return 0;
        }
        character++;
    }
    return 1;
}

/*
 * APIのデータをOpenSearchに登録する.
 * 1: 成功, 0: 失敗
 */
int setYoutubeContentsToOpenSearch(const YouTubeApiContentsList *contents)
{
    const char *opensearch_url;
    const char *index_name;
    const char *access_key = NULL;
    const char *secret_key = NULL;
    const char *region;
    const char *service;
    const char *session_token = NULL;
    char *request_url = NULL;
    char *host = NULL;
    char *port = NULL;
    char *uri = NULL;
    char *query = NULL;
    char *host_header = NULL;
    char *body = NULL;
    char *authorization = NULL;
    char canonical_hash[SHA256_HEX_SIZE] = "";
    char timestamp[17];
    char date[9];
    SYSTEMTIME utc_time;
    struct tm utc_tm;
    CURLU *url_parts = NULL;
    struct curl_slist *headers = NULL;
    CURL *curl = NULL;
    CURLcode curl_result = CURLE_FAILED_INIT;
    long http_status = 0;
    ResponseBuffer response = {NULL, 0};
    AwsCredentials credentials = {NULL, NULL, NULL};
    size_t base_length;
    int credential_status;
    int success = 0;

    if (contents == NULL) {
        return 0;
    }

    opensearch_url = getenv("OPENSEARCH_URL");
    if (opensearch_url == NULL || opensearch_url[0] == '\0') {
        fprintf(stderr, "OPENSEARCH_URLが設定されていません\n");
        return 0;
    }
    index_name = getenv("OPENSEARCH_INDEX");
    if (index_name == NULL || index_name[0] == '\0') {
        index_name = "youtube_search";
    }
    if (!valid_index_name(index_name)) {
        fprintf(stderr, "OPENSEARCH_INDEXは小文字英数字、ハイフン、アンダースコアで指定してください\n");
        return 0;
    }

    region = getenv("AWS_REGION");
    if (region == NULL || region[0] == '\0') {
        region = getenv("AWS_DEFAULT_REGION");
    }
    service = "es";
    printf("OpenSearchにデータを登録します: %zu件\n", contents->count);
    if (contents->count == 0) {
        return 1;
    }
    credential_status = resolve_credentials(&credentials);
    if (credential_status < 0) {
        fprintf(stderr, "AWS認証情報を取得できませんでした\n");
        goto cleanup;
    }
    access_key = credentials.access_key;
    secret_key = credentials.secret_key;
    session_token = credentials.session_token;
    if (credential_status > 0 && (region == NULL || region[0] == '\0')) {
        fprintf(stderr, "SigV4署名にはAWS_REGIONが必要です\n");
        goto cleanup;
    }

    base_length = strlen(opensearch_url);
    while (base_length > 0 && opensearch_url[base_length - 1] == '/') {
        base_length--;
    }
    request_url = malloc(base_length + strlen(index_name) + sizeof("//_bulk"));
    if (request_url == NULL) {
        goto cleanup;
    }
    snprintf(request_url, base_length + strlen(index_name) + sizeof("//_bulk"),
             "%.*s/%s/_bulk", (int)base_length, opensearch_url, index_name);
    body = build_bulk_body(contents, index_name);
    if (body == NULL) {
        fprintf(stderr, "OpenSearch用リクエスト本文の作成に失敗しました\n");
        goto cleanup;
    }

    url_parts = curl_url();
    if (url_parts == NULL || curl_url_set(url_parts, CURLUPART_URL, request_url, 0) != CURLUE_OK ||
        curl_url_get(url_parts, CURLUPART_HOST, &host, 0) != CURLUE_OK ||
        curl_url_get(url_parts, CURLUPART_PATH, &uri, 0) != CURLUE_OK) {
        fprintf(stderr, "OPENSEARCH_URLが正しいURLではありません\n");
        goto cleanup;
    }
    if (curl_url_get(url_parts, CURLUPART_QUERY, &query, 0) == CURLUE_OK &&
        query != NULL && query[0] != '\0') {
        fprintf(stderr, "OPENSEARCH_URLにクエリ文字列は指定できません\n");
        goto cleanup;
    }
    if (curl_url_get(url_parts, CURLUPART_PORT, &port, 0) == CURLUE_OK) {
        size_t host_length = strlen(host) + strlen(port) + 2;
        host_header = malloc(host_length);
        if (host_header != NULL) {
            snprintf(host_header, host_length, "%s:%s", host, port);
        }
    } else {
        host_header = _strdup(host);
    }
    if (host_header == NULL) {
        goto cleanup;
    }

    if (access_key != NULL && access_key[0] != '\0') {
        GetSystemTime(&utc_time);
        memset(&utc_tm, 0, sizeof(utc_tm));
        utc_tm.tm_year = (int)utc_time.wYear - 1900;
        utc_tm.tm_mon = (int)utc_time.wMonth - 1;
        utc_tm.tm_mday = (int)utc_time.wDay;
        utc_tm.tm_hour = (int)utc_time.wHour;
        utc_tm.tm_min = (int)utc_time.wMinute;
        utc_tm.tm_sec = (int)utc_time.wSecond;
        if (strftime(timestamp, sizeof(timestamp), "%Y%m%dT%H%M%SZ", &utc_tm) != 16) {
            fprintf(stderr, "署名用のUTC時刻を作成できませんでした\n");
            goto cleanup;
        }
        memcpy(date, timestamp, 8);
        date[8] = '\0';
        authorization = build_authorization(host_header, uri, body, strlen(body),
                                            access_key, secret_key, region, service,
                                            session_token, timestamp, date,
                                            canonical_hash);
        if (authorization == NULL) {
            fprintf(stderr, "AWS SigV4署名の計算に失敗しました\n");
            goto cleanup;
        }
    }

    curl = curl_easy_init();
    if (curl == NULL) {
        fprintf(stderr, "libcurlの初期化に失敗しました\n");
        goto cleanup;
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, capture_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_URL, request_url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)strlen(body));
    headers = curl_slist_append(headers, "Content-Type: application/x-ndjson");
    if (headers == NULL) {
        goto cleanup;
    }
    if (authorization != NULL) {
        char *host_header_value = malloc(strlen(host_header) + sizeof("Host: "));
        struct curl_slist *updated_headers;
        if (host_header_value == NULL) {
            goto cleanup;
        }
        sprintf(host_header_value, "Host: %s", host_header);
        updated_headers = curl_slist_append(headers, host_header_value);
        free(host_header_value);
        if (updated_headers == NULL) {
            goto cleanup;
        }
        headers = updated_headers;
        char *header = malloc(strlen(authorization) + sizeof("Authorization: "));
        if (header == NULL) {
            goto cleanup;
        }
        sprintf(header, "Authorization: %s", authorization);
        updated_headers = curl_slist_append(headers, header);
        free(header);
        if (updated_headers == NULL) {
            goto cleanup;
        }
        headers = updated_headers;
        {
            char date_header[64];
            char payload_header[SHA256_HEX_SIZE + 24];
            unsigned char body_digest[SHA256_SIZE];
            char body_hash[SHA256_HEX_SIZE];
            struct curl_slist *updated;
            sprintf(date_header, "x-amz-date: %s", timestamp);
            updated = curl_slist_append(headers, date_header);
            if (updated == NULL) {
                goto cleanup;
            }
            headers = updated;
            if (!calculate_sha256((const unsigned char *)body, strlen(body),
                                  body_digest, 0, NULL, 0)) {
                goto cleanup;
            }
            encode_hex(body_digest, sizeof(body_digest), body_hash);
            sprintf(payload_header, "x-amz-content-sha256: %s", body_hash);
            updated = curl_slist_append(headers, payload_header);
            if (updated == NULL) {
                goto cleanup;
            }
            headers = updated;
            if (session_token != NULL && session_token[0] != '\0') {
                char *token_header = malloc(strlen(session_token) + sizeof("x-amz-security-token: "));
                struct curl_slist *updated;
                if (token_header == NULL) {
                    goto cleanup;
                }
                sprintf(token_header, "x-amz-security-token: %s", session_token);
                updated = curl_slist_append(headers, token_header);
                free(token_header);
                if (updated == NULL) {
                    goto cleanup;
                }
                headers = updated;
            }
        }
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_result = curl_easy_perform(curl);
    if (curl_result == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    }

    if (curl_result != CURLE_OK) {
        fprintf(stderr, "OpenSearchへの接続に失敗しました: %s\n",
                curl_easy_strerror(curl_result));
        goto cleanup;
    }

    if (http_status < 200 || http_status >= 300) {
        fprintf(stderr, "OpenSearchからHTTPステータス %ld が返されました\n",
                http_status);
        if (canonical_hash[0] != '\0') {
            fprintf(stderr, "ローカルCanonical Request SHA256: %s\n", canonical_hash);
        }
        if (response.data != NULL && response.data[0] != '\0') {
            print_redacted_response(response.data);
        }
        goto cleanup;
    }
    if (response.data != NULL) {
        cJSON *bulk_response = cJSON_Parse(response.data);
        const cJSON *errors = cJSON_GetObjectItemCaseSensitive(bulk_response, "errors");
        if (cJSON_IsTrue(errors)) {
            fprintf(stderr, "OpenSearch Bulk APIで一部のデータ登録に失敗しました\n");
            print_bulk_item_errors(bulk_response);
            cJSON_Delete(bulk_response);
            goto cleanup;
        }
        cJSON_Delete(bulk_response);
    }

    printf("OpenSearchへの接続に成功しました\n");
    success = 1;

cleanup:
    if (curl != NULL) {
        curl_easy_cleanup(curl);
    }
    if (headers != NULL) {
        curl_slist_free_all(headers);
    }
    if (url_parts != NULL) {
        curl_url_cleanup(url_parts);
    }
    curl_free(host);
    curl_free(port);
    curl_free(uri);
    curl_free(query);
    free(host_header);
    free(body);
    free(request_url);
    free(authorization);
    free(response.data);
    clear_credentials(&credentials);
    fflush(stdout);
    return success;
}