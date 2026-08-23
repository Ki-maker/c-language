#include <string.h>

#include "router.h"
#include "search.h"

/*
 * HTTPリクエストを適切なファイルとコンテンツタイプへ振り分ける.
 * Returns 1 if the requestが正常終了, 0 そうでない場合.
 */
int route_request(const char *request, const char **file_name,
                  const char **content_type, const char **message)
{
    *file_name = "index.html";
    *content_type = "text/html; charset=UTF-8";
    *message = NULL;

    if (strncmp(request, "GET /style.css", strlen("GET /style.css")) == 0) {
        *file_name = "style.css";
        *content_type = "text/css; charset=UTF-8";
        return 1;
    }

    *message = handle_search_request(request);
    if (*message != NULL) {
        return 1;
    }

    if (strncmp(request, "GET / ", 6) == 0 ||
        strncmp(request, "GET /?", 6) == 0) {
        return 1;
    }

    return 0;
}
