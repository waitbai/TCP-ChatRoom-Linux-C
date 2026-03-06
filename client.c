#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "cJSON.h"

#define BUFFER_SIZE 1024

int sockfd;
char name[30] = {0};
int user_id = 0;

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

// 显示帮助信息
void show_help() {
    printf("\n可用命令:\n");
    printf("  /help - 显示帮助信息\n");
    printf("  /list - 列出在线用户\n");
    printf("  /pm [用户ID] [消息] - 发送私聊消息\n");
    printf("  /quit - 退出聊天室\n");
    printf("  直接输入消息 - 发送广播消息给所有人\n");
}

// 列出在线用户
void list_users() {
    cJSON *list_json = cJSON_CreateObject();
    cJSON_AddStringToObject(list_json, "command", "list");
    
    char *list_str = cJSON_PrintUnformatted(list_json);
    send_packet(sockfd, list_str);
    free(list_str);
    cJSON_Delete(list_json);
}

// 发送私聊消息
void send_private_message(int receiver_id, const char *message) {
    cJSON *pm_json = cJSON_CreateObject();
    cJSON_AddStringToObject(pm_json, "command", "pm");
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "receiver_id", receiver_id);
    cJSON_AddStringToObject(data, "message", message);
    cJSON_AddItemToObject(pm_json, "data", data);
    
    char *pm_str = cJSON_PrintUnformatted(pm_json);
    send_packet(sockfd, pm_str);
    free(pm_str);
    cJSON_Delete(pm_json);
    
    // 在发送方也显示提示
    printf("[您发送给用户ID:%d] %s\n", receiver_id, message);
}

// 接收消息的线程函数
void *recv_thread(void *arg) {
    char buffer[BUFFER_SIZE];
    
    while (1) {
        int recv_size = recv_packet(sockfd, buffer, BUFFER_SIZE);
        if (recv_size <= 0) {
            printf("与服务器断开连接\n");
            exit(0);
        }
        
        cJSON *json = cJSON_Parse(buffer);
        if (!json) {
            continue;
        }
        
        cJSON *status = cJSON_GetObjectItemCaseSensitive(json, "status");
        cJSON *message_type = cJSON_GetObjectItemCaseSensitive(json, "message");
        cJSON *data = cJSON_GetObjectItemCaseSensitive(json, "data");
        
        if (status && message_type) {
            if (strcmp(status->valuestring, "error") == 0) {
                printf("错误: %s\n", message_type->valuestring);
            } else if (strcmp(status->valuestring, "success") == 0) {
                if (strcmp(message_type->valuestring, "welcome") == 0) {
                    // 注册成功，保存用户ID
                    if (data) {
                        cJSON *id = cJSON_GetObjectItemCaseSensitive(data, "id");
                        if (id) {
                            user_id = id->valueint;
                            printf("您的用户ID是: %d\n", user_id);
                        }
                    }
                } else if (strcmp(message_type->valuestring, "online_users") == 0) {
                    // 显示在线用户列表
                    if (data && cJSON_IsArray(data)) {
                        printf("\n=== 在线用户列表 ===\n");
                        printf("ID\t用户名\n");
                        printf("--\t-----\n");
                        
                        int size = cJSON_GetArraySize(data);
                        for (int i = 0; i < size; i++) {
                            cJSON *user = cJSON_GetArrayItem(data, i);
                            cJSON *id = cJSON_GetObjectItemCaseSensitive(user, "id");
                            cJSON *name = cJSON_GetObjectItemCaseSensitive(user, "name");
                            
                            if (id && name) {
                                printf("%d\t%s\n", id->valueint, name->valuestring);
                            }
                        }
                        printf("====================\n");
                        printf("使用 /pm [用户ID] [消息] 发送私聊消息\n\n");
                    }
                } else if (strcmp(message_type->valuestring, "private_message_sent") == 0) {
                    printf("✓ 私聊消息已发送\n");
                }
            } else if (strcmp(status->valuestring, "message") == 0) {
                if (data) {
                    if (strcmp(message_type->valuestring, "broadcast") == 0) {
                        cJSON *sender_name = cJSON_GetObjectItemCaseSensitive(data, "sender_name");
                        cJSON *msg_content = cJSON_GetObjectItemCaseSensitive(data, "message");
                        
                        if (sender_name && msg_content) {
                            printf("%s: %s\n", sender_name->valuestring, msg_content->valuestring);
                        }
                    } else if (strcmp(message_type->valuestring, "private_message") == 0) {
                        cJSON *sender_id = cJSON_GetObjectItemCaseSensitive(data, "sender_id");
                        cJSON *sender_name = cJSON_GetObjectItemCaseSensitive(data, "sender_name");
                        cJSON *msg_content = cJSON_GetObjectItemCaseSensitive(data, "message");
                        
                        if (sender_name && msg_content) {
                            // 使用颜色和特殊格式突出显示私聊消息
                            printf("\033[1;35m[私聊来自 %s (ID:%d)]\033[0m %s\n", 
                                  sender_name->valuestring, 
                                  sender_id->valueint, 
                                  msg_content->valuestring);
                        }
                    } else if (strcmp(message_type->valuestring, "user_joined") == 0) {
                        cJSON *user_name = cJSON_GetObjectItemCaseSensitive(data, "user_name");
                        if (user_name) {
                            printf("➤ %s 加入了聊天室\n", user_name->valuestring);
                        }
                    } else if (strcmp(message_type->valuestring, "user_left") == 0) {
                        cJSON *user_name = cJSON_GetObjectItemCaseSensitive(data, "user_name");
                        if (user_name) {
                            printf("➤ %s 离开了聊天室\n", user_name->valuestring);
                        }
                    }
                }
            }
        }
        
        cJSON_Delete(json);
    }
    
    return NULL;
}

int main() {
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    
    // 输入用户名
    printf("请输入用户名: ");
    fgets(name, sizeof(name), stdin);
    name[strcspn(name, "\n")] = 0; // 移除换行符
    
    // 创建套接字
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("创建套接字失败");
        exit(EXIT_FAILURE);
    }
    
    // 设置服务器地址
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8888);
    server_addr.sin_addr.s_addr = inet_addr("169.254.66.68"); // 本地地址，可根据需要修改
    
    // 连接服务器
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("连接服务器失败");
        exit(EXIT_FAILURE);
    }
    
    // 发送注册信息
    cJSON *register_json = cJSON_CreateObject();
    cJSON_AddStringToObject(register_json, "command", "register");
    
    cJSON *reg_data = cJSON_CreateObject();
    cJSON_AddStringToObject(reg_data, "name", name);
    cJSON_AddItemToObject(register_json, "data", reg_data);
    
    char *register_str = cJSON_PrintUnformatted(register_json);
    send_packet(sockfd, register_str);
    free(register_str);
    cJSON_Delete(register_json);
    
    // 显示欢迎信息
    printf("\n================================\n");
    printf("欢迎来到聊天室, %s!\n", name);
    printf("输入 /help 查看可用命令\n");
    printf("================================\n\n");
    
    // 创建接收线程
    pthread_t recv_tid;
    if (pthread_create(&recv_tid, NULL, recv_thread, NULL) != 0) {
        perror("创建接收线程失败");
        exit(EXIT_FAILURE);
    }
    
    // 主循环处理用户输入
    while (1) {
        printf("> ");
        fflush(stdout);
        
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strcspn(buffer, "\n")] = 0; // 移除换行符
        
        if (strcmp(buffer, "/quit") == 0) {
            // 发送退出命令
            cJSON *quit_json = cJSON_CreateObject();
            cJSON_AddStringToObject(quit_json, "command", "quit");
            
            char *quit_str = cJSON_PrintUnformatted(quit_json);
            send_packet(sockfd, quit_str);
            free(quit_str);
            cJSON_Delete(quit_json);
            
            break;
        } else if (strcmp(buffer, "/help") == 0) {
            show_help();
        } else if (strcmp(buffer, "/list") == 0) {
            list_users();
        } else if (strncmp(buffer, "/pm ", 4) == 0) {
            // 解析私聊命令
            char *space = strchr(buffer + 4, ' ');
            if (space) {
                *space = '\0';
                int receiver_id = atoi(buffer + 4);
                char *message = space + 1;
                
                if (receiver_id > 0 && strlen(message) > 0) {
                    send_private_message(receiver_id, message);
                } else {
                    printf("用法: /pm [用户ID] [消息]\n");
                    printf("例如: /pm 2 你好，最近怎么样？\n");
                }
            } else {
                printf("用法: /pm [用户ID] [消息]\n");
                printf("例如: /pm 2 你好，最近怎么样？\n");
            }
        } else if (strlen(buffer) > 0) {
            // 发送广播消息
            cJSON *msg_json = cJSON_CreateObject();
            cJSON_AddStringToObject(msg_json, "command", "broadcast");
            
            cJSON *msg_data = cJSON_CreateObject();
            cJSON_AddStringToObject(msg_data, "message", buffer);
            cJSON_AddItemToObject(msg_json, "data", msg_data);
            
            char *msg_str = cJSON_PrintUnformatted(msg_json);
            send_packet(sockfd, msg_str);
            free(msg_str);
            cJSON_Delete(msg_json);
        }
    }
    
    close(sockfd);
    return 0;
}