#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <stdbool.h>
#include <time.h>

#define RECEIVER_PORT 47002
#define PLAYER_PORT 47020
#define FRAME_SIZE 160
#define BUFFER_SIZE 30000

char playout_buffer[BUFFER_SIZE][FRAME_SIZE];
bool frame_received[BUFFER_SIZE] = {false};
pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;

int sockfd;
struct sockaddr_in player_addr;

// Background worker thread to ingest network packets and recover dropped frames
void *receive_thread(void *arg) {
    struct sockaddr_in cliaddr;
    socklen_t len = sizeof(cliaddr);
    char rx_buf[400];

    while (1) {
        int n = recvfrom(sockfd, rx_buf, sizeof(rx_buf), 0, (struct sockaddr *)&cliaddr, &len);
        if (n < 164) continue;

        uint32_t curr_seq_net;
        memcpy(&curr_seq_net, rx_buf, 4);
        uint32_t curr_seq = ntohl(curr_seq_net);

        pthread_mutex_lock(&buffer_mutex);

        // Primary frame payload
        if (curr_seq < BUFFER_SIZE && !frame_received[curr_seq]) {
            memcpy(playout_buffer[curr_seq], rx_buf + 4, FRAME_SIZE);
            frame_received[curr_seq] = true;
        }

        // Recover preceding frame from redundant payload if piggybacked and needed
        if (n == 324 && curr_seq > 0) {
            uint32_t prev_seq = curr_seq - 1;
            if (prev_seq < BUFFER_SIZE && !frame_received[prev_seq]) {
                memcpy(playout_buffer[prev_seq], rx_buf + 4 + FRAME_SIZE, FRAME_SIZE);
                frame_received[prev_seq] = true;
            }
        }

        pthread_mutex_unlock(&buffer_mutex);
    }
    return NULL;
}

int main() {
    struct sockaddr_in servaddr;
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    servaddr.sin_port = htons(RECEIVER_PORT);

    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    memset(&player_addr, 0, sizeof(player_addr));
    player_addr.sin_family = AF_INET;
    player_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    player_addr.sin_port = htons(PLAYER_PORT);

    // Secure timing configs from environment
    char *t0_str = getenv("T0");
    char *delay_str = getenv("DELAY_MS");
    double t0_val = t0_str ? atof(t0_str) : 0.0;
    double delay_val = delay_str ? atof(delay_str) : 100.0;

    // Cross-reference clocks. Python uses epoch or monotonic depending on system setup. 
    // This perfectly aligns the environment T0 to our robust C POSIX CLOCK_MONOTONIC.
    struct timespec ts_real, ts_mono;
    clock_gettime(CLOCK_REALTIME, &ts_real);
    clock_gettime(CLOCK_MONOTONIC, &ts_mono);
    double real_now = ts_real.tv_sec + ts_real.tv_nsec / 1e9;
    double mono_now = ts_mono.tv_sec + ts_mono.tv_nsec / 1e9;

    double t0_mono;
    if (t0_val > real_now - 86400 && t0_val < real_now + 86400) {
        t0_mono = t0_val - (real_now - mono_now);
    } else {
        t0_mono = t0_val;
    }

    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, receive_thread, NULL) != 0) {
        perror("pthread_create failed");
        exit(EXIT_FAILURE);
    }

    uint32_t next_expected_seq = 0;
    char tx_buf[164];
    char last_valid_payload[FRAME_SIZE];
    memset(last_valid_payload, 0, FRAME_SIZE);
    bool basic_payload_cached = false;

    // We dispatch just-in-time, exactly 5ms prior to the strict deadline
    double send_offset_s = (delay_val / 1000.0) - 0.005;

    while (1) {
        double target_mono = t0_mono + send_offset_s + (next_expected_seq * 0.020);

        struct timespec ts_target;
        ts_target.tv_sec = (time_t)target_mono;
        ts_target.tv_nsec = (long)((target_mono - ts_target.tv_sec) * 1e9);

        // Rest accurately until dispatch window
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts_target, NULL);

        pthread_mutex_lock(&buffer_mutex);
        
        if (frame_received[next_expected_seq]) {
            // Frame is available
            memcpy(last_valid_payload, playout_buffer[next_expected_seq], FRAME_SIZE);
            basic_payload_cached = true;
            
            uint32_t net_seq = htonl(next_expected_seq);
            memcpy(tx_buf, &net_seq, 4);
            memcpy(tx_buf + 4, playout_buffer[next_expected_seq], FRAME_SIZE);
            sendto(sockfd, tx_buf, 164, 0, (const struct sockaddr *)&player_addr, sizeof(player_addr));
        } 
        else if (basic_payload_cached) {
            // Drop Concealment: Frame wasn't in buffer, dispatch last valid one to meet deadline anyway
            uint32_t net_seq = htonl(next_expected_seq);
            memcpy(tx_buf, &net_seq, 4);
            memcpy(tx_buf + 4, last_valid_payload, FRAME_SIZE);
            sendto(sockfd, tx_buf, 164, 0, (const struct sockaddr *)&player_addr, sizeof(player_addr));
        }

        pthread_mutex_unlock(&buffer_mutex);
        
        next_expected_seq++;
        
        // Safety exit block
        if (next_expected_seq >= 20000) {
            break;
        }
    }

    close(sockfd);
    return 0;
}