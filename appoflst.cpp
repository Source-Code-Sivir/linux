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
	act.sa_flags |= SA_RESTART;//一旦给信号设置了SA_RESTART标记，那么当执行某个阻塞系统调用时，收到该信号时，进程不会返回，而是重新执行该系统调用。
	/*sa_flags中包含了许多标志位，包括刚刚提到的SA_NODEFER及SA_NOMASK标志位。另一个
	比较重要的标志位是SA_SIGINFO，当设定了该标志位时，表示信号附带的参数可以被传递到信
	号处理函数中，因此，应该为sigaction结构中的sa_sigaction指定处理函数，而不应该为sa_handler
	指定信号处理函数，否则，设置该标志变得毫无意义。即使为sa_sigaction指定了信号处理函数，如果不
	设置SA_SIGINFO，信号处理函数同样不能得到信号传递过来的数据，在信号处理函数中对这些信息的访问都将导致段错误（Segmentation fault）。
	*/
	sigfillset(&act.sa_mask);//sa_mask指信号处理程序执行中哪些信号应该被阻塞。缺省情况下当前信号本身被阻塞
	assert(sigaction(sig, &act, 0)!=-1);
}
void timer_handler()
{
	timer_lst.tick();
	//定时处理任务 调用tick函数
	alarm(TIMESHOT);
}

//定时器回调函数 删除非活动连接socket上的注册事件
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
	int epollfd = epoll_create(5);//epoll size是epoll实例的大小？
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
							break;//使用timeout变量标记有定时任务需要处理，但是不需要立即处理，因为优先级不是很高；
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
				if (ret < 0)//读错误 关闭连接 移除定时器
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
					//如果某个客户连接上有数据可读，则我们需要调整计时器
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