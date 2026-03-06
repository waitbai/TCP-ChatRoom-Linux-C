# 编译器和编译选项（Linux下gcc，开启警告便于调试）
CC = gcc
CFLAGS = -Wall -Wextra -g
# 链接选项（如果用到线程/网络，必须加-lpthread -lsocket，CJSON已内置无需额外链接）
LDFLAGS = -lpthread

# 目标文件：服务端和客户端
TARGETS = server client

# 默认执行：编译所有目标
all: $(TARGETS)

# 编译服务端：依赖server.c + cJSON.c（如果服务端用到cJSON）
server: server.c cJSON.c
	$(CC) $(CFLAGS) -o server server.c cJSON.c $(LDFLAGS)

# 编译客户端：依赖client.c + cJSON.c（如果客户端用到cJSON）
client: client.c cJSON.c
	$(CC) $(CFLAGS) -o client client.c cJSON.c $(LDFLAGS)

# 清理编译产物
clean:
	rm -f $(TARGETS) *.o core