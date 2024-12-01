#include "lib/headerlist.h"
#include "lib/readdata.h"
#include "lib/httpfunc.h"

#define MAX_EVENTS 1024
#define MAX_DATA_SIZE 1024
#define MAX_CLIENTS 20000

struct QuestionList *question;

// 소켓을 non-blocking으로 설정
void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int PORTNUM = atoi(argv[1]);
    int sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int optval = 1;
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORTNUM);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Bind failed");
        close(sd);
        exit(EXIT_FAILURE);
    }

    if (listen(sd, MAX_CLIENTS) == -1) {
        perror("Listen failed");
        close(sd);
        exit(EXIT_FAILURE);
    }

    set_nonblocking(sd);

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1 failed");
        close(sd);
        exit(EXIT_FAILURE);
    }

    struct epoll_event event = {0}, events[MAX_EVENTS];
    event.events = EPOLLIN;
    event.data.fd = sd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sd, &event) == -1) {
        perror("epoll_ctl failed");
        close(sd);
        exit(EXIT_FAILURE);
    }

    printf("Listening on port %d\n", PORTNUM);

    question = read_gag();
    printf("Data loaded successfully\n");

    while (1) {
        int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (num_events == -1) {
            perror("epoll_wait failed");
            break;
        }

        for (int i = 0; i < num_events; i++) {
            if (events[i].data.fd == sd) {
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);
                int client_sd = accept(sd, (struct sockaddr *)&client_addr, &addr_len);

                if (client_sd == -1) {
                    perror("Accept failed");
                    continue;
                }
                set_nonblocking(client_sd);

                event.events = EPOLLIN | EPOLLET;
                event.data.fd = client_sd;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_sd, &event);
            } else {
                char buf[MAX_DATA_SIZE] = {0};
                ssize_t n = recv(events[i].data.fd, buf, sizeof(buf) - 1, 0);
                if (n <= 0) {
                    close(events[i].data.fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
                } else {
                    struct HTTPRequest http_request = {0};
                    parse_http_request(buf, &http_request);

                    if (strcmp(http_request.method, "GET") == 0){
                        if(strcmp(http_request.path, "/quiz") == 0) {
                        send_quiz(events[i].data.fd);
                        }
                        else{
                            char file_path[512];
                            snprintf(file_path, sizeof(file_path), "./rsc/html/%s", http_request.path[0] == '/' ? http_request.path + 1 : http_request.path);
                            send_file_content(events[i].data.fd, file_path);
                        }
                    } else {
                        const char *not_found_response =
                            "HTTP/1.1 404 Not Found\r\n"
                            "Content-Type: text/plain\r\n"
                            "Content-Length: 13\r\n"
                            "\r\n404 Not Found";
                        send(events[i].data.fd, not_found_response, strlen(not_found_response), 0);
                    }
                    close(events[i].data.fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
                }
            }
        }
    }

    close(sd);
    close(epoll_fd);
    return 0;
}
