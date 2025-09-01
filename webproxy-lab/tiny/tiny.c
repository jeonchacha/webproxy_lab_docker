/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, int is_haed);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

/* main -> accep 루프 -> doit -> (read_requesthdrs, parse_uri) -> serve_static | serve_dynamic -> close */
int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* 포트 미지정시 종료 */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  /* 리스닝 소켓 생성. socket -> setsockopt(SO_REUSEADDR) -> bind -> listen */
  listenfd = Open_listenfd(argv[1]);

  /* 무한 accept 루프 */
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen); // TCP 3-way handshake 완료된 연결 소켓 획득. (커널이 listen 큐에서 꺼냄)
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0); // 클라이언트 호스트/서비스 문자열 획득

    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // HTTP 트랜잭션 1건 처리.
    Close(connfd); // 연결 종료(HTTP/1.0 단발).
  }
}

/* 한 개의 HTTP 트랜잭션을 처리한다. */
void doit(int fd)
{
  int is_static; // 정적, 동적 구분
  struct stat sbuf; // 파일 메타
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE]; // 요청 라인 파싱 버퍼
  char filename[MAXLINE], cgiargs[MAXLINE]; // 서비스 대상 경로, CGI 인자
  rio_t rio;
  int is_head;

  /* Read request line and headers */
  Rio_readinitb(&rio, fd);            // fd로 rio 버퍼 초기화
  Rio_readlineb(&rio, buf, MAXLINE);  // 요청 라인 한 줄
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version); // 메서드/URI/버전 추출 -> "GET /path HTTP/1.1"

  /* 메서드 체크(GET, HEAD 외 501) */
  if (!strcasecmp(method, "GET")) {
    is_head = 0;
  } 
  else if(!strcasecmp(method, "HEAD")) {
    is_head = 1;
  } 
  else {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }

  read_requesthdrs(&rio);
  
  /* Parse URI from GET request */
  is_static = parse_uri(uri, filename, cgiargs);

  /* stat(2)로 파일 상태 확인. 실패 시 404 */
  if (stat(filename, &sbuf) < 0) {
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }
  
  if (is_static) { /* Serve static content */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read this file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size, is_head);
  }
  else { /* Serve dynamic content */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }

    if (is_head) { /* 동적 HEAD는 미지원 */
      clienterror(fd, method, "501", "Not implemented", "HEAD for CGI is not supported");
      return;
    }

    serve_dynamic(fd, filename, cgiargs);
  }
}

/* 에러 메시지를 클라이언트에게 보낸다. */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

/* 요청 헤더를 읽고 무시한다. (Host, User-Agent, Content-Length 등) */
void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf, "\r\n")) { // HTTP 헤더의 끝은 빈줄(\r\n)
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

/* HTTP URI를 분석한다. */
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  if (!strstr(uri, "cgi-bin")) { /* Static content */
    /* 정적 경로 조립 */
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);

    if (uri[strlen(uri)-1] == '/') {
      strcat(filename, "home.html");
    }
    return 1;
  }
  else { /* Dynamic content */
    ptr = index(uri, '?');
    if (ptr) {
      strcpy(cgiargs, ptr+1);
      *ptr = '\0';
    }
    else {
      strcpy(cgiargs, "");
    }
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

/* 정적 컨텐츠를 클라이언트에게 서비스한다. */
void serve_static(int fd, char *filename, int filesize, int is_haed)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* MIME 추출 -> Send response headers to client */
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers:\n");
  printf("%s", buf);

  /* HEAD 요청이면 바디 전송 생략 */
  if (is_haed) return;

  /* Send response body to client */
  srcfd = Open(filename, O_RDONLY, 0);

  /* mmap으로 매핑 후 Rio_writen으로 본문 전송 */
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // 파일을 가상메모리에 매핑
  // Close(srcfd);
  // Rio_writen(fd, srcp, filesize); // 유저->커널 복사1회
  // Munmap(srcp, filesize);

  /* malloc + rio_readn + rio_writen */
  srcp = (char *)Malloc((size_t)filesize); // 힙 버퍼 확보
  ssize_t nread = Rio_readn(srcfd, srcp, (size_t)filesize); // 커널->유저 복사1회
  Close(srcfd);
  Rio_writen(fd, srcp, (size_t)nread); // 유저->커널 복사1회
  Free(srcp);

  /*
      mmap:   파일 내용을 명시적으로 읽지 않아도(read 호출 없음) "메모리에 매핑된 주소"를
              rio_writen이 참조하는 순간 페이지 폴트로 커널이 파일 페이지를 올려주고, 유저->커널 한 번 복사만으로 소켓으로 간다.
              
      malloc: malloc으로 만든 버퍼는 비어 있으니, 먼저 파일 FD에서 유저 버퍼로 읽기(rio_readn)를 해야 하고,
              이어서 유저->커널로 쓰기(rio_writen)를 해야 한다(복사가 한 번 더 생김). 
  */
}

/* Derive file type from filename */
void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html")) {
    strcpy(filetype, "text/html");
  }
  else if (strstr(filename, ".gif")) {
    strcpy(filetype, "image/gif");
  }
  else if (strstr(filename, ".png")) {
    strcpy(filetype, "image/png");
  }
  else if (strstr(filename, ".jpg")) {
    strcpy(filetype, "image/jpeg");
  } 
  else if (strstr(filename, ".mpg") || strstr(filename, ".mpeg")) {
    strcpy(filetype, "video/mpeg");
  }
  else {
    strcpy(filetype, "text/plain");
  }
}

/* 
동적컨텐츠를 클라이언트에 제공한다.
fork로 자식 생성 -> QUERY_STRING 환경변수 세팅 -> dup2(fd, STDOUT)로 표준출력을 소켓으로 리다이렉트 
-> execve로 CGI 실행 -> 부모가 wait
*/
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
  char buf[MAXLINE], *emptylist[] = { NULL };

  /* Return first part of HTTP response 
  응답 헤더만 먼저 전송, CGI가 생성할 바디는 표준출력 -> 소켓으로 나감. */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  /* for/exec + 환경변수 + 리다이렉트 */
  if (Fork() == 0) { /* Child */
    /* Real server would set all CGI vars here */
    setenv("QUERY_STRING", cgiargs, 1); // CGI 인자 전달
    Dup2(fd, STDOUT_FILENO); /* Redirect stdout to client, stdout -> 소켓 fd */
    Execve(filename, emptylist, environ); /* Run CGI program */
    // 자식에서 execve 호출 시 현제 프로세스 이미지가 완전히 대체됨(코드/데이터/스택 새로 로드).
  }
  Wait(NULL); /* Parent waits for and reaps child */
}