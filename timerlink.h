#ifndef LST_TIMER
#define LST_TIMER
#endif // !LST_TIMER

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
#include<time.h>

#define BUFFER_SIZE 64
class util_timer;
//升序定时器链表
struct client_data
{
	sockaddr_in address;
	int sockfd;
	char buf[BUFFER_SIZE];
	util_timer* timer;
};

class util_timer//定时器类
{
public:
	util_timer();
	~util_timer();

public:
	time_t expire;
	void (*cd_func)(client_data*);//任务回调函数
	client_data* userData;
	util_timer* prev;
	util_timer* next;
};

util_timer::util_timer():prev(nullptr),next(nullptr)
{
}

class sort_timer_list
{
public:
	sort_timer_list():head(NULL),tail(NULL){};
	~sort_timer_list()
	{
		while (head)
		{
			util_timer* temp = head;
			head = head->next;
			delete temp;
		}
	}

	//将目标定时器插入到链表中
	void add_timer(util_timer* timer)
	{
		if (timer == nullptr)
			return;
		if (head == nullptr)
		{
			head = tail = timer;
			return;
		}
		if (timer->expire < head->expire)
		{
			timer->next = head;
			head->prev = timer;
			head = timer;
			return;
		}
		add_timer(timer, head);
	}

	//当某个定时任务发生变化时，需要调整定时器在链表中的位置 只考虑定时延长的情况，即定时器需要往链表尾部移动
	void adjust_timer(util_timer* timer)
	{
		if (timer == NULL)
			return;
		util_timer* temp = timer->next;
		if (temp == NULL || timer->expire < temp->expire)
		{
			return;
		}
		else
		{
			timer->prev->next = temp;
			temp->prev = timer->prev;
			add_timer(timer, timer->next);
		}
	}
	void del_timer(util_timer* timer)
	{
		if (timer == NULL) return;
		if (timer == head&&timer==tail)
		{
			head = NULL;
			tail = NULL;
			delete timer;
			return;
		}
		if (timer == head)
		{
			head = head->next;
			delete timer;
			head->prev = NULL;
			return;
		}
		if (timer == tail)
		{
			tail = tail->prev;
			tail->next = NULL;
			delete timer;
			return;
		}
		timer->prev->next = timer->next;
		timer->next->prev = timer->prev;
		delete timer;
	}
	void tick()
	{
		if (head == NULL)
			return;
		printf("time tick\n");
		time_t cur = time(0);
		util_timer* temp = head;
		while (temp)
		{
			if (temp->expire < cur)
			{
				temp->cd_func(temp->userData);
				head = temp->next;
				if (head)
					head->prev = NULL;
				temp = temp->next;
				delete temp;
			}
			else break;
		}
	}
private:
	void add_timer(util_timer* timer, util_timer* cur)
	{
		util_timer* pre = cur;
		util_timer* next = cur->next;
		while (next&&next->expire<timer->expire)
		{
			pre = next;
			next = next->next;
		}
		if (next == NULL)
		{
			tail->next = timer;
			timer->prev = tail;
			tail = timer;
			return;
		}
		pre->next = timer;
		timer->prev = pre;
		timer->next = next;
		next->prev = timer;
	}
private:
	util_timer* head;
	util_timer* tail;
};
//实际应用 处理非活动连接