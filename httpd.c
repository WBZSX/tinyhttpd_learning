/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */

#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>

typedef struct s_thread_list_t
{
    struct s_thread_list_t* next;
    pthread_t threadID;
    int socketfd;
}s_thread_list_t;

s_thread_list_t* thread_head;

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char *);
void execute_cgi(int, const char *, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void serve_file(int, const char *);
int startup(u_short *);
void unimplemented(int);

static void list_pop(s_thread_list_t* threadNode);

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
void* accept_request(void *pMsg)
{
    char buf[1024];
    int numchars;
    char method[255];
    char url[255];
    char path[512];
    size_t i, j;
    struct stat st;
    int cgi = 0;      /* becomes true if server decides this is a CGI
                        * program */
    char *query_string = NULL;

    s_thread_list_t thread_node;
    memcpy(&thread_node, pMsg, sizeof(s_thread_list_t));

    int client = thread_node.socketfd;

    numchars = get_line(client, buf, sizeof(buf)); // 这个函数的作用是遇到回车换行符就终止读取字符串，并返回
    i = 0; 
    j = 0;
    while (!ISspace(buf[j]) && (i < sizeof(method) - 1)) // while循环的作用是读取客户端的请求，遇到'空格符'后跳出循环,为何长度为255字节，这部分我也不确定？
    {
        method[i] = buf[j];
        i++; 
        j++;
    }
    method[i] = '\0';

    if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) 
    {
        unimplemented(client); //如果客户端的请求字符串中同时包含get和post，则进入unimplemented()函数
        return;
    }

    if (strcasecmp(method, "POST") == 0) 
    {
        cgi = 1; // 如果客户端发来的是POST请求，则cgi = 1
    }

    i = 0;
    while (ISspace(buf[j]) && (j < sizeof(buf))) // 继续从buf的第255个字节依次读取，遇到'非空格符'跳出循环
    {
        j++;
    }

    /* 
        跳过空格，继续往下读取字符
        直到遇到'空格符'，或者长度到达255个字节，或者读取到buff的末尾，
        跳出循环
        url数组的长度为255个字节，请参考文献：https://www.cnblogs.com/zxwBj/p/8689962.html
    */
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf))) 
    {
        url[i] = buf[j];
        i++; 
        j++;
    }
    url[i] = '\0';

    if (strcasecmp(method, "GET") == 0) // 判断method中是否为GET请求
    {
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0')) // 循环遍历url字符串，直到遇到'?'或者到url字符串末端，则跳出循环
        {
            query_string++;
        }

        if (*query_string == '?')
        {
            cgi = 1;
            *query_string = '\0'; // 如果url中遇到字符'?'，则将该字符换为'\0'
            query_string++; // 将指针向后移动一个字节
        }
    }

    sprintf(path, "htdocs%s", url);
    if (path[strlen(path) - 1] == '/')
    {
        strcat(path, "index.html");  // 拼接字符串，格式为：htdocs+url/index.html
    }
    
    if (stat(path, &st) == -1)  //获取path指向的文件信息，并将文件信息保存到结构体st中。返回值为-1表示获取文件信息失败
    {
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers 读取并抛弃header信息*/
        {
            numchars = get_line(client, buf, sizeof(buf));
        }
        not_found(client);
    }
    else
    {
        // 如果path仅仅是一个路径，那么在path后面添加index.html
        if ((st.st_mode & S_IFMT) == S_IFDIR)
        {
            strcat(path, "/index.html"); 
        }

        /* 
            判断index.html文件执行权限的范围
            S_IXUSR: 文件所有者具有可执行的权限
            S_IXGRP: 用户组具备可执行的权限
            S_IXOTH: 其他用户具备可执行的权限
            参考资料：https://blog.csdn.net/qq_40839779/article/details/82789217
        */
        if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH))
        {
            cgi = 1;
        }

        if (!cgi)
        {
            serve_file(client, path);
        }
        else
        {
            execute_cgi(client, path, method, query_string);
        }
    }

    close(client); //客户端请求处理完毕，线程结束，关闭socket
    list_pop(&thread_node);
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n"); // http 400表示服务器无法理解客户端的请求
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(int client, FILE *resource)
{
    char buf[1024];

    fgets(buf, sizeof(buf), resource);
    while (!feof(resource))
    {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n"); //http 500表示服务器内部发生错误，无法完成请求
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc)
{
    perror(sc);  //该函数用于输出标准错误，返回消息的格式为 sc :<error msg>.参考：https://www.runoob.com/cprogramming/c-function-perror.html
    exit(1);   //由于非正常原因，退出进程
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
void execute_cgi(int client, const char *path,
                 const char *method, const char *query_string)     
{
    char buf[1024];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;

    buf[0] = 'A'; 
    buf[1] = '\0';
    if (strcasecmp(method, "GET") == 0)
    {
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
        {
            numchars = get_line(client, buf, sizeof(buf));
        }
    }
    else    /* POST */
    {
        numchars = get_line(client, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf))
        {
            buf[15] = '\0';
            if (strcasecmp(buf, "Content-Length:") == 0)
            {
                content_length = atoi(&(buf[16]));
            }
            numchars = get_line(client, buf, sizeof(buf));
        }

        if (content_length == -1)
        {
            bad_request(client);
            return;
        }
    }

    sprintf(buf, "HTTP/1.0 200 OK\r\n"); //http 200表示服务端已经成功处理请求
    send(client, buf, strlen(buf), 0);

    if (pipe(cgi_output) < 0)
    {
        cannot_execute(client);
        return;
    }

    if (pipe(cgi_input) < 0)
    {
        cannot_execute(client);
        return;
    }

    if ( (pid = fork()) < 0 )
    {
        cannot_execute(client);
        return;
    }

    if (pid == 0)  /* child: CGI script */
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        /* 
            http://www.01happy.com/c-dup-dup2/ 
        */
        dup2(cgi_output[1], 1);
        dup2(cgi_input[0], 0);
        close(cgi_output[0]);
        close(cgi_input[1]);

        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);
        if (strcasecmp(method, "GET") == 0)
        {
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else
        {   /* POST */
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }

        /* 
            https://www.jb51.net/article/71734.html 
        */
        execl(path, path, NULL);
        exit(0);
    }
    else
    {    /* parent */
        close(cgi_output[1]);
        close(cgi_input[0]);
        if (strcasecmp(method, "POST") == 0) // strcasecmp()用于比较字符串，并忽略大小写
        {
            for (i = 0; i < content_length; i++)
            {
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        }

        while (read(cgi_output[0], &c, 1) > 0)
        {
            send(client, &c, 1, 0);
        }

        close(cgi_output[0]);
        close(cgi_input[1]);
        waitpid(pid, &status, 0);
    }
}

/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
int get_line(int sock , char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n'))
    {
        n = recv(sock, &c, 1, 0); // 0表示从tcp buffer中读取数据到buffer中，并从tcp buffer中移除已读取的数据
        /* DEBUG printf("%02X\n", c); */
        if (n > 0)
        {
            /* 
              '\r'代表回车符，回到当前行的行首，而不会换到下一行；
              '\n'代表换行符，换到当前位置的下一行，而不会回到行首；
              linux中\n代表回车+换行；
              windows中\r\n表示回车+换行;
              Mac中\r表示回车+换行

              该if判断的作用是将请求中的\r全部换为\n
            */ 
            if (c == '\r') 
            {
                /* MSG_PEEK表示从tcp buffer中的数据读取到buffer中，但是并不把已读取的数据从tcp buffer中移除 */
                n = recv(sock, &c, 1, MSG_PEEK); // 如果上一个字符为'\r'，则判断当前字符是否为'\n'。
                /* DEBUG printf("%02X\n", c); */
                if ((n > 0) && (c == '\n'))
                {
                    recv(sock, &c, 1, 0); // 如果当前字符为'\n'，则继续读取下一个字符
                }
                else
                {    
                    c = '\n';
                }
            }
            buf[i] = c;
            i++;
        }
        else
        {
            c = '\n';
        }
    }
    buf[i] = '\0';
 
 return(i);
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void headers(int client, const char *filename)
{
    char buf[1024];
    (void)filename;  /* could use filename to determine file type */

    strcpy(buf, "HTTP/1.0 200 OK\r\n"); // http的状态码为200，表示服务器已经成功处理了客户端的请求
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
void not_found(int client)
{
    char buf[1024];


    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n"); // 给客户端回复404，表示服务器未找到客户端请求的网页
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
void serve_file(int client, const char *filename)
{
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    buf[0] = 'A'; 
    buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers 这行代码的条件是如何丢弃掉header？？？*/
    {
        numchars = get_line(client, buf, sizeof(buf));
    }

    resource = fopen(filename, "r");
    if (resource == NULL)
    {
        not_found(client); // 如果文件为空，则返回404错误
    }
    else
    {
        headers(client, filename); // 返回http 200给客户端表示成功处理了请求
        cat(client, resource); // 读取文件的内容，并发送给客户端
    }
    fclose(resource);
}

/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
int startup(u_short *port)
{
    int httpd = 0;
    struct sockaddr_in name;

    httpd = socket(PF_INET, SOCK_STREAM, 0);   //创建socket，并返回socket文件标识符，SOCK_STREAM表明socket使用字节流传输数据
    if (httpd == -1) //校验socket是否创建成功，如果失败则报错
    {
          error_die("socket"); //报错，该函数的重点在内部的perror()函数
          memset(&name, 0, sizeof(name));
    }

    name.sin_family = AF_INET;   //AF_INET代表IPv4协议，AF_INET6代表IPv6协议
    name.sin_port = htons(*port);  //转换端口的数据排列方式，即将主机字节序转换为网络字节序，也就是数据大小端的转换。h代表host，n代表network，s代表short，l代表 long
    name.sin_addr.s_addr = htonl(INADDR_ANY);  //INADDR_ANY代表IP地址"0.0.0.0"
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0) //服务端的socket创建成功后，需要将本地IP地址与端口号进行绑定
    {
      error_die("bind");
    }
  
    if (*port == 0)  /* if dynamically allocating a port */
    {
      int namelen = sizeof(name);
      if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)  //如果端口为0，那么客户端与服务端通信时，该函数能够获取当前正在通信的socket的IP地址和端口号。
      {
          error_die("getsockname");
      }
    
      *port = ntohs(name.sin_port); //网络字节序转换为主机字节序
    }
    if (listen(httpd, 5) < 0)  //绑定成功后，监听该socekt,第二个参数表示请求队列中允许的最大请求数，即允许客户端请求数最大为5个
    {
        error_die("listen");
    }
    return(httpd);
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n"); //http的错误状态码501表示服务器不具备完成请求的功能
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/

#if 0
int main(void)
{
    int server_sock = -1; 
    u_short port = 50164;
    int client_sock = -1;
    struct sockaddr_in client_name;
    int client_name_len = sizeof(client_name);
    pthread_t newthread;   //声明线程表示符，用于创建新线程

    server_sock = startup(&port);  //将端口号作为实参，传入startup函数中
    printf("httpd running on port %d\n", port); //打印与socket绑定的端口号

    //主线程
    while (1)
    {
      /* 
         监听socket操作完成后，服务端在accept函数这边发生阻塞，等待客户端发来连接请求，并返回一个新的client_sock文件描述符。
         之后，客户端与服务端的数据通信都是通过该socket完成。
        
         在这里停一下，前面创建、绑定和监听的socket与数据通信的socketfd不一样，你曾注意过吗？如果你现在才发现，那么去搜索答案吧
      */
      client_sock = accept(server_sock, (struct sockaddr *)&client_name, &client_name_len); 
      if(client_sock == -1)
      {
          /* 如果发生错误，整个进程全部结束。申请的资源不用回收或者释放 */
          close(server_sock);
          error_die("accept"); 
      }
      
      /* 
          每接收到一个客户端的连接请求，服务端创建一个新的线程来处理该连接请求.

          但该地方有几个问题：
            （1）子线程创建后与主线程没有消息交互，可以将子线程属性设置为分离属性
            （2）按照目前的代码结构，在退出前最好使用pthread_join()函数等待线程结束。万一某些线程没有结束，此时不应该退出进程。
            （3）代码能够接收多个客户端的连接请求，但是创建线程时，仅仅使用一个线程表示符newthread，这不符合多线程的编程逻辑，是一个不好的示范。
      */
      if (pthread_create(&newthread , NULL, accept_request, client_sock) != 0)
      {
          perror("pthread_create");
      }
    }

    /* 
       这个地方代码比较奇怪，整个进程结束后，不用回收或者释放资源。然而，这里偏偏关闭了服务端的socket。 
       但在line 509错误处理时，并未关闭服务端的socket，看着不舒服。
       在实际工作中，最好养成申请资源之后释放的好习惯。
    */
    close(server_sock);

    return(0);
}
#endif

static void list_init()
{
    thread_head->next = NULL;
    thread_head->socketfd = -1;
    return;
}

extern void list_push(s_thread_list_t* threadNode)
{
    s_thread_list_t* tmp_node;
    tmp_node = thread_head->next;

    while(NULL != tmp_node->next)
    {
        tmp_node = tmp_node->next;
    }

    tmp_node->next = threadNode;

    threadNode->next = NULL;
    return;
}

static void list_pop(s_thread_list_t* threadNode)
{
    s_thread_list_t* tmp_node;
    tmp_node = thread_head;

    while(threadNode->socketfd != tmp_node->next->socketfd)
    {
        tmp_node = tmp_node->next;
    }

    tmp_node->next = threadNode->next;
    return;
}

static void create_thread(s_thread_list_t* threadNode)
{
    pthread_attr_t thread_attr;

    //设置线程为分离属性，当线程运行结束后，资源自动回收
    pthread_attr_init(&thread_attr);

    pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);

    pthread_create(&(threadNode->threadID), &thread_attr, accept_request, threadNode);

    list_push(&threadNode);
}

int main()
{
    int server_socket = -1;
    int client_socket = -1;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    int port = 50164;
    int sin_size;
    
    thread_head = malloc(sizeof(s_thread_list_t));
    thread_head->next = NULL;

    //创建socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);

    //绑定socket
    server_addr.sin_port = htons(port);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    bind(server_socket, (struct sockaddr*) &server_addr, sizeof(server_addr));

    //监听socket
    listen(server_socket, 5);

    while(1)
    {
        //每接到一次客户端请求，则创建一个线程处理
        client_socket = accept(server_socket, (struct sockaddr*) &client_addr, &sin_size);
        s_thread_list_t thread_node;
        thread_node.socketfd = client_socket;
        thread_node.next = NULL;

        //为每一个连接请求，创建资源
        create_thread(&thread_node);

        if(thread_head->next == NULL)
        {
            break;
        }
    }

    return 0;
}
