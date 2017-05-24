// #include <iostream>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <strings.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
// using namespace std;
typedef enum { false = 0, true } bool;
sem_t sockLock;
int socketfd, vportfd;
int lastPulseTime = 0;
bool remoteAlive = true;
const char readFn[] = "/data/data/byr.ipv4over6/myfifo";
const char writeFn[] = "/data/data/byr.ipv4over6/myfifo_stats";

#define BUF_SIZE 4096
int ttlSndBytes = 0, ttlRcvBytes = 0, ttlSndPkts = 0, ttlRcvPkts;

typedef  struct {
    int length;
    char type;
    char data[BUF_SIZE];
    // friend ostream& operator<<(const ostream& o, const Msg& msg);
} Msg ;

// ostream& operator<<(ostream& o, const Msg& msg) {
// 	o << "len " << msg.length << "\ttype " << (int)msg.type << endl;
// 	o << msg.data;
// 	return o;
// }
void printMsg(Msg* msg) {

}

inline int mutSend(Msg* msg) {
    int res = send(socketfd, msg, msg->length, 0);
    ttlSndBytes += res;
    return res;
}

inline int mutRecv(Msg* msg) {
    int res = recv(socketfd, msg, sizeof(Msg), 0);
    ttlRcvBytes += res;
    return res;
}

int readTunnel(char* target, int maxLen) {
    int readfd = open(readFn, O_RDONLY);
    int cnt = read(readfd, target, maxLen);
    close(readfd);
    return cnt;
}

int writeTunnel(char* content, int length) {
    // content[0] determins whether it is an ip response or the statistics of data flow
    // 0 for statistics, others(assert 103) is ip response
    int writefd = open(writeFn, O_WRONLY);
    int cnt = write(writefd, content, length);
    close(writefd);
    return cnt;
}

inline int readVport(char* target, int maxLen) {
    return read(vportfd, target, maxLen);
}

inline int writeVport(char* content, int length) {
    return write(vportfd, content, length);
}

int connect2Server(/*char* virtualIp*/) {
    struct sockaddr_in6 dest;
    int socketfd = socket(AF_INET6, SOCK_STREAM, 0);
    printf("socketfd: %d", socketfd);
    bzero(&dest, sizeof(dest));
    dest.sin6_family = AF_INET6;
    dest.sin6_port = htons(1313);
    printf("convert %d\n", inet_pton(AF_INET6, "2402:f000:5:8401::bbbb:2", &dest.sin6_addr));
    if ((connect(socketfd, (struct sockaddr*)&dest, sizeof(dest))) != 0) {
        printf("connect failed %d\n", errno);
        exit(errno);
    }
    Msg ipReq;
    ipReq.type = 100;
    memset(ipReq.data, 0, sizeof(ipReq.data));
    ipReq.length = 5;
    int sendNum = send(socketfd, (void*)&ipReq, ipReq.length, 0);
    printf("sent %d bytes of data\n", sendNum);
    Msg ipRes;
    int recvNum = recv(socketfd, &ipRes, sizeof(ipRes), 0);
    printf("received %d bytes data\n", recvNum);
    printf("%s\n", ipRes.data);

    sprintf(ipRes.data + length - 5, " %d%c", socketfd, '\n')
    writeTunnel(ipRes.data, strlen(ipRes.data));
    // writeTunnel((char*)&socketfd, sizeof(int));
    // strcpy(virtualIp, ipRes.data);
    return socketfd;
}
void* pulseThread(void* nouse) {
    Msg pulseMsg = {5, 104, ""};
    int cnt = 0;
    char content[50];
    while(1) {
        ++cnt;
        printf("in pulseThread %d\n", cnt);
        if(cnt >= 20) {
            cnt = 0;
            printf("sending to server\n");
            printMsg(&pulseMsg);
            mutSend(&pulseMsg);
        }
        sleep(1);

        sprintf(content, "%d %d %d %d%c", ttlSndBytes, ttlSndPkts, ttlRcvBytes, ttlRcvPkts, '\0');
        writeTunnel(content, strlen(content));

        int currentTime = time(NULL);
        int delta = currentTime - lastPulseTime;
        printf("2 pulses interval: %d seconds\n", delta);
        if(lastPulseTime && currentTime - lastPulseTime > 60) {	// time out
            printf("time out!!!!!!\n");
            break;
        }
    }
    sprintf(content, "-1 -1 -1 -1%c", '\0');
    writeTunnel(content, strlen(content));
    return NULL;
}

void* recvThread(void* nouse) {
    Msg recvMsg;
    while(remoteAlive) {
        printf("recving %d bytes of data\n", mutRecv(&recvMsg));
        printMsg(&recvMsg);
        switch(recvMsg.type) {
            case 104: {
                lastPulseTime = time(NULL);
            }break;
            case 101:	// why???
            case 102: {
                printMsg(&recvMsg);
            }break;
            case 103:{
                char* content = recvMsg.data;
                writeVport(content, recvMsg.length-5);
            }break;
            default:
                printMsg(&recvMsg);
        }
    }
    return NULL;
}

void* sendThread(void* nouse) {
    Msg sendMsg;
    while(remoteAlive) {
        int len = readVport(sendMsg.data, BUF_SIZE);
        if(len >= BUF_SIZE) {
            printf("gave too long in the tunnel %d\n", len);
            break;
        }
        sendMsg.type = 102;
        sendMsg.length = len;
        mutSend(&sendMsg);
    }
}

void createTunnels() {
    if(access(readFn, F_OK) == -1) {
        printf("creating read file %d\n", mknod(readFn,  S_IFIFO | 0666, 0));
    }
    if(access(writeFn, F_OK) == -1) {
        printf("creating write file %d\n", mknod(writeFn,  S_IFIFO | 0666, 0));
    }
}

int main() {
    sem_init(&sockLock, 0, 1);
    // char virtualIp[4096];
    createTunnels();
    socketfd = connect2Server(/*virtualIp*/);
    readTunnel((char*)&vportfd, 4);

    pthread_t pulseThrd, sendThrd, recvThrd;	// receive thread is the main thread
    pthread_create(&pulseThrd, NULL, pulseThread, NULL);
    pthread_create(&sendThrd, NULL, sendThread, NULL);
    pthread_create(&recvThrd, NULL, recvThread, NULL);
    pthread_join(pulseThrd, NULL);
    pthread_join(sendThrd, NULL);
    pthread_join(recvThrd, NULL);

    return 0;
}
