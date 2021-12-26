//逻辑单元的一种高校编程方法有限状态转移机
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<assert.h>
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<errno.h>
#include<string.h>
#include<fcntl.h>
#define BUFFER_SIZE 4096

//主状态机的两种可能状态 当前正在分析请求行 当前正在分析头部字段
enum CHECK_STATE{ CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER};


//从状态机的三种可能 行的读取状态 读取到一个完整的行 行出错 行数据尚不完整
enum LINE_STATUS
{
	LINE_OK = 0,
	LINE_BAD,
	LINE_OPEN	
};
//服务器处理HTTP请求的结果 NO_REQUEST表示请求不完整，需要继续读取客户数据；GET_REQUEST表示获得了一个完整的客户请求，BAD_REQUEST表示客户请求有
//语法错误；FORBIDDEN_REQUEST表示客户访问权限不足；INTRENAL_ERROR表示服务器内部错误，CLOSED_CONNECTION表示客户端已经关闭连接
enum HTTP_CODE
{
	NO_REQUEST,GET_REQUEST,BAD_REQUEST,FORBIDDEN_REQUEST,INTRENAL_ERROR,CLOSED_CONNECTION
};
/*我们没有给客户端发送一个完整的http应答报文，只是根据服务器的处理结果发送成功或者失败信息*/
static const char* szret[] = { "I get a correct result\n","Something wrong\n" };

//从状态机 解析出一行内容
LINE_STATUS parse_line(char* buffer, int& checked_index, int& read_index)
{
	char temp;
	/*checked_index指向buffer（读缓冲区中）正在解析的字节，read_index指向buffer中客户数据尾部的下一字节。buffer中第0-checked_index字节已经分析
	完毕，第checked_index-read_index-1的字节由下面的程序逐个分析*/
	for (; checked_index < read_index; ++checked_index)
	{
		//获取当前要分析的字节
		temp = buffer[checked_index];
		//如果当前的字符是\r回车符，说明可能读取到一个完整的行
		if (temp == '\r')
		{
			/*如果回车符是目前buffer中最后一个被读入的客户数据，那么这次分析没有读取到一个完整的行，返回lineopen表示还需要继续读取客户数据才
			能进一步分析*/
			if (checked_index + 1 == read_index)
				return LINE_OPEN;
			else if (buffer[checked_index + 1] == '\n')
			{
				//下一个字符是\n说明我们读取到一个完整的行

				buffer[checked_index++] = '\0';
				buffer[checked_index++] = '\0';
				return LINE_OK;
			}
			//否则说明客户发送的http有问题
			return LINE_BAD;
		}
		else if (temp == '\n')//当前是换行符，说明可能读取了一个完整的行
		{
			if ((checked_index > 1) && buffer[checked_index - 1] == '\r')
			{
				buffer[checked_index++] = '\0';
				buffer[checked_index++] = '\0';
				return LINE_OK;//  、\r\n  windows下换行符
			}
			return LINE_BAD;
		}
		
	}
	return LINE_OPEN;//所有内容分析完毕也没有遇到'\r‘说明还需要更多数据进行进一步分析
}
//分析请求行
HTTP_CODE parse_requestline(char* temp, CHECK_STATE& checkState)
{
	char* url = strpbrk(temp, " \t");//如果请求中没有空白字符或者\t http请求必定有问题  找出第一个str1中str2中出现的元素下标
	if (!url)
		return BAD_REQUEST;
	*url++ = '\0';

	char* method = temp;
	if (strcasecmp(method, "GET") == 0)//仅支持get方法 忽略大小写的比较字符串
	{
		printf("The request method is get \n");
	}
	else
		return BAD_REQUEST;
	url += strspn(url, " \t"); //检索str1中第一个不在str2中出现的字符下标  找到url中第一个不是空格或者\t的字符 省略空格

	char* version = strpbrk(url, " \t");//找出下一个空格的位置
	*version++ = '\0';

	version += strspn(version, " \t");

	if (strcasecmp(version, "HTTP/1.1") != 0)
		return BAD_REQUEST;
	if (strncasecmp(url, "http://", 7) == 0)
	{
		url += 7;
		url = strchr(url, '/');
	}
	if (!url || url[0] != '/')
		return BAD_REQUEST;
	printf("request url is%s\n", url);
	checkState = CHECK_STATE_HEADER;
	return NO_REQUEST;
}
//分析头部字段
HTTP_CODE parse_headers(char* temp)
{
	if (temp[0] == '\0')
		return GET_REQUEST;//遇到空行 得到正确的http请求
	else if (strncasecmp(temp, "Host:", 5) == 0)
	{
		temp += 5;
		temp += strspn(temp, " \t");
		printf("request host is %s\n", temp);
	}
	else
		printf("cannot handle\n");
	return NO_REQUEST;
}

HTTP_CODE parse_content(char* buffer, int& checked_index, CHECK_STATE& checkstate, int& read_index, int& start_line)
{
	LINE_STATUS linestatus = LINE_OK;//记录当前行的读取状态
	HTTP_CODE retcode = NO_REQUEST;//记录http请求的处理结果

	while ((linestatus = parse_line(buffer, checked_index, read_index)) == LINE_OK)
	{
		char* temp = buffer + start_line;//startline 是行在buffer中的起始位置
		start_line = checked_index;//记录下一行的起始位置
		//checkstate 记录主状态机当前的状态
		switch (checkstate)
		{
		case CHECK_STATE_REQUESTLINE://第一个状态 分析请求行
		{
			retcode = parse_requestline(temp, checkstate);
			if (retcode==BAD_REQUEST)
			{
				return BAD_REQUEST;
			}
			break;
		}
		case CHECK_STATE_HEADER:
		{
			retcode = parse_headers(temp);
			if (retcode == BAD_REQUEST)
			{
				return BAD_REQUEST;
			}
			else if (retcode == GET_REQUEST)
			{
				return GET_REQUEST;
			}
			break;
		}
		default:
		{
			return INTRENAL_ERROR;
		}
			break;
		}
	}
	//如果没有读取到一个完整的行，表示客户端还需要继续读取客户数据
	if (linestatus == LINE_OPEN)
		return NO_REQUEST;
	else
		return BAD_REQUEST;
}
int main(int argc, char* argv[])
{
	if (argc < 2) exit(1);
	const char* ip = argv[1];
	int port = atoi(argv[2]);

	sockaddr_in address;
	bzero(&address, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = inet_addr(ip);
	address.sin_port = htons(port);

	int listenfd = socket(AF_INET, SOCK_STREAM, 0);
	assert(listenfd >= 0);
	int ret = bind(listenfd,(sockaddr *) & address, sizeof(address));
	assert(ret != -1);
	ret = listen(listenfd, 5);
	assert(ret != -1);
	sockaddr_in clntAddr;
	socklen_t addrlen;
	int fd = accept(listenfd, (sockaddr*)&clntAddr, &addrlen);
	if (fd < 0)
	{
		printf("errno is%d\n", errno);
	}
	else
	{
		char buffer[BUFFER_SIZE];
		memset(buffer, '\0', BUFFER_SIZE);
		int data_read = 0;
		int read_index = 0;
		int checked_index = 0;
		int start_line = 0;//行在buffer中的起始位置

		//设置主状态机的起始状态
		CHECK_STATE checkstate = CHECK_STATE_REQUESTLINE;
		while (1)
		{
			data_read = recv(fd, buffer, BUFFER_SIZE - read_index, 0);
			if (data_read == -1)
			{
				printf("reading fail\n");
				break;
			}
			else if (data_read == 0)
			{
				printf("clnt closed\n");
				break;
			}
			read_index += data_read;

			HTTP_CODE result = parse_content(buffer, checked_index, checkstate, read_index, start_line);
			if (result == NO_REQUEST)
				continue;
			else if (result==GET_REQUEST)
			{
				send(fd, szret[0], strlen(szret[0]), 0);
				break;
			}
			else
			{
				send(fd, szret[1], strlen(szret[1]), 0);
				break;
			}
		}
		close(fd);
	}
	close(listenfd);
	return 0;
}

//主状态机在内部调用从状态机