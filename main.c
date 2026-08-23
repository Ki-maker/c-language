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

        fseek(html_file, 0, SEEK_END);
        long body_length = ftell(html_file);
        rewind(html_file);
        char *body = malloc((size_t)body_length + 1);
        if (body == NULL ||
            fread(body, 1, (size_t)body_length, html_file) !=
                (size_t)body_length) {
            fclose(html_file);
            free(body);
            closesocket(client_socket);
            continue;
        }
        fclose(html_file);
        body[body_length] = '\0';

        // 他の接続先のcファイルの処理に失敗した場合
        if (!replace_message_placeholder(
                &body, &body_length,
                search_message != NULL ? search_message : "")) {
            free(body);
            closesocket(client_socket);
            continue;
        }

        int header_length = snprintf(
            response, sizeof(response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %ld\r\n"
            "Connection: close\r\n"
            "\r\n",
            content_type, body_length);

        if (!send_all(client_socket, response, header_length) ||
            !send_all(client_socket, body, (int)body_length)) {
            fprintf(stderr, "Failed to send the HTTP response.\n");
        }

        free(body);
        shutdown(client_socket, SD_SEND);
        closesocket(client_socket);
    }

    closesocket(server_socket);
    WSACleanup();
    return 0;
}
