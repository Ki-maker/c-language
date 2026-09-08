#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "search.h"

#define BUFFER_API_RESPONSE 256
#define BUFFER_END 1

typedef struct {
    char api_response[BUFFER_API_RESPONSE + BUFFER_END];
    size_t response_length;
    size_t capacity;
    response_writer writer;
    void *writer_context;
} response_context;


/*
 * sizeとcountをかけたバイト数を返すコールバック関数.
 * Returns total_size
 */
static size_t write_callback(void *contents, size_t size,
                             size_t count, void *body_data)
{
    // body_dataはvoid型なので、別変数でresponse_context型に変更する必要がある
    response_context *context = body_data;

    // API Response(contents)の容量を求める
    size_t api_response_size = size * count;

    // 必要な容量を計算する
    // context->response_lengthはすでに格納されている容量、api_response_sizeは新たに追加される容量
    size_t required_capacity = context->response_length +
                               api_response_size + BUFFER_END;

    // bodyの容量が足りない場合、0を返す
    //  context->capacityはあらかじめ確保している容量
    if (required_capacity > context->capacity) {
        printf("API RESPONSEを追加した際に必要な容量が不足しています\n");
        return 0;
    }

    // writerがNULLでない場合、writer(main.cにあるsend_chunk)を使ってデータをHTTPソケットへ送信する
    if (context->writer != NULL) {
        if (!context->writer(context->writer_context, contents,
                             api_response_size)) {
            return 0;
        }
        return api_response_size;
    }
    // TODO: ソケット通信しているmain.cにあるsend_chunk関数をみて、APIレスポンスをHTTPソケットへ送信する処理を追加する
    return api_response_size;
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
        return "検索ボタンが押されました";
    }

    return NULL;
}

int get_searchResults(const char *request, response_writer writer,
                      void *writer_context) {
    (void)request;
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
        return 0;
    }


    response_context context = {
        .response_length = 0, // APIレスポンスの長さを初期化
        .capacity = BUFFER_API_RESPONSE + BUFFER_END,
        .writer = writer, // 受信した API データを書き込む処理
        .writer_context = writer_context
    };

    if (api_key == NULL || api_key[0] == '\0') {
        fprintf(stderr, "YOUTUBE_API_KEYが設定されていません\n");
        curl_easy_cleanup(curl);
        return 0;
    }

    // 検索文字列をURLの使える形に変更する
    // 第三引数の0は'\0'まで自動判定
    encoded_query = curl_easy_escape(curl, "劇⽯中毒", 0);
    if (encoded_query == NULL) {
        fprintf(stderr, "検索語のURLエンコードに失敗しました\n");
        curl_easy_cleanup(curl);
        return 0;
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

    // CURLOPT_WRITEFUNCTION=どの関数を呼ぶのか
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);

    // CURLOPT_WRITEDATA=一つ上で設定している関数にどのデータの引数を渡すのか
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
    // 通信を実行する
    // resultには通信の結果が格納される。CURLE_OKであれば成功
    CURLcode result = curl_easy_perform(curl);

    if (result != CURLE_OK) {
        fprintf(stderr, "通信エラー: %s\n",
            curl_easy_strerror(result));
        curl_free(encoded_query);
        curl_easy_cleanup(curl);
        return 0;
        }

// メモ：libcurlが管理するデータはメモリ開放する必要がある
curl_free(encoded_query);
// ibcurlが作成したCURL専用のデータを解放する関数(freeと同じ意味)
curl_easy_cleanup(curl);
return 1;
}