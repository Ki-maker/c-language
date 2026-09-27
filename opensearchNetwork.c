#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include <windows.h>
#include <bcrypt.h>
#include <curl/curl.h>

#include "opensearchNetwork.h"

#define SHA256_SIZE 32
#define SHA256_HEX_SIZE 65

typedef struct { char *data; size_t length; } ResponseBuffer;

static size_t capture_response(void *data, size_t size, size_t count, void *user_data)
{
    ResponseBuffer *response = user_data;
    size_t bytes = size * count;
    char *resized = realloc(response->data, response->length + bytes + 1);
    if (resized == NULL) return 0;
    memcpy(resized + response->length, data, bytes);
    response->length += bytes;
    resized[response->length] = '\0';
    response->data = resized;
    return bytes;
}

static int sha256(const unsigned char *data, size_t length,
                  unsigned char output[SHA256_SIZE], int hmac,
                  const unsigned char *key, size_t key_length)
{
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    unsigned char *object = NULL;
    DWORD object_length = 0, result_length = 0;
    NTSTATUS status;
    int success = 0;

    status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL,
                                         hmac ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0);
    if (status < 0 || BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
        (PUCHAR)&object_length, sizeof(object_length), &result_length, 0) < 0) goto cleanup;
    object = malloc(object_length);
    if (object == NULL || BCryptCreateHash(algorithm, &hash, object, object_length,
        hmac ? (PUCHAR)key : NULL, hmac ? (ULONG)key_length : 0, 0) < 0 ||
        BCryptHashData(hash, (PUCHAR)data, (ULONG)length, 0) < 0 ||
        BCryptFinishHash(hash, output, SHA256_SIZE, 0) < 0) goto cleanup;
    success = 1;
cleanup:
    if (hash != NULL) BCryptDestroyHash(hash);
    if (algorithm != NULL) BCryptCloseAlgorithmProvider(algorithm, 0);
    free(object);
    return success;
}

static void hex_encode(const unsigned char *data, size_t length, char *output)
{
    static const char digits[] = "0123456789abcdef";
    size_t index;
    for (index = 0; index < length; index++) {
        output[index * 2] = digits[data[index] >> 4];
        output[index * 2 + 1] = digits[data[index] & 0x0f];
    }
    output[length * 2] = '\0';
}

static int sha256_hex(const char *data, char output[SHA256_HEX_SIZE])
{
    unsigned char digest[SHA256_SIZE];
    if (!sha256((const unsigned char *)data, strlen(data), digest, 0, NULL, 0)) return 0;
    hex_encode(digest, sizeof(digest), output);
    return 1;
}

static int hmac(const unsigned char *key, size_t key_length, const char *data,
                unsigned char output[SHA256_SIZE])
{
    return sha256((const unsigned char *)data, strlen(data), output, 1, key, key_length);
}

static char *make_authorization(const char *method, const char *host, const char *uri,
                                const char *body, const char *access_key,
                                const char *secret_key, const char *region,
                                const char *service, const char *token,
                                const char *timestamp, const char *date)
{
    char payload_hash[SHA256_HEX_SIZE], canonical_hash[SHA256_HEX_SIZE];
    char canonical_headers[2048], canonical_request[4096], scope[256];
    char string_to_sign[4600], signature[SHA256_HEX_SIZE], secret_prefix[512];
    unsigned char date_key[SHA256_SIZE], region_key[SHA256_SIZE];
    unsigned char service_key[SHA256_SIZE], signing_key[SHA256_SIZE];
    unsigned char signature_bytes[SHA256_SIZE];
    const char *signed_headers = token != NULL && token[0] != '\0'
        ? "host;x-amz-content-sha256;x-amz-date;x-amz-security-token"
        : "host;x-amz-content-sha256;x-amz-date";
    char *authorization;
    int length;

    if (strlen(secret_key) + 5 > sizeof(secret_prefix) || !sha256_hex(body, payload_hash)) return NULL;
    if (token != NULL && token[0] != '\0') {
        snprintf(canonical_headers, sizeof(canonical_headers),
            "host:%s\nx-amz-content-sha256:%s\nx-amz-date:%s\nx-amz-security-token:%s\n",
            host, payload_hash, timestamp, token);
    } else {
        snprintf(canonical_headers, sizeof(canonical_headers),
            "host:%s\nx-amz-content-sha256:%s\nx-amz-date:%s\n",
            host, payload_hash, timestamp);
    }
    snprintf(canonical_request, sizeof(canonical_request), "%s\n%s\n\n%s\n%s\n%s",
             method, uri, canonical_headers, signed_headers, payload_hash);
    if (!sha256_hex(canonical_request, canonical_hash)) return NULL;
    snprintf(scope, sizeof(scope), "%s/%s/%s/aws4_request", date, region, service);
    snprintf(string_to_sign, sizeof(string_to_sign), "AWS4-HMAC-SHA256\n%s\n%s\n%s",
             timestamp, scope, canonical_hash);
    snprintf(secret_prefix, sizeof(secret_prefix), "AWS4%s", secret_key);
    if (!hmac((const unsigned char *)secret_prefix, strlen(secret_prefix), date, date_key) ||
        !hmac(date_key, sizeof(date_key), region, region_key) ||
        !hmac(region_key, sizeof(region_key), service, service_key) ||
        !hmac(service_key, sizeof(service_key), "aws4_request", signing_key) ||
        !hmac(signing_key, sizeof(signing_key), string_to_sign, signature_bytes)) return NULL;
    hex_encode(signature_bytes, sizeof(signature_bytes), signature);
    length = snprintf(NULL, 0, "AWS4-HMAC-SHA256 Credential=%s/%s, SignedHeaders=%s, Signature=%s",
                      access_key, scope, signed_headers, signature);
    authorization = malloc((size_t)length + 1);
    if (authorization != NULL) snprintf(authorization, (size_t)length + 1,
        "AWS4-HMAC-SHA256 Credential=%s/%s, SignedHeaders=%s, Signature=%s",
        access_key, scope, signed_headers, signature);
    return authorization;
}

int executeOpenSearchRequest(const char *method, const char *path, const char *body,
                             const char *content_type, char **response_body, long *http_status)
{
    const char *base_url = getenv("OPENSEARCH_URL");
    const char *access_key = getenv("AWS_ACCESS_KEY_ID");
    const char *secret_key = getenv("AWS_SECRET_ACCESS_KEY");
    const char *region = getenv("AWS_REGION");
    const char *service = getenv("OPENSEARCH_SERVICE");
    const char *token = getenv("AWS_SESSION_TOKEN");
    char *url = NULL, *host = NULL, *uri = NULL, *port = NULL;
    char *host_header = NULL, *authorization = NULL;
    char timestamp[17], date[9];
    CURLU *parts = NULL;
    CURL *curl = NULL;
    struct curl_slist *headers = NULL;
    ResponseBuffer response = {0};
    CURLcode result;
    SYSTEMTIME now;
    struct tm tm_now;
    int signed_request = access_key != NULL && access_key[0] != '\0' &&
                         secret_key != NULL && secret_key[0] != '\0';
    int success = 0;

    if (base_url == NULL || base_url[0] == '\0' || method == NULL || path == NULL ||
        body == NULL || response_body == NULL || http_status == NULL) return 0;
    *response_body = NULL; *http_status = 0;
    if (service == NULL || service[0] == '\0') service = "es";
    {
        size_t length = strlen(base_url);
        while (length > 0 && base_url[length - 1] == '/') length--;
        url = malloc(length + strlen(path) + 1);
        if (url == NULL) goto cleanup;
        snprintf(url, length + strlen(path) + 1, "%.*s%s", (int)length, base_url, path);
    }
    parts = curl_url();
    if (parts == NULL || curl_url_set(parts, CURLUPART_URL, url, 0) != CURLUE_OK ||
        curl_url_get(parts, CURLUPART_HOST, &host, 0) != CURLUE_OK ||
        curl_url_get(parts, CURLUPART_PATH, &uri, 0) != CURLUE_OK) goto cleanup;
    if (curl_url_get(parts, CURLUPART_PORT, &port, 0) == CURLUE_OK) {
        host_header = malloc(strlen(host) + strlen(port) + 2);
        if (host_header != NULL) snprintf(host_header, strlen(host) + strlen(port) + 2, "%s:%s", host, port);
    } else host_header = _strdup(host);
    if (host_header == NULL) goto cleanup;
    if (signed_request) {
        if (region == NULL || region[0] == '\0') goto cleanup;
        GetSystemTime(&now); memset(&tm_now, 0, sizeof(tm_now));
        tm_now.tm_year = now.wYear - 1900; tm_now.tm_mon = now.wMonth - 1;
        tm_now.tm_mday = now.wDay; tm_now.tm_hour = now.wHour; tm_now.tm_min = now.wMinute; tm_now.tm_sec = now.wSecond;
        if (strftime(timestamp, sizeof(timestamp), "%Y%m%dT%H%M%SZ", &tm_now) != 16) goto cleanup;
        memcpy(date, timestamp, 8); date[8] = '\0';
        authorization = make_authorization(method, host_header, uri, body, access_key, secret_key,
                                           region, service, token, timestamp, date);
        if (authorization == NULL) goto cleanup;
    }
    curl = curl_easy_init();
    if (curl == NULL) goto cleanup;
    curl_easy_setopt(curl, CURLOPT_URL, url); curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body); curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)strlen(body));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, capture_response); curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L); curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    {
        char header[4096];
        snprintf(header, sizeof(header), "Content-Type: %s", content_type == NULL ? "application/json" : content_type);
        headers = curl_slist_append(headers, header);
        if (signed_request) {
            snprintf(header, sizeof(header), "Host: %s", host_header); headers = curl_slist_append(headers, header);
            snprintf(header, sizeof(header), "Authorization: %s", authorization); headers = curl_slist_append(headers, header);
            snprintf(header, sizeof(header), "x-amz-date: %s", timestamp); headers = curl_slist_append(headers, header);
            { char hash[SHA256_HEX_SIZE]; if (!sha256_hex(body, hash)) goto cleanup;
              snprintf(header, sizeof(header), "x-amz-content-sha256: %s", hash); headers = curl_slist_append(headers, header); }
            if (token != NULL && token[0] != '\0') { snprintf(header, sizeof(header), "x-amz-security-token: %s", token); headers = curl_slist_append(headers, header); }
        }
    }
    if (headers == NULL) goto cleanup;
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    result = curl_easy_perform(curl);
    if (result != CURLE_OK || curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, http_status) != CURLE_OK) goto cleanup;
    *response_body = response.data; response.data = NULL; success = 1;
cleanup:
    if (curl != NULL) curl_easy_cleanup(curl); curl_slist_free_all(headers);
    if (parts != NULL) curl_url_cleanup(parts); curl_free(host); curl_free(port); curl_free(uri);
    free(host_header); free(url); free(authorization); free(response.data);
    return success;
}