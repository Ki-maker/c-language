#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "search.h"

/*
 * sizeとcountをかけたバイト数を返すコールバック関数.
 * Returns total_size
 */
static size_t write_callback(void *contents, size_t size,
                             size_t count, void *user_data)
{
    // size_tはsize * count分のバイトが入る
    size_t total_size = size * count;

    // コンパイラに使わないことを残している
    // contentsがresponseのデータを指すポインタであることを示すために、user_dataは使わない
    (void)user_data;
    printf("%.*s", (int)total_size, (char *)contents);
    return total_size;
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
        get_searchResults();
        return "検索ボタンが押されました";
    }

    return NULL;
}

int replace_message_placeholder(char **body, long *body_length,
                                const char *message)
{
    const char placeholder[] = "{{message}}";

    // strstr(検索対象の文字列, 探す文字列)
    char *placeholder_position = strstr(*body, placeholder);

    // bodyの中でplaceholderが見つからなかった場合、1を返す
    if (placeholder_position == NULL) {
        return 1;
    }

    // 例えば、*body = "<p>{{message}}</p>";の場合、{{message}}の前の<p>のバイト数(3バイト)となる
    size_t prefix_length = (size_t)(placeholder_position - *body);
    // 例えば、*body = "<p>{{message}}</p>";の場合、{{message}}の後の</p>のバイト数(4バイト)となる
    size_t suffix_length = strlen(placeholder_position + strlen(placeholder));
    // messageの長さ
    size_t message_length = strlen(message);
    // ↑の3つと/0の分のバイト数を確保する
    char *updated_body = malloc(prefix_length + message_length +
                                 suffix_length + 1);
    if (updated_body == NULL) {
        return 0;
    }

    // memcpy(コピー先, コピー元, コピーするバイト数);
    // 例えば、{{ message }}=検索結果であり、*body = "<p>{{message}}</p>";の場合、updated_body = "<p>検索結果</p>";となる
    memcpy(updated_body, *body, prefix_length);
    // messageを、updated_bodyのprefix_lengthバイト後から書き込む
    memcpy(updated_body + prefix_length, message, message_length);
    // messageを、updated_bodyのprefix_length + message_lengthバイト後から書き込む
    memcpy(updated_body + prefix_length + message_length,
           placeholder_position + strlen(placeholder), suffix_length + 1);

    // 旧bodyを解放し、bodyを更新する
    free(*body);
    // 引数に設定している*bodyを更新する
    *body = updated_body;
    // 引数に設定しているbody_lengthを更新する
    *body_length = (long)(prefix_length + message_length + suffix_length);
    return 1;
}

void get_searchResults(void) {
    // 検索結果を取得する処理をここに実装する

    // API呼び出しの初期設定
    // 通信の準備をする関数の設定
    CURL *curl = curl_easy_init(); 
    // APIキーを設定する
    // TODO: シークレットマネージャーから取得するように変更する
    const char *api_key = "AIzaSyByOFozBXw56klm9CuXvBqo2iwzSi6HB8k";
    char *encoded_query;
    char url[1024];

    if (curl == NULL) {
        fprintf(stderr, "curlの初期化に失敗しました\n");
        return;
    }

    if (api_key == NULL || api_key[0] == '\0') {
        fprintf(stderr, "YOUTUBE_API_KEYが設定されていません\n");
        curl_easy_cleanup(curl);
        return;
    }

    // 検索文字列をURLの使える形に変更する
    // 第三引数の0は'\0'まで自動判定
    encoded_query = curl_easy_escape(curl, "劇⽯中毒", 0);
    if (encoded_query == NULL) {
        fprintf(stderr, "検索語のURLエンコードに失敗しました\n");
        curl_easy_cleanup(curl);
        return;
    }

    // URLをセットする
    // snprintf(書き込み先, 最大サイズ, 書式, 値1, 値2);
    snprintf(url, sizeof(url),
    "https://www.googleapis.com/youtube/v3/search"
    "?part=snippet&q=%s&type=video&maxResults=50&key=%s",
    encoded_query, api_key);

    // CURLOPT_URLはどこで通信するのかを決める
    curl_easy_setopt(curl, CURLOPT_URL,
    url);

    // レスポンスを標準出力に書き込むためのコールバック関数を設定する
    //レスポンスデータはwrite_callback関数で処理される
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);

    // 通信を実行する
    // resultには通信の結果が格納される。CURLE_OKであれば成功
    CURLcode result = curl_easy_perform(curl);

    if (result != CURLE_OK) {
    fprintf(stderr, "通信エラー: %s\n",
            curl_easy_strerror(result));
}

// メモ：libcurlが管理するデータはメモリ開放する必要がある

curl_free(encoded_query);
// ibcurlが作成したCURL専用のデータを解放する関数(freeと同じ意味)
curl_easy_cleanup(curl);
}