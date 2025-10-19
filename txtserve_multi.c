// txtserve_multi.c — serve files by name from a directory root
// Protocol:
//   Client: "GET <name>\n"  -> Server: "SIZE <n>\n\n" + <n bytes>
//   Client: "HEAD <name>\n" -> Server: "SIZE <n>\n\n"
// Notes: <name> must be a simple filename (no '/' or "..").

#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int signum){(void)signum; g_stop = 1;}

static int send_all(int fd, const void *buf, size_t len){
    const char *p = (const char*)buf;
    while(len){
        ssize_t n = send(fd, p, len, 0);
        if(n < 0){ if(errno==EINTR) continue; return -1; }
        p += n; len -= (size_t)n;
    }
    return 0;
}

static ssize_t recv_line(int fd, char *buf, size_t maxlen){
    size_t i=0;
    while(i+1<maxlen){
        char c; ssize_t n = recv(fd,&c,1,0);
        if(n==0) break;
        if(n<0){ if(errno==EINTR) continue; return -1; }
        buf[i++]=c; if(c=='\n') break;
    }
    buf[i]='\0'; return (ssize_t)i;
}

static bool valid_name(const char *s){
    if(*s=='\0') return false;
    for(const char *p=s; *p; ++p){
        if(*p=='/' || *p=='\\') return false;
    }
    if(strstr(s,"..")) return false;
    return true;
}

static int serve_once(int cfd, const char *rootdir){
    char line[512];
    ssize_t rn = recv_line(cfd, line, sizeof(line));
    if(rn <= 0) return -1;

    // Strip CRLF
    for(ssize_t i=0;i<rn;i++){ if(line[i]=='\r'||line[i]=='\n'){ line[i]='\0'; break; } }

    bool want_body=false;
    const char *name=NULL;

    if(strncmp(line,"GET ",4)==0){ want_body=true; name=line+4; }
    else if(strncmp(line,"HEAD ",5)==0){ want_body=false; name=line+5; }
    else { send_all(cfd,"ERR unknown command\n",20); return 0; }

    if(!name || !valid_name(name)){ send_all(cfd,"ERR bad name\n",13); return 0; }

    // Build full path: rootdir + "/" + name
    char path[1024];
    int pn = snprintf(path,sizeof(path),"%s/%s",rootdir,name);
    if(pn<0 || (size_t)pn>=sizeof(path)){ send_all(cfd,"ERR name too long\n",19); return 0; }

    int fd = open(path,O_RDONLY);
    if(fd<0){
        char e[256];
        int n=snprintf(e,sizeof(e),"ERR open (%s)\n",strerror(errno));
        send_all(cfd,e,(size_t)n);
        return 0;
    }

    struct stat st;
    if(fstat(fd,&st)<0 || !S_ISREG(st.st_mode)){ close(fd); send_all(cfd,"ERR not file\n",13); return 0; }

    off_t size = st.st_size;
    char hdr[64];
    int hn = snprintf(hdr,sizeof(hdr),"SIZE %lld\n\n",(long long)size);
    if(send_all(cfd,hdr,(size_t)hn)<0){ close(fd); return -1; }

    if(want_body && size>0){
        char buf[8192];
        off_t left=size;
        while(left>0){
            ssize_t r = read(fd,buf,(left>(off_t)sizeof(buf))?sizeof(buf):(size_t)left);
            if(r<0){ if(errno==EINTR) continue; break; }
            if(r==0) break;
            if(send_all(cfd,buf,(size_t)r)<0){ close(fd); return -1; }
            left-=r;
        }
    }
    close(fd);
    return 0;
}

int main(int argc, char **argv){
    if(argc!=3){
        fprintf(stderr,"Usage: %s <port> <root-directory>\n",argv[0]);
        return 1;
    }
    const char *port=argv[1], *root=argv[2];

    signal(SIGINT,on_sigint);
    signal(SIGTERM,on_sigint);

    struct addrinfo hints, *res=NULL;
    memset(&hints,0,sizeof(hints));
    hints.ai_family = AF_INET;          // IPv4; change to AF_INET6 if you want IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int rc = getaddrinfo(NULL,port,&hints,&res);
    if(rc!=0){ fprintf(stderr,"getaddrinfo: %s\n",gai_strerror(rc)); return 1; }

    int sfd = socket(res->ai_family,res->ai_socktype,res->ai_protocol);
    if(sfd<0){ perror("socket"); freeaddrinfo(res); return 1; }

    int yes=1;
    setsockopt(sfd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));

    if(bind(sfd,res->ai_addr,(socklen_t)res->ai_addrlen)<0){ perror("bind"); close(sfd); freeaddrinfo(res); return 1; }
    freeaddrinfo(res);

    if(listen(sfd,16)<0){ perror("listen"); close(sfd); return 1; }
    fprintf(stderr,"Serving files from %s on port %s\n",root,port);

    while(!g_stop){
        struct sockaddr_storage ss; socklen_t slen=sizeof(ss);
        int cfd = accept(sfd,(struct sockaddr*)&ss,&slen);
        if(cfd<0){ if(errno==EINTR && g_stop) break; if(errno==EINTR) continue; perror("accept"); break; }
        serve_once(cfd,root);
        close(cfd);
    }
    close(sfd);
    return 0;
}
