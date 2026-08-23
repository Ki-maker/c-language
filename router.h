#ifndef ROUTER_H
#define ROUTER_H

#include "search.h"

/*
 * HTTPリクエストを適切なファイルとコンテンツタイプへ振り分けるためroute.cファイルへの橋渡し.
 */
int route_request(const char *request, const char **file_name,
                  const char **content_type, const char **message);

#endif
