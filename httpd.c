/* J. David's webserver */
/* 杰·大卫的 Web 服务器 */
/* This is a simple webserver.
 * 这是一个简单的 Web 服务器。
 * Created November 1999 by J. David Blackstone.
 * 由 J. David Blackstone 于 1999 年 11 月创建。
 * CSE 4344 (Network concepts), Prof. Zeigler
 * CSE 4344（网络概念），Zeigler 教授
 * University of Texas at Arlington
 * 德克萨斯大学阿灵顿分校
 */
/* This program compiles for Sparc Solaris 2.6.
 * 本程序可在 Sparc Solaris 2.6 上编译。
 * To compile for Linux:
 * 要在 Linux 上编译：
 *  1) Comment out the #include <pthread.h> line.
 *     注释掉 #include <pthread.h> 这一行。
 *  2) Comment out the line that defines the variable newthread.
 *     注释掉定义变量 newthread 的那一行。
 *  3) Comment out the two lines that run pthread_create().
 *     注释掉运行 pthread_create() 的两行代码。
 *  4) Uncomment the line that runs accept_request().
 *     取消注释运行 accept_request() 的那一行。
 *  5) Remove -lsocket from the Makefile.
 *     从 Makefile 中移除 -lsocket。
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
#include <stdint.h>

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"
#define STDIN   0
#define STDOUT  1
#define STDERR  2

void accept_request(void *);
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

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * 有请求到达服务器端口，导致 accept() 返回。适当地处理该请求。
 * Parameters: the socket connected to the client
 * 参数：连接到客户端的套接字 */
/**********************************************************************/
void accept_request(void *arg)
{
    int client = (intptr_t)arg;
    char buf[1024];
    size_t numchars;
    char method[255];
    char url[255];
    char path[512];
    size_t i, j;
    struct stat st;
    int cgi = 0;      /* becomes true if server decides this is a CGI
                       * program */
                       /* 如果服务器判定这是一个 CGI 程序，则该值变为真 */
    char *query_string = NULL;

    /* 从客户端读取第一行请求数据（请求行） */
    numchars = get_line(client, buf, sizeof(buf));
    i = 0; j = 0;
    /* 解析 HTTP 方法（GET 或 POST） */
    while (!ISspace(buf[i]) && (i < sizeof(method) - 1))
    {
        method[i] = buf[i];
        i++;
    }
    j=i;
    method[i] = '\0';

    /* 如果请求方法不是 GET 也不是 POST，则返回未实现 */
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {
        unimplemented(client);
        return;
    }

    /* POST 请求默认需要执行 CGI */
    if (strcasecmp(method, "POST") == 0)
        cgi = 1;

    /* 跳过方法后的空白字符，开始解析 URL */
    i = 0;
    while (ISspace(buf[j]) && (j < numchars))
        j++;
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < numchars))
    {
        url[i] = buf[j];
        i++; j++;
    }
    url[i] = '\0';

    /* 对于 GET 方法，处理查询字符串（? 后面的参数） */
    if (strcasecmp(method, "GET") == 0)
    {
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        if (*query_string == '?')
        {
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }

    /* 将 URL 映射到服务器本地的文件路径 */
    sprintf(path, "htdocs%s", url);
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");
    /* 获取文件状态信息，判断文件是否存在 */
    if (stat(path, &st) == -1) {
        /* 文件不存在，读取并丢弃剩余请求头，然后返回 404 */
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
                                                      /* 读取并丢弃请求头 */
            numchars = get_line(client, buf, sizeof(buf));
        not_found(client);
    }
    else
    {
        /* 如果路径是目录，默认追加 index.html */
        if ((st.st_mode & S_IFMT) == S_IFDIR)
            strcat(path, "/index.html");
        /* 如果文件具有可执行权限，则作为 CGI 程序执行 */
        if ((st.st_mode & S_IXUSR) ||
                (st.st_mode & S_IXGRP) ||
                (st.st_mode & S_IXOTH)    )
            cgi = 1;
        /* 根据是否为 CGI 决定处理方式 */
        if (!cgi)
            serve_file(client, path);
        else
            execute_cgi(client, path, method, query_string);
    }

    /* 关闭客户端连接 */
    close(client);
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * 通知客户端其发送的请求存在问题。
 * Parameters: client socket
 * 参数：客户端套接字 */
/**********************************************************************/
void bad_request(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
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
 * 将文件的完整内容输出到套接字。该函数以 UNIX "cat" 命令命名，
 * 因为使用 pipe、fork 和 exec("cat") 也许会更简单。
 * Parameters: the client socket descriptor
 *             客户端套接字描述符
 *             FILE pointer for the file to cat
 *             要输出的文件指针 */
/**********************************************************************/
void cat(int client, FILE *resource)
{
    char buf[1024];

    /* 逐行读取文件并发送到客户端 */
    fgets(buf, sizeof(buf), resource);
    while (!feof(resource))
    {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * 通知客户端 CGI 脚本无法执行。
 * Parameter: the client socket descriptor.
 * 参数：客户端套接字描述符 */
/**********************************************************************/
void cannot_execute(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
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
 * program indicating an error.
 * 使用 perror() 打印错误信息（用于系统错误；基于 errno 的值，
 * 表示系统调用错误）并以错误状态退出程序。 */
/**********************************************************************/
void error_die(const char *sc)
{
    perror(sc);
    exit(1);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * 执行 CGI 脚本。需要按需设置环境变量。
 * Parameters: client socket descriptor
 *             客户端套接字描述符
 *             path to the CGI script
 *             CGI 脚本的路径 */
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

    /* 初始化 buf，使其满足 while 循环的进入条件 */
    buf[0] = 'A'; buf[1] = '\0';
    /* 对于 GET 请求，读取并丢弃所有请求头 */
    if (strcasecmp(method, "GET") == 0)
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
                                                      /* 读取并丢弃请求头 */
            numchars = get_line(client, buf, sizeof(buf));
    else if (strcasecmp(method, "POST") == 0) /*POST*/
                                              /* POST 方法 */
    {
        /* 读取请求头，查找 Content-Length */
        numchars = get_line(client, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf))
        {
            buf[15] = '\0';
            if (strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&(buf[16]));
            numchars = get_line(client, buf, sizeof(buf));
        }
        if (content_length == -1) {
            /* 未找到 Content-Length，返回错误请求 */
            bad_request(client);
            return;
        }
    }
    else/*HEAD or other*/
        /* HEAD 方法或其他方法 */
    {
    }

    /* 创建用于 CGI 输出（子进程 -> 父进程）的管道 */
    if (pipe(cgi_output) < 0) {
        cannot_execute(client);
        return;
    }
    /* 创建用于 CGI 输入（父进程 -> 子进程）的管道 */
    if (pipe(cgi_input) < 0) {
        cannot_execute(client);
        return;
    }

    /* 创建子进程来执行 CGI 脚本 */
    if ( (pid = fork()) < 0 ) {
        cannot_execute(client);
        return;
    }
    /* 先发送 HTTP 200 OK 响应状态行 */
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    if (pid == 0)  /* child: CGI script */
                   /* 子进程：执行 CGI 脚本 */
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        /* 将子进程的标准输出重定向到 cgi_output 管道的写端 */
        dup2(cgi_output[1], STDOUT);
        /* 将子进程的标准输入重定向到 cgi_input 管道的读端 */
        dup2(cgi_input[0], STDIN);
        /* 关闭不需要的管道端 */
        close(cgi_output[0]);
        close(cgi_input[1]);
        /* 设置 REQUEST_METHOD 环境变量 */
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);
        if (strcasecmp(method, "GET") == 0) {
            /* GET 方法：设置 QUERY_STRING 环境变量 */
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else {   /* POST */
                 /* POST 方法：设置 CONTENT_LENGTH 环境变量 */
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        /* 执行 CGI 程序 */
        execl(path, NULL);
        exit(0);
    } else {    /* parent */
                /* 父进程 */
        /* 关闭父进程中不需要的管道端 */
        close(cgi_output[1]);
        close(cgi_input[0]);
        /* 如果是 POST 请求，将请求体数据通过管道写入子进程 */
        if (strcasecmp(method, "POST") == 0)
            for (i = 0; i < content_length; i++) {
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        /* 从 CGI 输出管道读取子进程的输出，并发送给客户端 */
        while (read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);

        /* 关闭剩余管道端并等待子进程结束 */
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
 * 从套接字读取一行。行尾可以是换行符、回车符或 CRLF 组合。
 * 读取的字符串以空字符结尾。如果在缓冲区末尾之前没有找到换行符，
 * 字符串也以空字符结尾。如果读取到上述三种行终止符之一，
 * 字符串的最后一个字符将是换行符，并以空字符结尾。
 * Parameters: the socket descriptor
 *             套接字描述符
 *             the buffer to save the data in
 *             用于保存数据的缓冲区
 *             the size of the buffer
 *             缓冲区大小
 * Returns: the number of bytes stored (excluding null)
 *          返回存储的字节数（不包括空字符） */
/**********************************************************************/
int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n'))
    {
        /* 逐个字符从套接字接收数据 */
        n = recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        if (n > 0)
        {
            if (c == '\r')
            {
                /* 遇到回车符，窥探下一个字符判断是否为 CRLF */
                n = recv(sock, &c, 1, MSG_PEEK);
                /* DEBUG printf("%02X\n", c); */
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            buf[i] = c;
            i++;
        }
        else
            c = '\n';
    }
    buf[i] = '\0';

    return(i);
}

/**********************************************************************/
/* Return the informational HTTP headers about a file.
 * 返回关于文件的 HTTP 信息头。
 * Parameters: the socket to print the headers on
 *             要输出响应头的套接字
 *             the name of the file
 *             文件名 */
/**********************************************************************/
void headers(int client, const char *filename)
{
    char buf[1024];
    (void)filename;  /* could use filename to determine file type */
                     /* 可以使用文件名来确定文件类型 */

    /* 发送 HTTP 200 OK 响应头 */
    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Give a client a 404 not found status message.
 * 向客户端返回 404 未找到的状态信息。 */
/**********************************************************************/
void not_found(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
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
 * 向客户端发送一个普通文件。先发送响应头，如果出错则向客户端报告。
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             由套接字文件描述符产生的文件结构指针
 *             the name of the file to serve
 *             要提供的文件名 */
/**********************************************************************/
void serve_file(int client, const char *filename)
{
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    /* 初始化 buf，满足 while 循环进入条件 */
    buf[0] = 'A'; buf[1] = '\0';
    /* 读取并丢弃所有请求头 */
    while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
                                                  /* 读取并丢弃请求头 */
        numchars = get_line(client, buf, sizeof(buf));

    /* 打开请求的文件 */
    resource = fopen(filename, "r");
    if (resource == NULL)
        not_found(client);
    else
    {
        /* 发送响应头，然后将文件内容输出到客户端 */
        headers(client, filename);
        cat(client, resource);
    }
    fclose(resource);
}

/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * 本函数启动在指定端口监听 Web 连接的过程。如果端口为 0，
 * 则动态分配一个端口，并修改原始端口变量以反映实际端口。
 * Parameters: pointer to variable containing the port to connect on
 *             指向包含要连接端口的变量的指针
 * Returns: the socket
 *          返回创建的套接字 */
/**********************************************************************/
int startup(u_short *port)
{
    int httpd = 0;
    int on = 1;
    struct sockaddr_in name;

    /* 创建 TCP/IP 套接字 */
    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
        error_die("socket");
    /* 初始化服务器地址结构 */
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    /* 设置 SO_REUSEADDR 选项，允许端口复用，避免 "Address already in use" 错误 */
    if ((setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0)  
    {  
        error_die("setsockopt failed");
    }
    /* 将套接字绑定到指定端口 */
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
        error_die("bind");
    /* 如果端口为 0，动态分配端口并获取实际端口号 */
    if (*port == 0)  /* if dynamically allocating a port */
                     /* 如果动态分配端口 */
    {
        socklen_t namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
            error_die("getsockname");
        *port = ntohs(name.sin_port);
    }
    /* 开始监听，最大等待连接数为 5 */
    if (listen(httpd, 5) < 0)
        error_die("listen");
    return(httpd);
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * 通知客户端请求的 Web 方法尚未实现。
 * Parameter: the client socket
 * 参数：客户端套接字 */
/**********************************************************************/
void unimplemented(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
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

int main(void)
{
    int server_sock = -1;
    u_short port = 4000;
    int client_sock = -1;
    struct sockaddr_in client_name;
    socklen_t  client_name_len = sizeof(client_name);
    pthread_t newthread;

    /* 初始化服务器套接字，开始监听端口 */
    server_sock = startup(&port);
    printf("httpd running on port %d\n", port);

    /* 主循环：接受客户端连接 */
    while (1)
    {
        /* 接受一个新的客户端连接 */
        client_sock = accept(server_sock,
                (struct sockaddr *)&client_name,
                &client_name_len);
        if (client_sock == -1)
            error_die("accept");
        /* accept_request(&client_sock); */
        /* 直接调用 accept_request（单线程模式，已注释掉） */
        /* 为每个客户端连接创建一个新线程来处理请求 */
        if (pthread_create(&newthread , NULL, (void *)accept_request, (void *)(intptr_t)client_sock) != 0)
            perror("pthread_create");
    }

    close(server_sock);

    return(0);
}
