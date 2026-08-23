#ifndef SEARCH_H
#define SEARCH_H

/*
 * 検索して、messageとして返すためのsearch.cファイルへの橋渡し.
 */
const char *handle_search_request(const char *request);
int replace_message_placeholder(char **body, long *body_length,
								const char *message);

#endif
