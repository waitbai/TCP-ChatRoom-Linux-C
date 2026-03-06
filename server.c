#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <net/if.h>
#include <ifaddrs.h>
#include "cJSON.h"

#define PORT 8888
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10

typedef struct {
    int sockfd;
    int id;
    char name[30];
    int in_group;
    int group_id;
    struct sockaddr_in addr; // 存储客户端地址信息
} ClientInfo;

ClientInfo clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// 通过ID查找客户端
ClientInfo* find_client_by_id(int id) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].sockfd != 0 && clients[i].id == id) {
            return &clients[i];
        }
    }
    return NULL;
}

// 通过用户名查找客户端
ClientInfo* find_client_by_name(const char *name) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].sockfd != 0 && strcmp(clients[i].name, name) == 0) {
            return &clients[i];
        }
    }
    return NULL;
}

// 封包发送函数
int send_packet(int sock, const char *data) {
    int length = strlen(data);
    uint32_t net_length = htonl(length);
    
    if (send(sock, &net_length, sizeof(net_length), 0) < 0) {
        return -1;
    }
    
    if (send(sock, data, length, 0) < 0) {
        return -1;
    }
    
    return 0;
}

// 接收数据包
int recv_packet(int sock, char *buffer, int buf_size) {
    uint32_t net_length;
    int bytes_received = recv(sock, &net_length, sizeof(net_length), 0);
    
    if (bytes_received <= 0) {
        return -1;
    }
    
    int length = ntohl(net_length);
    if (length >= buf_size) {
        return -2; // 缓冲区太小
    }
    
    int total_received = 0;
    while (total_received < length) {
        bytes_received = recv(sock, buffer + total_received, length - total_received, 0);
        if (bytes_received <= 0) {
            return -1;
        }
        total_received += bytes_received;
    }
    
    buffer[total_received] = '\0';
    return total_received;
}

// 记录日志到文件
void log_message(const char *message) {
    FILE *log_file = fopen("chat_log.txt", "a");
    if (log_file) {
        time_t now = time(NULL);
        fprintf(log_file, "%s: %s\n", ctime(&now), message);
        fclose(log_file);
    }
}

// 广播消息给所有客户端
void broadcast_message(const char *message, int exclude_sock) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].sockfd != 0 && clients[i].sockfd != exclude_sock) {
            send_packet(clients[i].sockfd, message);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// 发送私聊消息
int send_private_message(int sender_id, int receiver_id, const char *message) {
    ClientInfo *sender = find_client_by_id(sender_id);
    ClientInfo *receiver = find_client_by_id(receiver_id);
    
    if (!sender || !receiver) {
        return -1; // 发送者或接收者不存在
    }
    
    // 创建私聊消息
    cJSON *pm_json = cJSON_CreateObject();
    cJSON_AddStringToObject(pm_json, "status", "message");
    cJSON_AddStringToObject(pm_json, "message", "private_message");
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "sender_id", sender_id);
    cJSON_AddStringToObject(data, "sender_name", sender->name);
    cJSON_AddStringToObject(data, "message", message);
    cJSON_AddItemToObject(pm_json, "data", data);
    
    char *pm_str = cJSON_PrintUnformatted(pm_json);
    int result = send_packet(receiver->sockfd, pm_str);
    
    free(pm_str);
    cJSON_Delete(pm_json);
    
    return result;
}

// 处理客户端请求的线程函数
void *handle_client(void *arg) {
    ClientInfo *client = (ClientInfo *)arg;
    int client_sock = client->sockfd;
    char buffer[BUFFER_SIZE];
    
    // 打印客户端连接信息
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client->addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(client->addr.sin_port);
    
    printf("客户端已连接: %s:%d\n", client_ip, client_port);
    
    // 接收客户端注册信息
    if (recv_packet(client_sock, buffer, BUFFER_SIZE) > 0) {
        cJSON *json = cJSON_Parse(buffer);
        if (json) {
            cJSON *command = cJSON_GetObjectItemCaseSensitive(json, "command");
            if (command && strcmp(command->valuestring, "register") == 0) {
                cJSON *data = cJSON_GetObjectItemCaseSensitive(json, "data");
                if (data) {
                    cJSON *name = cJSON_GetObjectItemCaseSensitive(data, "name");
                    if (name) {
                        strncpy(client->name, name->valuestring, sizeof(client->name) - 1);
                        
                        // 打印用户注册成功信息
                        printf("用户注册成功: %s (ID: %d)\n", client->name, client->id);
                        
                        // 发送成功响应
                        cJSON *response = cJSON_CreateObject();
                        cJSON_AddStringToObject(response, "status", "success");
                        cJSON_AddStringToObject(response, "message", "welcome");
                        
                        cJSON *data_obj = cJSON_CreateObject();
                        cJSON_AddNumberToObject(data_obj, "id", client->id);
                        cJSON_AddStringToObject(data_obj, "name", client->name);
                        cJSON_AddItemToObject(response, "data", data_obj);
                        
                        char *response_str = cJSON_PrintUnformatted(response);
                        send_packet(client_sock, response_str);
                        free(response_str);
                        cJSON_Delete(response);
                        
                        // 广播用户加入消息
                        char join_msg[100];
                        snprintf(join_msg, sizeof(join_msg), "%s joined the chat", client->name);
                        log_message(join_msg);
                        
                        cJSON *broadcast = cJSON_CreateObject();
                        cJSON_AddStringToObject(broadcast, "status", "message");
                        cJSON_AddStringToObject(broadcast, "message", "user_joined");
                        
                        cJSON *broadcast_data = cJSON_CreateObject();
                        cJSON_AddStringToObject(broadcast_data, "user_name", client->name);
                        cJSON_AddItemToObject(broadcast, "data", broadcast_data);
                        
                        char *broadcast_str = cJSON_PrintUnformatted(broadcast);
                        broadcast_message(broadcast_str, client_sock);
                        free(broadcast_str);
                        cJSON_Delete(broadcast);
                    }
                }
            }
            cJSON_Delete(json);
        }
    }
    
    // 处理客户端消息
    while (1) {
        int recv_size = recv_packet(client_sock, buffer, BUFFER_SIZE);
        if (recv_size <= 0) {
            break;
        }
        
        cJSON *json = cJSON_Parse(buffer);
        if (!json) {
            continue;
        }
        
        cJSON *command = cJSON_GetObjectItemCaseSensitive(json, "command");
        if (command) {
            // 处理退出命令
            if (strcmp(command->valuestring, "quit") == 0) {
                break;
            }
            // 处理广播消息
            else if (strcmp(command->valuestring, "broadcast") == 0) {
                cJSON *data = cJSON_GetObjectItemCaseSensitive(json, "data");
                if (data) {
                    cJSON *message = cJSON_GetObjectItemCaseSensitive(data, "message");
                    if (message) {
                        // 记录聊天消息
                        char log_msg[BUFFER_SIZE];
                        snprintf(log_msg, sizeof(log_msg), "%s: %s", client->name, message->valuestring);
                        log_message(log_msg);
                        
                        // 打印服务器端消息
                        printf("%s: %s\n", client->name, message->valuestring);
                        
                        // 广播消息给所有客户端
                        cJSON *broadcast = cJSON_CreateObject();
                        cJSON_AddStringToObject(broadcast, "status", "message");
                        cJSON_AddStringToObject(broadcast, "message", "broadcast");
                        
                        cJSON *broadcast_data = cJSON_CreateObject();
                        cJSON_AddNumberToObject(broadcast_data, "sender_id", client->id);
                        cJSON_AddStringToObject(broadcast_data, "sender_name", client->name);
                        cJSON_AddStringToObject(broadcast_data, "message", message->valuestring);
                        cJSON_AddItemToObject(broadcast, "data", broadcast_data);
                        
                        char *broadcast_str = cJSON_PrintUnformatted(broadcast);
                        broadcast_message(broadcast_str, client_sock);
                        free(broadcast_str);
                        cJSON_Delete(broadcast);
                    }
                }
            }
            // 处理私聊消息
            else if (strcmp(command->valuestring, "pm") == 0) {
                cJSON *data = cJSON_GetObjectItemCaseSensitive(json, "data");
                if (data) {
                    cJSON *receiver_id = cJSON_GetObjectItemCaseSensitive(data, "receiver_id");
                    cJSON *message = cJSON_GetObjectItemCaseSensitive(data, "message");
                    
                    if (receiver_id && message) {
                        // 记录私聊消息
                        char log_msg[BUFFER_SIZE];
                        ClientInfo *receiver = find_client_by_id(receiver_id->valueint);
                        if (receiver) {
                            snprintf(log_msg, sizeof(log_msg), "%s -> %s: %s", 
                                    client->name, receiver->name, message->valuestring);
                            log_message(log_msg);
                            
                            // 打印服务器端私聊消息
                            printf("%s -> %s: %s\n", client->name, receiver->name, message->valuestring);
                            
                            // 发送私聊消息
                            if (send_private_message(client->id, receiver_id->valueint, message->valuestring) == 0) {
                                // 发送成功响应给发送者
                                cJSON *response = cJSON_CreateObject();
                                cJSON_AddStringToObject(response, "status", "success");
                                cJSON_AddStringToObject(response, "message", "private_message_sent");
                                
                                char *response_str = cJSON_PrintUnformatted(response);
                                send_packet(client_sock, response_str);
                                free(response_str);
                                cJSON_Delete(response);
                            } else {
                                // 发送失败响应
                                cJSON *response = cJSON_CreateObject();
                                cJSON_AddStringToObject(response, "status", "error");
                                cJSON_AddStringToObject(response, "message", "user_not_found");
                                
                                char *response_str = cJSON_PrintUnformatted(response);
                                send_packet(client_sock, response_str);
                                free(response_str);
                                cJSON_Delete(response);
                            }
                        } else {
                            // 接收者不存在
                            cJSON *response = cJSON_CreateObject();
                            cJSON_AddStringToObject(response, "status", "error");
                            cJSON_AddStringToObject(response, "message", "user_not_found");
                            
                            char *response_str = cJSON_PrintUnformatted(response);
                            send_packet(client_sock, response_str);
                            free(response_str);
                            cJSON_Delete(response);
                        }
                    }
                }
            }
            // 处理列出在线用户请求
            else if (strcmp(command->valuestring, "list") == 0) {
                cJSON *response = cJSON_CreateObject();
                cJSON_AddStringToObject(response, "status", "success");
                cJSON_AddStringToObject(response, "message", "online_users");
                
                cJSON *users_array = cJSON_CreateArray();
                pthread_mutex_lock(&clients_mutex);
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].sockfd != 0) {
                        cJSON *user = cJSON_CreateObject();
                        cJSON_AddNumberToObject(user, "id", clients[i].id);
                        cJSON_AddStringToObject(user, "name", clients[i].name);
                        cJSON_AddItemToArray(users_array, user);
                    }
                }
                pthread_mutex_unlock(&clients_mutex);
                
                cJSON_AddItemToObject(response, "data", users_array);
                
                char *response_str = cJSON_PrintUnformatted(response);
                send_packet(client_sock, response_str);
                free(response_str);
                cJSON_Delete(response);
            }
        }
        
        cJSON_Delete(json);
    }
    
    // 客户端断开连接
    close(client_sock);
    
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].sockfd == client_sock) {
            clients[i].sockfd = 0;
            break;
        }
    }
    client_count--;
    pthread_mutex_unlock(&clients_mutex);
    
    // 广播用户离开消息
    char leave_msg[100];
    snprintf(leave_msg, sizeof(leave_msg), "%s left the chat", client->name);
    log_message(leave_msg);
    
    // 打印用户离开信息
    printf("用户离开: %s (ID: %d)\n", client->name, client->id);
    
    cJSON *broadcast = cJSON_CreateObject();
    cJSON_AddStringToObject(broadcast, "status", "message");
    cJSON_AddStringToObject(broadcast, "message", "user_left");
    
    cJSON *broadcast_data = cJSON_CreateObject();
    cJSON_AddStringToObject(broadcast_data, "user_name", client->name);
    cJSON_AddItemToObject(broadcast, "data", broadcast_data);
    
    char *broadcast_str = cJSON_PrintUnformatted(broadcast);
    broadcast_message(broadcast_str, client_sock);
    free(broadcast_str);
    cJSON_Delete(broadcast);
    
    free(client);
    pthread_exit(NULL);
}

int main() {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // 初始化客户端数组
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].sockfd = 0;
        clients[i].id = i + 1;
    }
    
    // 创建套接字
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // 设置套接字选项
    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }
    
    // 绑定地址 - 这里绑定所有可用接口
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("169.254.66.68");  // 绑定所有接口
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    
    // 监听连接
    if (listen(server_sock, 5) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    
    // 获取并显示服务器IP地址
    char host[128];
    struct ifaddrs *ifaddr, *ifa;
    
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
    } else {
        printf("服务器已启动，监听端口 %d\n", PORT);
        printf("可用IP地址:\n");
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET)
                continue;
            
            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            const char *addr = inet_ntoa(sa->sin_addr);
            printf("  %s: %s\n", ifa->ifa_name, addr);
        }
        freeifaddrs(ifaddr);
    }
    
    printf("等待客户端连接...\n");
    
    // 接受客户端连接
    while (1) {
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock < 0) {
            perror("Accept failed");
            continue;
        }
        
        pthread_mutex_lock(&clients_mutex);
        
        if (client_count >= MAX_CLIENTS) {
            // 服务器已满，发送错误响应
            printf("服务器已满，拒绝客户端连接\n");
            
            cJSON *response = cJSON_CreateObject();
            cJSON_AddStringToObject(response, "status", "error");
            cJSON_AddStringToObject(response, "message", "server_full");
            
            char *response_str = cJSON_PrintUnformatted(response);
            send_packet(client_sock, response_str);
            free(response_str);
            cJSON_Delete(response);
            
            close(client_sock);
        } else {
            // 添加新客户端
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].sockfd == 0) {
                    clients[i].sockfd = client_sock;
                    clients[i].addr = client_addr; // 保存客户端地址信息
                    client_count++;
                    
                    // 创建线程处理客户端
                    pthread_t thread_id;
                    ClientInfo *client_info = malloc(sizeof(ClientInfo));
                    *client_info = clients[i];
                    
                    if (pthread_create(&thread_id, NULL, handle_client, (void *)client_info) != 0) {
                        perror("Thread creation failed");
                        close(client_sock);
                        clients[i].sockfd = 0;
                        client_count--;
                        free(client_info);
                    } else {
                        pthread_detach(thread_id);
                    }
                    break;
                }
            }
        }
        
        pthread_mutex_unlock(&clients_mutex);
    }
    
    close(server_sock);
    return 0;
}