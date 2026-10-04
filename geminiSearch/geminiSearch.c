#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#include "geminiSearch.h"

#define GEMINI_MIN_KEYWORD_CHARS 2
#define GEMINI_MAX_RESPONSE_BYTES 1200
// 固定文413 + キーワード64×2 + NUL1 = 542
#define GEMINI_PROMPT_BUFFER_BYTES 560
#define GEMINI_URL \
    "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.5-flash-lite:generateContent"

typedef struct {
    char *data;
    size_t length;
} gemini_buffer;

static size_t write_callback(void *contents, size_t size, size_t count,
                             void *user_data)
{
    gemini_buffer *buffer = user_data;
    size_t bytes = size * count;
    char *grown;

    if (buffer->length + bytes > GEMINI_MAX_RESPONSE_BYTES) {
        return 0;
    }
    grown = realloc(buffer->data, buffer->length + bytes + 1);
    if (grown == NULL) {
        return 0;
    }
    buffer->data = grown;
    memcpy(buffer->data + buffer->length, contents, bytes);
    buffer->length += bytes;
    buffer->data[buffer->length] = '\0';
    return bytes;
}

/* UTF-8の文字数を数える(継続バイトを除く). */
static size_t count_utf8_chars(const char *text)
{
    size_t count = 0;

    for (; *text != '\0'; text++) {
        if (((unsigned char)*text & 0xC0) != 0x80) {
            count++;
        }
    }
    return count;
}

int isGeminiKeywordEligible(const char *keyword)
{
    return keyword != NULL &&
           count_utf8_chars(keyword) >= GEMINI_MIN_KEYWORD_CHARS;
}

/* リクエストボディのJSONを組み立てる. 呼び出し元がfree()する. */
static char *build_request_body(const char *keyword)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *contents = cJSON_AddArrayToObject(root, "contents");
    cJSON *content = cJSON_CreateObject();
    cJSON *parts = cJSON_AddArrayToObject(content, "parts");
    cJSON *part = cJSON_CreateObject();
    cJSON *config = cJSON_AddObjectToObject(root, "generationConfig");
    char prompt[GEMINI_PROMPT_BUFFER_BYTES];
    char *body;

    if (root == NULL || contents == NULL || content == NULL ||
        parts == NULL || part == NULL || config == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(content);
        cJSON_Delete(part);
        return NULL;
    }

    snprintf(prompt, sizeof(prompt),
             "Suggest the top 5 YouTube search autocomplete candidates, ordered by popularity, "
             "for the keyword \"%s\". Every candidate MUST start with the exact keyword string \"%s\" "
             "(prefix match); never return candidates that do not begin with it. "
             "Limit to searches made in Japan. "
             "Each candidate may contain multiple words, and words may overlap between candidates. "
             "Write candidates in Japanese. Return only a JSON array of strings.\n",
             keyword, keyword);

    cJSON_AddStringToObject(part, "text", prompt);
    cJSON_AddItemToArray(parts, part);
    cJSON_AddItemToArray(contents, content);
    cJSON_AddStringToObject(config, "responseMimeType", "application/json");
    cJSON_AddNumberToObject(config, "temperature", 0.0);

    // 文字列の配列(最大5件)にレスポンス形式を固定する
    cJSON *schema = cJSON_AddObjectToObject(config, "responseSchema");
    cJSON *items = schema == NULL ? NULL : cJSON_AddObjectToObject(schema, "items");
    if (schema == NULL || items == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddStringToObject(schema, "type", "ARRAY");
    cJSON_AddNumberToObject(schema, "maxItems", 5);
    cJSON_AddStringToObject(items, "type", "STRING");

    body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return body;
}

/* レスポンスから候補テキスト(JSON配列文字列)を取り出す. */
static char *extract_suggestions(const char *response_json)
{
    cJSON *root = cJSON_Parse(response_json);
    const cJSON *candidates;
    const cJSON *first;
    const cJSON *parts;
    const cJSON *text;
    cJSON *array = NULL;
    char *result = NULL;

    if (root == NULL) {
        return NULL;
    }

    candidates = cJSON_GetObjectItemCaseSensitive(root, "candidates");
    first = cJSON_GetArrayItem(candidates, 0);
    parts = cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(first, "content"), "parts");
    text = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(parts, 0),
                                            "text");

    if (cJSON_IsString(text) && text->valuestring != NULL) {
        array = cJSON_Parse(text->valuestring);
        if (cJSON_IsArray(array)) {
            result = cJSON_PrintUnformatted(array);
        }
    }

    cJSON_Delete(array);
    cJSON_Delete(root);
    return result;
}

/* Gemini APIからYouTube検索候補を取得する */
char *getGeminiSuggestions(const char *keyword)
{
    const char *api_key = getenv("GEMINI_API_KEY");
    gemini_buffer response = {0};
    struct curl_slist *headers = NULL;
    char key_header[80];
    char *body;
    char *result = NULL;
    CURL *curl;
    CURLcode code;
    long status = 0;
    char error_text[CURL_ERROR_SIZE] = {0};

    if (!isGeminiKeywordEligible(keyword)) {
        return NULL;
    }
    if (api_key == NULL || api_key[0] == '\0') {
        fprintf(stderr, "GEMINI_API_KEYが設定されていません\n");
        return NULL;
    }
    if (snprintf(key_header, sizeof(key_header), "x-goog-api-key: %s",
                 api_key) >= (int)sizeof(key_header)) {
        fprintf(stderr, "GEMINI_API_KEYが長すぎます (ヘッダ上限=%zuバイト)\n",
                sizeof(key_header));
        return NULL;
    }

    body = build_request_body(keyword);
    curl = curl_easy_init();
    if (body == NULL || curl == NULL) {
        free(body);
        if (curl != NULL) {
            curl_easy_cleanup(curl);
        }
        return NULL;
    }

    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, key_header);

    curl_easy_setopt(curl, CURLOPT_URL, GEMINI_URL);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    // IPv6経路が詰まる環境でのタイムアウト回避
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_text);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    if (code == CURLE_OK && status == 200 && response.data != NULL) {
        fprintf(stdout, "[Gemini raw response]\n%s\n", response.data);
        result = extract_suggestions(response.data);
        fprintf(stdout, "[Gemini suggestions] keyword=%s result=%s\n", keyword,
                result == NULL ? "(解析失敗)" : result);
        fflush(stdout);
    } else {
        fprintf(stderr, "Gemini API呼び出し失敗 (curl=%d, http=%ld): %s\n",
                (int)code, status, error_text);
        if (response.data != NULL) {
            fprintf(stderr, "%s\n", response.data);
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(body);
    free(response.data);
    return result;
}
