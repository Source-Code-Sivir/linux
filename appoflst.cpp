#include"timerlink.h"
#include<sys/epoll.h>
#include<arpa/inet.h>

#define FD_LIMIT 65535
#define MAX_EVENT_NUMBER 1024
#define TIMESHOT 5

static int pipe[2];
static sort_timer_list timer_lst;
static int epollfd = 0;
int setnoblocking(int fd)
{
	int old = fcntl(fd, F_GETFL);
	int newop = old | O_NONBLOCK;
	fcntl(fd, F_SETFL, newop);
	return old;
}
void addfd(int epollfd, int fd)
{
	epoll_event even;
	even.data.fd = fd;
	even.events = EPOLLIN | EPOLLET;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd,&even);
	setnoblocking(fd);
}

void sig_handler(int sig)
{
	int saveerr = errno;
	int msg = sig;
	send(pipe[1], (char*)&msg, 1, 0);
	errno = saveerr;
}

void addsig(int sig)
{
	struct sigaction act;
	memset(&act, 0, sizeof(act));
	act.sa_handler = sig_handler;
	act.sa_flags |= SA_RESTART;//һ�����ź�������SA_RESTART��ǣ���ô��ִ��ĳ������ϵͳ����ʱ���յ����ź�ʱ�����̲��᷵�أ���������ִ�и�ϵͳ���á�
	/*sa_flags�а���������־λ�������ո��ᵽ��SA_NODEFER��SA_NOMASK��־λ����һ��
	�Ƚ���Ҫ�ı�־λ��SA_SIGINFO�����趨�˸ñ�־λʱ����ʾ�źŸ����Ĳ������Ա����ݵ���
	�Ŵ������У���ˣ�Ӧ��Ϊsigaction�ṹ�е�sa_sigactionָ��������������Ӧ��Ϊsa_handler
	ָ���źŴ��������������øñ�־��ú������塣��ʹΪsa_sigactionָ�����źŴ������������
	����SA_SIGINFO���źŴ�����ͬ�����ܵõ��źŴ��ݹ��������ݣ����źŴ������ж���Щ��Ϣ�ķ��ʶ������¶δ���Segmentation fault����
	*/
	sigfillset(&act.sa_mask);//sa_maskָ�źŴ������ִ������Щ�ź�Ӧ�ñ�������ȱʡ����µ�ǰ�źű�������
	assert(sigaction(sig, &act, 0)!=-1);
}
void timer_handler()
{
	timer_lst.tick();
	//��ʱ�������� ����tick����
	alarm(TIMESHOT);
}

//��ʱ���ص����� ɾ���ǻ����socket�ϵ�ע���¼�
void cb_func(client_data* user_data)
{
	epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
	assert(user_data);
	close(user_data->sockfd);
	printf("close fd %d\n", user_data->sockfd);
}
int main(int argc, char* argv[])
{
	const char* ip = argv[1];
	int port = atoi(argv[2]);
	int ret = 0;
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
		printf("bind error %d\n", errno);
		exit(1);
	}
	ret = listen(listenfd, 5);
	assert(ret != -1);

	epoll_event events[MAX_EVENT_NUMBER];
	int epollfd = epoll_create(5);//epoll size��epollʵ���Ĵ�С��
	assert(epollfd != -1);
	addfd(epollfd, listenfd);

	ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipe);
	assert(ret != -1);
	setnoblocking(pipe[0]);
	setnoblocking(pipe[1]);

	addsig(SIGALRM);
	addsig(SIGTERM);
	bool stop_server = false;

	client_data* user = new client_data[FD_LIMIT];
	bool timeout = false;
	alarm(TIMESHOT);
	while (!stop_server)
	{
		int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
		if (num < 0 && errno != EINTR)
		{
			printf("epoll falire\n");
			break;
		}
		for (int i = 0; i < num; ++i)
		{
			int sockfd = events[i].data.fd;
			if (sockfd == listenfd)
			{
				sockaddr_in clntAddr;
				socklen_t soclen;
				int connfd = accept(sockfd, (sockaddr*)&clntAddr, &soclen);
				assert(connfd != -1);
				addfd(epollfd, connfd);
				user[connfd].address = clntAddr;
				user[connfd].sockfd = connfd;
				util_timer* timer = new util_timer;
				timer->userData = &user[connfd];
				timer->cd_func = cb_func;
				time_t cur = time(0);
				timer->expire = cur + 3 * TIMESHOT;
				user[connfd].timer = timer;
				timer_lst.add_timer(timer);
			}
			else if (sockfd == pipe[0] && events[i].events & EPOLLIN)
			{
				int sig;
				char signal[1024];
				ret = recv(sockfd, signal,1024,0);
				if (ret == -1)
				{
					continue;
				}
				else if (ret == 0)
				{
					continue;
				}
				else
				{
					for (int i = 0; i < ret; ++i)
					{
						switch (signal[i])
						{
						case SIGALRM:
						{
							timeout = true;
							break;//ʹ��timeout��������ж�ʱ������Ҫ�������ǲ���Ҫ����������Ϊ���ȼ����Ǻܸߣ�
						}
						case SIGTERM:
						{
							stop_server = true;
						}
						default:
							break;
						}
					}
				}
			}
			else if (events[i].events && EPOLLIN)
			{
				memset(user[sockfd].buf, '\0', BUFFER_SIZE);
				ret = recv(sockfd, user[sockfd].buf, BUFFER_SIZE - 1, 0);
				printf("get %d bytes of data %s from %d\n", ret, user[sockfd].buf, sockfd);

				util_timer* timer = user[sockfd].timer;
				if (ret < 0)//������ �ر����� �Ƴ���ʱ��
				{
					if (errno != EAGAIN)
					{
						cb_func(&user[sockfd]);
						if (timer)
							timer_lst.del_timer(timer);
					}
				}
				else if (ret == 0)
				{
					cb_func(&user[sockfd]);
					if (timer)
						timer_lst.del_timer(timer);
				}
				else
				{
					//���ĳ���ͻ������������ݿɶ�����������Ҫ������ʱ��
					if (timer)
					{
						time_t cur = time(0);
						timer->expire = cur + 3 * TIMESHOT;
						printf("adjust timer once\n");
						timer_lst.adjust_timer(timer);
					}
				}
			}
		}
		if (timeout)
		{
			timer_handler();
			timeout = false;
		}
	}
	close(listenfd);
	close(pipe[1]);
	close(pipe[0]);
	delete[] user;
	return 0;
}