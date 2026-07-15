#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SOURCE_PORT 47010
#define RELAY_PORT 47001
#define FRAME_SIZE 160

#pragma pack(push, 1)
struct RedundantPacket {
    uint32_t current_seq;
    char current_payload[FRAME_SIZE];
    char prev_payload[FRAME_SIZE];
};
#pragma pack(pop)

#pragma pack(push, 1)
struct StandardPacket {
    uint32_t current_seq;
    char current_payload[FRAME_SIZE];
};
#pragma pack(pop)

int main() {
    int sockfd;
    struct sockaddr_in servaddr, cliaddr, relay_addr;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    servaddr.sin_port = htons(SOURCE_PORT);

    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    memset(&relay_addr, 0, sizeof(relay_addr));
    relay_addr.sin_family = AF_INET;
    relay_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    relay_addr.sin_port = htons(RELAY_PORT);

    char saved_prev_payload[FRAME_SIZE];
    memset(saved_prev_payload, 0, FRAME_SIZE);

    char rx_buf[164]; 
    socklen_t len = sizeof(cliaddr);

    while (1) {
        int n = recvfrom(sockfd, rx_buf, sizeof(rx_buf), 0, (struct sockaddr *)&cliaddr, &len);
        if (n < 164) continue;

        uint32_t current_seq;
        memcpy(&current_seq, rx_buf, 4);
        uint32_t host_seq = ntohl(current_seq);

        if (host_seq % 20 == 0) {
            struct StandardPacket std_pkt;
            std_pkt.current_seq = current_seq;
            memcpy(std_pkt.current_payload, rx_buf + 4, FRAME_SIZE);
            sendto(sockfd, &std_pkt, sizeof(std_pkt), 0, (const struct sockaddr *)&relay_addr, sizeof(relay_addr));
        } else {
            struct RedundantPacket red_pkt;
            red_pkt.current_seq = current_seq;
            memcpy(red_pkt.current_payload, rx_buf + 4, FRAME_SIZE);
            memcpy(red_pkt.prev_payload, saved_prev_payload, FRAME_SIZE);
            sendto(sockfd, &red_pkt, sizeof(red_pkt), 0, (const struct sockaddr *)&relay_addr, sizeof(relay_addr));
        }

        memcpy(saved_prev_payload, rx_buf + 4, FRAME_SIZE);
    }

    close(sockfd);
    return 0;
}