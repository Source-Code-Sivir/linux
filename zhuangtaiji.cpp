//�߼���Ԫ��һ�ָ�У��̷�������״̬ת�ƻ�
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

//��״̬�������ֿ���״̬ ��ǰ���ڷ��������� ��ǰ���ڷ���ͷ���ֶ�
enum CHECK_STATE{ CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER};


//��״̬�������ֿ��� �еĶ�ȡ״̬ ��ȡ��һ���������� �г��� �������в�����
enum LINE_STATUS
{
	LINE_OK = 0,
	LINE_BAD,
	LINE_OPEN	
};
//����������HTTP����Ľ�� NO_REQUEST��ʾ������������Ҫ������ȡ�ͻ����ݣ�GET_REQUEST��ʾ�����һ�������Ŀͻ�����BAD_REQUEST��ʾ�ͻ�������
//�﷨����FORBIDDEN_REQUEST��ʾ�ͻ�����Ȩ�޲��㣻INTRENAL_ERROR��ʾ�������ڲ�����CLOSED_CONNECTION��ʾ�ͻ����Ѿ��ر�����
enum HTTP_CODE
{
	NO_REQUEST,GET_REQUEST,BAD_REQUEST,FORBIDDEN_REQUEST,INTRENAL_ERROR,CLOSED_CONNECTION
};
/*����û�и��ͻ��˷���һ��������httpӦ���ģ�ֻ�Ǹ��ݷ������Ĵ��������ͳɹ�����ʧ����Ϣ*/
static const char* szret[] = { "I get a correct result\n","Something wrong\n" };

//��״̬�� ������һ������
LINE_STATUS parse_line(char* buffer, int& checked_index, int& read_index)
{
	char temp;
	/*checked_indexָ��buffer�����������У����ڽ������ֽڣ�read_indexָ��buffer�пͻ�����β������һ�ֽڡ�buffer�е�0-checked_index�ֽ��Ѿ�����
	��ϣ���checked_index-read_index-1���ֽ�������ĳ����������*/
	for (; checked_index < read_index; ++checked_index)
	{
		//��ȡ��ǰҪ�������ֽ�
		temp = buffer[checked_index];
		//�����ǰ���ַ���\r�س�����˵�����ܶ�ȡ��һ����������
		if (temp == '\r')
		{
			/*����س�����Ŀǰbuffer�����һ��������Ŀͻ����ݣ���ô��η���û�ж�ȡ��һ���������У�����lineopen��ʾ����Ҫ������ȡ�ͻ����ݲ�
			�ܽ�һ������*/
			if (checked_index + 1 == read_index)
				return LINE_OPEN;
			else if (buffer[checked_index + 1] == '\n')
			{
				//��һ���ַ���\n˵�����Ƕ�ȡ��һ����������

				buffer[checked_index++] = '\0';
				buffer[checked_index++] = '\0';
				return LINE_OK;
			}
			//����˵���ͻ����͵�http������
			return LINE_BAD;
		}
		else if (temp == '\n')//��ǰ�ǻ��з���˵�����ܶ�ȡ��һ����������
		{
			if ((checked_index > 1) && buffer[checked_index - 1] == '\r')
			{
				buffer[checked_index++] = '\0';
				buffer[checked_index++] = '\0';
				return LINE_OK;//  ��\r\n  windows�»��з�
			}
			return LINE_BAD;
		}
		
	}
	return LINE_OPEN;//�������ݷ������Ҳû������'\r��˵������Ҫ�������ݽ��н�һ������
}
//����������
HTTP_CODE parse_requestline(char* temp, CHECK_STATE& checkState)
{
	char* url = strpbrk(temp, " \t");//���������û�пհ��ַ�����\t http����ض�������  �ҳ���һ��str1��str2�г��ֵ�Ԫ���±�
	if (!url)
		return BAD_REQUEST;
	*url++ = '\0';

	char* method = temp;
	if (strcasecmp(method, "GET") == 0)//��֧��get���� ���Դ�Сд�ıȽ��ַ���
	{
		printf("The request method is get \n");
	}
	else
		return BAD_REQUEST;
	url += strspn(url, " \t"); //����str1�е�һ������str2�г��ֵ��ַ��±�  �ҵ�url�е�һ�����ǿո����\t���ַ� ʡ�Կո�

	char* version = strpbrk(url, " \t");//�ҳ���һ���ո��λ��
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
//����ͷ���ֶ�
HTTP_CODE parse_headers(char* temp)
{
	if (temp[0] == '\0')
		return GET_REQUEST;//�������� �õ���ȷ��http����
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
	LINE_STATUS linestatus = LINE_OK;//��¼��ǰ�еĶ�ȡ״̬
	HTTP_CODE retcode = NO_REQUEST;//��¼http����Ĵ�����

	while ((linestatus = parse_line(buffer, checked_index, read_index)) == LINE_OK)
	{
		char* temp = buffer + start_line;//startline ������buffer�е���ʼλ��
		start_line = checked_index;//��¼��һ�е���ʼλ��
		//checkstate ��¼��״̬����ǰ��״̬
		switch (checkstate)
		{
		case CHECK_STATE_REQUESTLINE://��һ��״̬ ����������
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
	//���û�ж�ȡ��һ���������У���ʾ�ͻ��˻���Ҫ������ȡ�ͻ�����
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
		int start_line = 0;//����buffer�е���ʼλ��

		//������״̬������ʼ״̬
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

//��״̬�����ڲ����ô�״̬��