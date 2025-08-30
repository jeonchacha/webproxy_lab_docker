#include "csapp.h"

static void echo(int connfd) /* 인자 connfd는 accept()가 반환한 연결된 TCP 소켓 FD. 이 FD로 읽고 쓰면 상대 클라이언트와 통신. */
{
    size_t n;
    char buf[MAXLINE]; /* 한 줄을 담을 사용자 공간 버퍼. */

    /*
    RIO 상태 구조체:
    - rio_fd: 대상 FD(여기선 connfd)
	- rio_cnt: 내부 버퍼에 남은 읽기 바이트 수
	- rio_bufptr: 내부 버퍼의 현재 읽기 위치
	- rio_buf[RIO_BUFSIZE]: 내부 순환 버퍼(8192B)
    */
    rio_t rio;

    Rio_readinitb(&rio, connfd); /* RIO 컨텍스트 초기화 */
    while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) { /* 한 줄씩 읽어들인다. 블로킹 동작: 서버는 클라이언트가 개행을 보낼 때까지 기다린다. */
        printf("server received %d bytes\n", (int)n);
        Rio_writen(connfd, buf, n); /* 클라이언트에게 그대로 에코. 이때 커널은 connfd의  송신 버퍼에 데이터를 적재하고, TCP가 네트워크로 밀어낸다. */
    }
}

int main(int argc, char **argv) /* argv[1]에 포트를 기대 */
{
    int listenfd, connfd; /* 리스닝 소켓 FD(연결 대기용), accept가 반환하는 연결된 소켓 FD(클라이언트 1명과 1:1 통신용). */
    socklen_t clientlen; /* accept가 채워 줄 주소 구조체의 길이 인자. */
    struct sockaddr_storage clientaddr; /* accept로 보내지는 소켓 주소 구조체, sockaddr_storage는 모든 형태의 소켓 주소를 저장하기에 충분히 크다. */
    char client_hostname[MAXLINE], client_port[MAXLINE]; /* Getnameinfo로 얻은 문자열 형태의 호스트/포트를 담을 버퍼. */

    if (argc != 2) { /* 포트 인자 없으면 종료 */
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }

    /*
        리스닝 소켓 생성 래퍼. 내부적으로:
        1. getaddrinfo(NULL, port, AI_PASSIVE, ...)
        2. socket() (AF_INET)
        3. setsockopt(SO_REUSEADDR)
        4. bind() (모든 인터페이스의 지정 포트)
        5. listen() (백로그 큐 생성)
    */
    listenfd = Open_listenfd(argv[1]);
    
    /* 
        iterative라서 한 번에 하나의 연결만 처리
        단일 스레드 -> 두 번째 클라이언트는 첫 번째 처리 끝날 때까지 대기.
    */
    while (1) { 
        /* accept 호출 전 입력 파라미터 길이 설정. 커널이 이 크기만큼 주소를 채워준다. */
        clientlen = sizeof(struct sockaddr_storage);

        /*
            블로킹: 새 연결이 백로그 큐에 없으면 여기서 대기.
            성공: 새로운 연결 소켓 FD(connfd)를 반환(리스닝 FD와는 별도).
            clientaddr엔 원격 클라이언트의 (IP, port)가 채워진다.
            (SA *) 캐스팅은 struct sockaddr *로 맞추기 위한 관용.
        */
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        /* 클라이언트 (host, port) 로깅 */
        Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        printf("Connected to (%s, %s)\n", client_hostname, client_port);

        /*
            한 클라이언트 세션 처리.
            내부에서 while(readline) write(line) 반복 -> EOF 시 리턴.
            이 동안 서버는 다른 클라이언트를 처리하지 못함
        */
        echo(connfd);

        /* 커널이 FIN을 보내고, 상대가 응답하면 TCP 종료 절차가 완료된다. */
        Close(connfd);
    }
    exit(0);
}

/* 에코 서버(iterative) 
* 1. 서버 소켓 생성·바인딩·리스닝
* 2. accept()로 클라이언트 연결 수락
* 3. 연결 소켓으로부터 라인 단위(또는 바이트 단위)로 읽음
* 4. 읽은 그대로 다시 write()
* 5. 연결 종료
*
* 메모리 관점:
* 각 클라이언트 처리 때 rio_t 구조체를 스택에 두고(지역변수), 내부 버퍼(RIO_BUFSIZE)로 커널에서 읽은 데이터를 사용자 공간 버퍼에 채움
* -> Rio_writen으로 fd(커널의 송신 버퍼)로 밀어넣음 -> 커널이 TCP로 전송.
*/