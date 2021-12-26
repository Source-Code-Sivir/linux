#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<assert.h>
#include<stdio.h>
#include<signal.h>
#include<signal.h>
#include<unistd.h>
#include<errno.h>
#include<string.h>
#include<fcntl.h>
#include<stdlib.h>
#include<sys/epoll.h>
#include<pthread.h>
#include<arpa/inet.h>

#define MAX_EVENT_NUMBER 1024
static int pipefd[2];

int setnonblocking(int fd)
{
	int oldOption = fcntl(fd, F_GETFL);
	int newOption = oldOption | O_NONBLOCK;
	fcntl(fd, F_SETFL, newOption);
	return oldOption;
}
void addfd(int epollfd, int fd)
{
	epoll_event curevent;
	curevent.data.fd = fd;
	curevent.events = EPOLLIN | EPOLLET;//���ݶ������������������+��Ե������
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &curevent);
	setnonblocking(fd);
}

//�źŴ�����
void sig_handler(int sig)
{
	int save_errno = errno;//����ԭ����errno �ں��������ָ�����֤�����Ŀ�������
	int msg = sig;
	send(pipefd[1], (char*)&msg, 1, 0);//���ź�д��ܵ� ֪ͨ��ѭ��
	errno = save_errno;
}
//�����źŵĴ�����
void addsig(int sig)
{
	struct sigaction sa;
	memset(&sa, '\0', sizeof(sa));
	sa.sa_handler = sig_handler;
	sa.sa_flags |= SA_RESTART;
	sigfillset(&sa.sa_mask);//���źż���sigaction���źż��� Ȼ��ֱ�ӵ��ö�Ӧ�ĺ���
	assert(sigaction(sig, &sa, 0) != -1);//�������Ϊ�� ��ֱ������ѭ��
}
int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		printf("para less\n");
		exit(1);
	}
	const char* ip = argv[1];
	int port = atoi(argv[2]);

	int ret = 0;
	sockaddr_in address;
	bzero(&address, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	address.sin_addr.s_addr = inet_addr(ip);

	int listenfd = socket(AF_INET, SOCK_STREAM, 0);
	assert(listenfd >= 0);

	if (bind(listenfd, (sockaddr*)&address, sizeof(address)) == -1)
	{
		printf("bind error %d\n",errno);
		exit(1);
	}
	ret = listen(listenfd, 5);
	assert(ret != -1);

	epoll_event events[MAX_EVENT_NUMBER];
	int epollfd = epoll_create(5);//epoll size��epollʵ���Ĵ�С��
	assert(epollfd != -1);
	addfd(epollfd, listenfd);

	ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
	assert(ret != -1);
	setnonblocking(pipefd[0]);
	setnonblocking(pipefd[1]);

	addsig(SIGHUP);
	addsig(SIGCHLD);
	addsig(SIGTERM);
	addsig(SIGINT);
	bool stopserver = false;

	while (!stopserver)
	{
		int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
		if (number < 0 && errno != EINTR)
		{
			printf("epoll failure\n");
			exit(1);
		}
		for (int i = 0; i < number; ++i)
		{
			int sockfd = events[i].data.fd;
			if (sockfd == listenfd)
			{
				sockaddr_in clntaddr;
				socklen_t addrlen;
				int clntfd = accept(sockfd, (sockaddr*)&clntaddr, &addrlen);
				assert(clntfd >= 0);
				addfd(epollfd, clntfd);
			}
			else if (sockfd == pipefd[0] && events[i].events & EPOLLIN)
			{
				int sig;
				char signals[1024];
				ret = recv(pipefd[0], signals, sizeof(signals), 0);
				if (ret == -1)
					continue;
				else if (ret == 0)
					continue;
				else
				{
					for (int i = 0; i < ret; ++i)
					{
						switch (signals[i])
						{
						case SIGCHLD:
						case SIGHUP:
						{
							continue;
						}
						case SIGTERM:
						case SIGINT:
						{
							stopserver = true;
						}
						default:
							break;
						}
					}
				}
			}
		}
	}
	printf("close serv\n");
	close(listenfd);
	close(pipefd[1]);
	close(pipefd[0]);
	return 0;
}