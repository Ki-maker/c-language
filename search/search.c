#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "search.h"

/*
 * 検索して、messageとして返す.
 * Returns the message 検索結果, NULL 検索に失敗.
 */
const char *handle_search_request(const char *request)
{
    if (strncmp(request, "GET /search?", strlen("GET /search?")) == 0 ||
        strncmp(request, "GET /search ", strlen("GET /search ")) == 0) {
#ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
#endif
        printf("検索ボタンが押されました\n");
        fflush(stdout);
        return "検索ボタンが押されました";
    }

    return NULL;
}

int replace_message_placeholder(char **body, long *body_length,
                                const char *message)
{
    const char placeholder[] = "{{message}}";
    char *placeholder_position = strstr(*body, placeholder);

    if (placeholder_position == NULL) {
        return 1;
    }

    // html用のメモリ設定をリセットする
    size_t prefix_length = (size_t)(placeholder_position - *body);
    size_t suffix_length = strlen(placeholder_position + strlen(placeholder));
    size_t message_length = strlen(message);
    char *updated_body = malloc(prefix_length + message_length +
                                 suffix_length + 1);
    if (updated_body == NULL) {
        return 0;
    }

    memcpy(updated_body, *body, prefix_length);
    memcpy(updated_body + prefix_length, message, message_length);
    memcpy(updated_body + prefix_length + message_length,
           placeholder_position + strlen(placeholder), suffix_length + 1);

    free(*body);
    *body = updated_body;
    *body_length = (long)(prefix_length + message_length + suffix_length);
    return 1;
}
