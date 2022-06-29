#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#include "wrap.h"
#include "pub.h"

#define PORT 8899

void send_header(int cfd, int code, char *info, char* filetype, int length)
{
    // send stat
    char buf[1024] = "";
    int len = 0;
    len = sprintf(buf, "HTTP/1.1 %d %s\r\n", code, info);
    send(cfd, buf, len, 0);
    // send message
    len = sprintf(buf, "Content-Type:%s\r\n", filetype);
    send(cfd, buf, len, 0);
    if(length > 0)
    {
        len = sprintf(buf, "Content-Length:%d\r\n", length);
        send(cfd, buf, len, 0);
    }
    // sen \n
    send(cfd, "\r\n", 2, 0);
}

void send_file(int cfd, char* path, struct epoll_event *ev, int epfd)
{
    int fd = open(path, O_RDONLY);
    if(fd < 0)
    {
        perror("");
        return;
    }

    char buf[1024] = "";
    int len = 0;
    while(1)
    {
        len = read(fd, buf, sizeof(buf));
        if(len < 0)
        {
            perror("");
            break;
        }
        else if(0 == len)
        {
            break;
        }
        else
        {
            send(cfd, buf, len, 0);
        }
    }
    close(fd);

    close(cfd);
    epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, ev);
}

void read_client_request(int epfd, struct epoll_event *ev)
{
    // read request(read one)
    char buf[1024] = "";
    char tmp[1024] = "";
    int n = Readline(ev->data.fd, buf, sizeof(buf));
    if(n <= 0)
    {
        printf("close or err\n");
        epoll_ctl(epfd, EPOLL_CTL_DEL, ev->data.fd, ev);
        close(ev->data.fd);

        return;
    }
    printf("%s\n", buf);

    int ret = 0;
    while((ret = Readline(ev->data.fd, tmp, sizeof(tmp))) > 0);
    
    // GET /a.txt HTTP/1.1\R\N
    char method[256] = "";
    char content[256] = "";
    char protocol[256] = "";
    sscanf(buf, "%[^ ] %[^ ] %[^ \r\n]", method, content, protocol);
    printf("[%s] [%s] [%s]\n", method, content, protocol);

    // if get handle
    if(0 == strcasecmp(method, "get"))
    {
        // get a.txt
        char* strfile = content + 1;
        strdecode(strfile, strfile);
        // if file
        if(0 == *strfile)
            // if no file 
            strfile = "./";

        struct stat s;
        if(stat(strfile, &s) < 0)
        {
            printf("file not found...\n");
            // send headline(stat message \n)
            send_header(ev->data.fd, 404, "NOT FOUND", get_mime_type("*.html"), 0);
            // send file
            send_file(ev->data.fd, "error.html", ev, epfd);   
        }
        else
        {
            // request file
            if(S_ISREG(s.st_mode))
            {
                printf("file\n");
                // send headline(stat message \n)
                send_header(ev->data.fd, 200, "OK", get_mime_type(strfile), s.st_size);
                // send file
                send_file(ev->data.fd, strfile, ev, epfd);
            }
            // request path
            else if(S_ISDIR(s.st_mode))
            {
                printf("dir\n");
            }
        }


    }
}


int main(void)
{
    signal(SIGPIPE, SIG_IGN);

    // cd file
    char pwd_path[256] = "";
    // get now file path
    char* path =  getenv("PWD");
    strcpy(pwd_path, path);
    strcat(pwd_path, "/web-http");
    printf("%s\n", pwd_path);
    chdir(pwd_path);

    // 1.create socket bind
    int lfd = tcp4bind(PORT, NULL);

    // 2.listen
    Listen(lfd, 128);

    // 3.create tree
    int epfd = epoll_create(1);

    // 4.add lfd in tree
    struct epoll_event ev, evs[1024];
    ev.data.fd = lfd;
    ev.events = EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);

    // 5.while listen
    while(1)
    {
        int nready = epoll_wait(epfd, evs, 1024, -1);
        if(nready < 0)
        {
            perror("");
            break;
        }
        else
        {
            for(int i = 0; i < nready; i++)
            {
                // lfd
                if(evs[i].data.fd == lfd && evs[i].events & EPOLLIN)
                {
                    struct sockaddr_in cliaddr;
                    char ip[16] = "";
                    socklen_t len = sizeof(cliaddr);
                    int cfd = Accept(lfd, (struct sockaddr *)&cliaddr, &len);
                    printf("new client ip = %s port = %d\n", inet_ntop(AF_INET, &cliaddr.sin_addr.s_addr, ip, 16),
                            ntohs(cliaddr.sin_port));
                    // set 
                    int flag = fcntl(cfd, F_GETFL);
                    flag |= O_NONBLOCK;
                    fcntl(cfd, F_SETFL, flag);

                    // add tree
                    ev.data.fd = cfd;
                    ev.events = EPOLLIN;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
                }
                // cfd
                else if(evs[i].events & EPOLLIN)
                {
                    read_client_request(epfd, &evs[i]);
                }
            }
        }
    }

    // 6.close
    close(lfd);

    return 0;
}
