#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>

#include "router.h"

#pragma comment(lib, "ws2_32.lib")

#define PORT 8080

/*
 * すべてのデータを送信するためのヘルパー関数.
 * Returns 1 if all dataが正常に送信された, 0 そうでない場合.
 */
static int send_all(SOCKET client_socket, const char *data, int length)
{
    int total_sent = 0;

    while (total_sent < length) {
        int sent = send(client_socket, data + total_sent,
                        length - total_sent, 0);
        if (sent == SOCKET_ERROR) {
            return 0;
        }
        total_sent += sent;
    }

    return 1;
}

static int send_chunk(void *context, const char *data, size_t length)
{
    SOCKET client_socket = *(SOCKET *)context;
    char header[32];
    int header_length = snprintf(header, sizeof(header), "%zx\r\n", length);

    return send_all(client_socket, header, header_length) &&
           send_all(client_socket, data, (int)length) &&
           send_all(client_socket, "\r\n", 2);
}

static int send_template(FILE *html_file, SOCKET client_socket,
                         const char *request, const char *message)
{
    static const char api_placeholder[] = "{{api_res_data}}";
    static const char message_placeholder[] = "{{message}}";
    char candidate[32];
    int current;

    while ((current = fgetc(html_file)) != EOF) {
        if (current != '{') {
            char character = (char)current;
            if (!send_chunk(&client_socket, &character, 1)) {
                return 0;
            }
            continue;
        }

        size_t candidate_length = 0;
        candidate[candidate_length++] = (char)current;
        while (candidate_length < sizeof(candidate) - 1 &&
               (current = fgetc(html_file)) != EOF) {
            candidate[candidate_length++] = (char)current;
            if (candidate_length >= 2 &&
                candidate[candidate_length - 1] == '}' &&
                candidate[candidate_length - 2] == '}') {
                break;
            }
        }
        candidate[candidate_length] = '\0';

        if (strcmp(candidate, api_placeholder) == 0 && message != NULL) {
            if (!get_searchResults(request, send_chunk, &client_socket)) {
                return 0;
            }
        } else if (strcmp(candidate, message_placeholder) != 0 &&
                   !send_chunk(&client_socket, candidate, candidate_length)) {
            return 0;
        }
    }

    return ferror(html_file) == 0;
}

/*
 * フラウザとの接続設定.
 */
int main(void)
{
    WSADATA wsa_data;
    SOCKET server_socket;
    SOCKET client_socket;
    struct sockaddr_in server_address;
    char request[4096];
    char response[512];

    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        fprintf(stderr, "Failed to initialize Winsock.\n");
        return 1;
    }

    server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET) {
        fprintf(stderr, "Failed to create the server socket.\n");
        WSACleanup();
        return 1;
    }

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_address.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_address,
             sizeof(server_address)) == SOCKET_ERROR ||
        listen(server_socket, 16) == SOCKET_ERROR) {
        fprintf(stderr, "Failed to listen on port %d.\n", PORT);
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    printf("Open http://localhost:%d/ in your browser.\n", PORT);
    printf("Press Ctrl+C in this window to stop the server.\n");
    fflush(stdout);

    while (1) {
        client_socket = accept(server_socket, NULL, NULL);
        if (client_socket == INVALID_SOCKET) {
            fprintf(stderr, "Failed to accept a client connection.\n");
            break;
        }

        int request_length = recv(client_socket, request,
                                  sizeof(request) - 1, 0);
        if (request_length <= 0) {
            closesocket(client_socket);
            continue;
        }
        request[request_length] = '\0';

        const char *file_name = "index.html";
        const char *content_type = "text/html; charset=UTF-8";
        const char *search_message = NULL;
        if (!route_request(request, &file_name, &content_type,
                           &search_message)) {
            const char error_response[] =
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Type: text/plain; charset=UTF-8\r\n"
                "Content-Length: 10\r\n"
                "Connection: close\r\n"
                "\r\n"
                "Not Found.\n";
            send_all(client_socket, error_response,
                     (int)strlen(error_response));
            shutdown(client_socket, SD_SEND);
            closesocket(client_socket);
            continue;
        }

        FILE *html_file = fopen(file_name, "rb");
        if (html_file == NULL) {
            const char error_response[] =
                "HTTP/1.1 500 Internal Server Error\r\n"
                "Content-Type: text/plain; charset=UTF-8\r\n"
                "Content-Length: 21\r\n"
                "Connection: close\r\n"
                "\r\n"
                "File not found.\n";
            send_all(client_socket, error_response,
                     (int)strlen(error_response));
            shutdown(client_socket, SD_SEND);
            closesocket(client_socket);
            continue;
        }

        int header_length = snprintf(
            response, sizeof(response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Connection: close\r\n"
            "\r\n",
            content_type);

        if (!send_all(client_socket, response, header_length) ||
            !send_template(html_file, client_socket, request,
                           search_message)) {
            fprintf(stderr, "Failed to send the HTTP response.\n");
        }

        fclose(html_file);
        send_all(client_socket, "0\r\n\r\n", 5);
        shutdown(client_socket, SD_SEND);
        closesocket(client_socket);
    }

    closesocket(server_socket);
    WSACleanup();
    return 0;
}
