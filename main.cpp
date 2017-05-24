#include <iostream>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <string>
#include <cstdlib>
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
using namespace std;

sem_t sockLock;
int socketfd, vportfd;
int lastPulseTime = 0;
const char readFn[] = "read.txt";
const char writeFn[] = "write.txt";
const int BUF_SIZE = 4096;
unsigned long long totalSndBytes = 0, totalRcvBytes = 0;

struct Msg {
	int length;
	char type;
	char data[BUF_SIZE];
	friend ostream& operator<<(const ostream& o, const Msg& msg);
};

ostream& operator<<(ostream& o, const Msg& msg) {
	o << "len " << msg.length << "\ttype " << (int)msg.type << endl;
	o << msg.data;
	return o;
}

inline int mutSend(Msg* msg) {
	int res = send(socketfd, msg, msg->length, 0);
	totalSndBytes += res;
	return res;
}

inline int mutRecv(Msg* msg) {
	int res = recv(socketfd, msg, sizeof(Msg), 0);
	totalRcvBytes += res;
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

int readVport(char* target, int maxLen) {

}

int writeVport(char* content, int length) {

}

int connect2Server(/*char* virtualIp*/) {
	sockaddr_in6 dest;
	int socketfd = socket(AF_INET6, SOCK_STREAM, 0);
	cout << socketfd << endl;
	bzero(&dest, sizeof(dest));
	dest.sin6_family = AF_INET6;
	dest.sin6_port = htons(1313);
	cout << "convert " << inet_pton(AF_INET6, "2402:f000:5:8401::bbbb:2", &dest.sin6_addr) << endl;
	if ((connect(socketfd, (sockaddr*)&dest, sizeof(dest))) != 0) {
		cout << "connect failed " << endl;
		exit(errno);
	}
	Msg ipReq;
	ipReq.type = 100;
	memset(ipReq.data, 0, sizeof(ipReq.data));
	ipReq.length = 5;
	int sendNum = send(socketfd, (void*)&ipReq, ipReq.length, 0);
	cout << "sent " << sendNum << " bytes of data\n";
	Msg ipRes;
	int recvNum = recv(socketfd, &ipRes, sizeof(ipRes), 0);
	cout << "received " << recvNum << "bytes data\n";
	cout << ipRes.data << endl;
	
	writeTunnel(ipRes.data, ipRes.length-5);
	writeTunnel(&socketfd, sizeof(int));
	// strcpy(virtualIp, ipRes.data);
	return socketfd;
}
void* pulseThread(void*) {
	Msg pulseMsg = {5, 104, ""};
	int cnt = 0;
	while(1) {
		++cnt;
		cout << "in pulseThread " << cnt << endl;
		if(cnt >= 20) {
			cnt = 0;
			cout << "sending to server " << pulseMsg << endl;
			mutSend(&pulseMsg);
		}
		sleep(1);
		
		char content[17];	
		// [BOOL][TOTAL SEND BYTES][TOTAL RECEIVE BYTES]
		// [ 0  ][1 			 8][9                17]
		content[0] = 0;
		*(long long*)(content + 1) = totalSndBytes;
		*(long long*)(content + 9) = totalRcvBytes;
		writeTunnel(content, 17);

		int currentTime = time(NULL);
		int delta = currentTime - lastPulseTime;
		cout << "2 pulses interval: " << delta << " seconds\n";
		if(lastPulseTime && currentTime - lastPulseTime > 60) {	// time out
			cout << "time out!!!!!!" << endl;
			break;
		}
	}
	return NULL;
}

void* recvThread(void*) {
	Msg recvMsg;
	while(1) {
		cout << "recving " << mutRecv(&recvMsg) << " bytes of data\n";
		cout << recvMsg << endl;
		switch(recvMsg.type) {
		case 104:
			lastPulseTime = time(NULL);
			break;
		case 101:	// why???
		case 102:
			cout << recvMsg << endl;
			break;
		case 103:
			char* content = recvMsg.data;
			writeVport(content, recvMsg.length-5);
			break;
		default:
			cout << recvMsg << endl;
		}
	}
	return NULL;
}

void* sendThread(void*) {
	Msg sendMsg;
	while(1) {
		int len = readVport(sendMsg.data, BUF_SIZE);
		if(len >= BUF_SIZE) {
			cout << "gave too long in the tunnel " << len << endl;
			break;
		}
		sendMsg.type = 102;
		sendMsg.length = len;
		mutSend(&sendMsg);
	}
}

void createTunnels() {
	if(access(readFn, F_OK) == -1) {
		cout << "creating read file " << mknod(readFn,  S_IFIFO | 0666, 0);
	}
	if(access(writeFn, F_OK) == -1) {
		cout << "creating write file " << mknod(writeFn,  S_IFIFO | 0666, 0);
	}
}

int main() {
	sem_init(&sockLock, 0, 1);
	// char virtualIp[4096];
	createTunnels();
	socketfd = connect2Server(/*virtualIp*/);
	readTunnel(&vportfd, 4);

	pthread_t pulseThrd, sendThrd, recvThrd;	// receive thread is the main thread
	pthread_create(&pulseThrd, NULL, pulseThread, NULL);
	pthread_create(&sendThrd, NULL, sendThread, NULL);
	pthread_create(&recvThrd, NULL, recvThread, NULL);
	pthread_join(pulseThrd, NULL);
	pthread_join(sendThrd, NULL);
	pthread_join(recvThrd, NULL);

	return 0;
}