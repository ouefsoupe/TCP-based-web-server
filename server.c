/* 
 * echoservert.c - A concurrent echo server using threads
 */

#include "nethelp.h"

void handle_connection(int connfd);
void *thread(void *vargp);
static void send_simple(int fd, const char *version, int code, const char *reason, const char *body);
const char *get_file_type(const char *path);

int main(int argc, char **argv)
{
    int listenfd, *connfdp, port;
    socklen_t clientlen = sizeof(struct sockaddr_in);
    struct sockaddr_in clientaddr;
    pthread_t tid; 

    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(0);
    }
    port = atoi(argv[1]);

    listenfd = open_listenfd(port);
    while (1) {
	connfdp = malloc(sizeof(int));
	*connfdp = accept(listenfd, (struct sockaddr*)&clientaddr, &clientlen);
	pthread_create(&tid, NULL, thread, connfdp);
    }
}

/* thread routine */
void * thread(void * vargp) 
{  
    int connfd = *((int *)vargp);
    pthread_detach(pthread_self()); 
    free(vargp);

    handle_connection(connfd);

    // echo(connfd);
    close(connfd);
    return NULL;
}

/*
 * handle_connection - read http request
 */

void handle_connection(int connfd)
{
    char buf[8192];
    int total = 0;

    while (1) {
        int n = recv(connfd, buf + total, (int)sizeof(buf) - 1 - total, 0);
        if (n < 0) {
            perror("recv");
            return;
        }
        if (n == 0) {
            // client closed connection
            return;
        }

        total += n;
        buf[total] = '\0';

        if (strstr(buf, "\r\n\r\n") != NULL) {
            break;
        }

        // Prevent oversized headers from blowing up
        if (total >= (int)sizeof(buf) - 1) {
            send_simple(connfd, "HTTP/1.1", 400, "Bad Request", "The request could not be parsed or is malformed\n");
            break;
        }
    }
    
    printf("=== RAW REQUEST ===\n%.*s\n===================\n", total, buf);

    char *line_end = strstr(buf, "\r\n");

    if (!line_end) {
        // malformed request doesnt have \r\n
        send_simple(connfd, "HTTP/1.1", 400, "Bad Request", "The request could not be parsed or is malformed\n");
        return;
    }
    *line_end = '\0';

    char method[16], uri[2048], version[16];
    if (sscanf(buf, "%15s %2047s %15s", method, uri, version) != 3) {
        // malformed
        send_simple(connfd, version, 400, "Bad Request", "The request could not be parsed or is malformed\n");
        return;
    }

    // error handingling
    // must be GET request
    if (strcmp(method, "GET") != 0) {
        send_simple(connfd, version, 405, "Method Not Allowed", "Only GET supported\n");
        return;
    }

    // Must be 1.0 or 1.1
    if (strcmp(version, "HTTP/1.0") != 0 && strcmp(version, "HTTP/1.1") != 0) {
        send_simple(connfd, "HTTP/1.1", 505, "HTTP Version Not Supported", "Use HTTP/1.0 or HTTP/1.1\n");
        return;
    }

    printf("METHOD=%s URI=%s VERSION=%s\n", method, uri, version);
    
    // checks for valid URI

    if (uri[0] != '/'){
        send_simple(connfd, "HTTP/1.1", 400, "Bad Request", "The request could not be parsed or is malformed\n");
        return;
    }
    if (strstr(uri, "..") != NULL) {
        send_simple(connfd, version, 403, "Forbidden", "404 The requested file can not be accessed due to a file permission issue\n");
        return;
    }
    
    struct stat st;

    char path[2064];
    
    snprintf(path, sizeof(path), "./www%s", uri);

    if (stat(path, &st) < 0) {
    // file does not exist
        send_simple(connfd, version, 404, "Not Found", "404 The requested file can not be found in the document tree\n");
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        char try1[2064], try2[2064];

        // Adds trailign '/' if it doesnt exist
        // allows for append to work
        size_t len = strlen(path);
        if (len > 0 && path[len - 1] != '/') {
            // Only add slash if there's space
            if (len + 1 >= sizeof(path)) {
                send_simple(connfd, version, 404, "Not Found", "404 The requested file can not be found in the document tree\n");
                return;
            }
            strcat(path, "/");
        }

        snprintf(try1, sizeof(try1), "%sindex.html", path);
        snprintf(try2, sizeof(try2), "%sindex.htm", path);

        if (stat(try1, &st) == 0) {
            strncpy(path, try1, sizeof(path));
            path[sizeof(path) - 1] = '\0';
        } else if (stat(try2, &st) == 0) {
            strncpy(path, try2, sizeof(path));
            path[sizeof(path) - 1] = '\0';
        } else {
            send_simple(connfd, version, 404, "Not Found", "404 The requested file can not be found in the document tree\n");
            return;
        }
    }

    // using the stat metadate for file size
    off_t filesize = st.st_size;

    const char *file_type = get_file_type(path);

    // open file
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (errno == EACCES) {
            send_simple(connfd, version, 403, "Forbidden", "404 The requested file can not be accessed due to a file permission issue\n");
        } else {
            send_simple(connfd, version, 404, "Not Found", "404 The requested file can not be found in the document tree\n");
        }
        return;
    }

    char header[1024];
    int header_len = snprintf(header, sizeof(header),
        "%s 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: Close\r\n"
        "\r\n",
        version,
        file_type,
        (long)filesize);

    send(connfd, header, header_len, 0);

    char filebuf[8000];
    ssize_t n;

    while ((n = read(fd, filebuf, sizeof(filebuf))) > 0) {
        ssize_t sent = 0;
        while (sent < n) {
            ssize_t s = send(connfd, filebuf + sent, n - sent, 0);
            if (s <= 0) break;
            sent += s;
        }
    }

    close(fd);
}

// helper for sending error codes
static void send_simple(int fd, const char *version, int code, const char *reason, const char *body)
{
    if (!version) version = "HTTP/1.1";
    if (!body) body = "";
    int body_len = (int)strlen(body);

    char header[512];
    int header_len = snprintf(header, sizeof(header),
        "%s %d %s\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %d\r\n"
        "Connection: Close\r\n"
        "\r\n",
        version, code, reason, body_len);

    send(fd, header, header_len, 0);
    if (body_len) send(fd, body, body_len, 0);
}

// helper to return file type
const char *get_file_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) {
        return "application/octet-stream";
    };

    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".htm")  == 0) return "text/html";
    if (strcmp(ext, ".txt")  == 0) return "text/plain";
    if (strcmp(ext, ".png")  == 0) return "image/png";
    if (strcmp(ext, ".gif")  == 0) return "image/gif";
    if (strcmp(ext, ".jpg")  == 0) return "image/jpg";
    if (strcmp(ext, ".ico")  == 0) return "image/x-icon";
    if (strcmp(ext, ".css")  == 0) return "text/css";
    if (strcmp(ext, ".js")   == 0) return "application/javascript";


    return "application/octet-stream";
}