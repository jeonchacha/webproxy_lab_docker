#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

void doit(int fd);
void read_requesthdrs(rio_t *rp, char *host_header, char *other_header);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void parse_uri(char *uri, char *hostname, char *port, char *paht);
void forward_response(int servefd, int fd);
void reassemble(char *req, char *path, char *hostname, char *other_header);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  // 클라가 먼저 끊어도 죽지 않게(SIGPIPE 무시)
  Signal(SIGPIPE, SIG_IGN);

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);

    doit(connfd);
    Close(connfd);
  }
}

void doit(int fd)
{
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char host_header[MAXLINE], other_header[MAXLINE];
  char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
  char reqest_buf[MAXLINE];
  rio_t rio;

  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  if (strcasecmp(method, "GET") != 0) {
    clienterror(fd, method, "501", "Not implemented", "This Server does not implement this method");
    return;
  }
  read_requesthdrs(&rio, host_header, other_header);
  parse_uri(uri, hostname, port, path);
  int servefd = Open_clientfd(hostname, port);
  reassemble(reqest_buf, path, hostname, other_header);
  Rio_writen(servefd, reqest_buf, strlen(reqest_buf));
  forward_response(servefd, fd);
}

void reassemble(char *req, char *path, char *hostname, char *other_header)
{
  sprintf(req,
    "GET %s HTTP/1.0\r\n"
    "Host: %s\r\n"
    "%s"
    "Connection: close\r\n"
    "Proxy-Connection: close\r\n"
    "%s"
    "\r\n",
    path,
    hostname,
    user_agent_hdr,
    other_header
  );
}

void forward_response(int servefd, int fd)
{
  rio_t serve_rio;
  char response_buf[MAXLINE];

  Rio_readinitb(&serve_rio, servefd);
  ssize_t n;
  while ((n = Rio_readlineb(&serve_rio, response_buf, MAXLINE)) > 0) {
    Rio_writen(fd, response_buf, n);
  }
}

void read_requesthdrs(rio_t *rp, char *host_header, char *other_header)
{
  char buf[MAXLINE];
  host_header[0] = '\0';
  other_header[0] = '\0'; 

  while(Rio_readlineb(rp, buf, MAXLINE) > 0 && strcmp(buf, "\r\n")) {
    if (!strncasecmp(buf, "Host:", 5)) {
      strcpy(host_header, buf);
    }
    else if (!strncasecmp(buf, "User-Agent:", 11) || !strncasecmp(buf, "Connection:", 11) || !strncasecmp(buf, "Proxy-Connection:", 17)) {
      continue;  // 무시
    }
    else {
      strcat(other_header, buf);
    }
  }
}

void parse_uri(char *uri, char *hostname, char *port, char *path)
{
  char *hostbegin, *hostend, *portbegin, *pathbegin;
  char buf[MAXLINE];
 
  strcpy(buf, uri);
 
  hostbegin = strstr(buf, "//");
  hostbegin = (hostbegin != NULL) ? hostbegin + 2 : buf; 
 
  pathbegin = strchr(hostbegin, '/');
  if (pathbegin != NULL) {
    strcpy(path, pathbegin);
    *pathbegin = '\0';
  }
  else {
    strcpy(path, "/");
  }
 
  portbegin = strchr(hostbegin, ':');
  if (portbegin != NULL) {
    *portbegin = '\0';                
    strcpy(hostname, hostbegin);
    strcpy(port, portbegin + 1);      
  } 
  else {
    strcpy(hostname, hostbegin);
    strcpy(port, "80");       
   }
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXLINE];
  sprintf(body, "<html><title>Tiny Error</title></html>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n</body>", body);

  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body)); 
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}