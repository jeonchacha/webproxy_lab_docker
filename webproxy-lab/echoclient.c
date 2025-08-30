#include "csapp.h"

int main(int argc, char **argv) /* argc는 인자 개수, argv는 인자 문자열 배열 */
{
    int clientfd; /* 서버와 연결된 소켓 디스크립터 */
    char *host, *port, buf[MAXLINE]; /* 고정 크기 바이트 배열. 한 줄 읽기/쓰기용 유저 공간 버퍼 */
    rio_t rio; /* rio_fd(대상 FD), rio_cnt(내부버퍼에 남은 읽기 바이트 수), rio_bufptr(내부버퍼 내 현재 위치), 내부 고정 버퍼 */

    if (argc != 3) { /* 인자 개수 확인. <host><port> 두 개가 정확히 필요 */
        fprintf(stderr, "usage: %s <host> <port>\n", argv[0]); /* 표준에러 스트림으로 출력 */
        exit(0); /* 정상 종료 코드를 리턴하며 프로세스 종료 */    
    }
    host = argv[1];
    port = argv[2];

    /* 
    1. getaddrinfo(host, port, ...)로 목적지 주소(IPv4) 해석
    2. socket()으로 TCP 소켓 생성 -> FD 할당
    3. connect()로 서버의 <host:port>에 3-way handshake 수행
    - 커널: SYN 송신 -> SYN-ACK 수신 -> ACK 송신
    - 성공 시 이 FD는 연결된 상태가 되고 이후 read/write 가능
    */
    clientfd = Open_clientfd(host, port);
    
    /*
    robust IO 컨텍스트 초기화:
    - rio.rio_fd = clientfd;
    - rio.rio_cnt = 0; (내부 버퍼에 아직 데이터 없음)
    - rio.rio_bufptr = - rio.rio_buf; (내부 버퍼 시작)
    이후 Rio_readlineb 호출 시
    - 내부 버퍼가 비어 있으면 read(rio_fd, rio.rio_buf, RIO_BUFSIZE)로 커널에서 대량 읽기
    -> 사용자 내부버퍼에 채움 -> 그 안에서 줄 단위로 잘라서 반환.
    */
    Rio_readinitb(&rio, clientfd);

    while (Fgets(buf, MAXLINE, stdin) != NULL) { /* 표준입력(stdin)에서 최대 MAXLINE-1 바이트를 읽어 buf에 저장, 사용자가 Ctrl+D(EOF) 입력하면 NULL이 되어 루프 종료. */
        /* robust write:
        write(...)가 부분쓰기(예: 300바이트 중 120바이트만 전송)할 수 있음.
        Rio_writen은 내부 루프로 남은 바이트를 모두 전송할 때까지 재호출 -> n 바이트 보장.
        전송단위: strlen(buf) -> Fgets가 넣은 문자열 길이
        */
        Rio_writen(clientfd, buf, strlen(buf)); /* 보내기 */
        
        /*
        rio의 내부 버퍼에서 buf로 복사
        내부버퍼가 비면 커널에서 추가로 읽어 채움.
        블로킹: 서버가 응답을 줄 때까지 대기.
        */
        Rio_readlineb(&rio, buf, MAXLINE); /* 받기 */
        Fputs(buf, stdout); /* 출력 */
    }

    /* 소켓 디스크립터 닫기:
    - 유저 공간: 이 FD 번호가 프로세스의 열린 파일 테이블에서 제거
    - 커널: 이 소켓을 참조하는 FD가 더 이상 없으면 TCP 종료 시퀀스(FIN -> ACK -> ...)를 진행해 상대에 "더 이상 보낼 데이터 없음"을 알림. 
    - 상대가 아직 열려 있으면 반닫힘(half-close) 상태가 될 수 있고, 상대도 닫으면 완전 종료.
    */
    Close(clientfd);
    
    exit(0);
}

/* echo client 흐름
1. 인자 검사(호스트, 포트)
2. 서버에 TCP 연결(open_clientfd)
3. robust IO 상태(Rio_readinitb) 초기화
4. 표준입력에서 한 줄을 읽어 서버로 송신
5. 서버가 돌려준 한 줄을 수신하여 표준출력에 그대로 출력
6. EOF까지 반복
7. 소켓 닫기
*/