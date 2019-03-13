#include <stdio.h>
#include <winsock2.h>

const unsigned short HTTP_DEF_PORT     = 8000;
const unsigned short HTTP_BUF_SIZE     = 2048;
const unsigned short HTTP_FILENAME_LEN = 256;
const unsigned short HTTP_DIR_LEN      = 256;

const unsigned short MAX_THREAD_NUM    = 256;

struct doc_type{
    char *suffix;
    char *type;
};

struct doc_type file_type[] = {
    {"html",   "text/html"},
    {"gif",    "image/gif"},
    {"jpeg",  "image/jpeg"},
    {"jpg",   "image/jpeg"},
    {"ico", "image/x-icon"},
    { NULL,   NULL}
};

char *http_response_hdr_tmpl_OK = "HTTP/1.1 200 OK\r\n"
    "Accept-Ranges: bytes\r\nContent-Length: %d\r\nConnection: close\r\n"
    "Content-Type: %s\r\n\r\n";

char *http_response_hdr_tmpl_BAD_REQUEST = "HTTP/1.1 400 Bad Request\r\n"
    "Accept-Ranges: bytes\r\nContent-Length: %d\r\nConnection: close\r\n"
    "Content-Type: %s\r\n\r\n";

char main_dir[HTTP_DIR_LEN] = "./";

struct ARG{
    SOCKET socket;
    char recv_buf[HTTP_BUF_SIZE];
};

ARG args[MAX_THREAD_NUM];

char *http_get_type_by_suffix(const char *suffix){
    doc_type *type;

    for (type = file_type; type->suffix; type++){
        if (strcmp(type->suffix, suffix) == 0) return type->type;
    }

    return NULL;
}

void http_parse_request_cmd(char *buf, char *file_name, char *suffix){
    char *url_begin = buf, *url_end = strchr(url_begin, ' ');
    *url_end = '\0';

    char *bias = strrchr(url_begin, '/');
    int length = url_end - bias;

    if((*bias == '/') || (*bias == '\\')){
        bias++;
        length--;
    }

    if (length > 0){
        memcpy(file_name, bias, length);
        file_name[length] = 0;

        char* sfx_begin = strrchr(file_name, '.');
        if (sfx_begin) strcpy(suffix, sfx_begin + 1);
    }
}

void http_sned_get_response(SOCKET soc, char *buf){
    char file_name[HTTP_FILENAME_LEN] = "index.html", suffix[16] = "html", full_file_name[HTTP_DIR_LEN + HTTP_FILENAME_LEN];

    http_parse_request_cmd(buf, file_name, suffix);

    FILE *res_file;

    sprintf(full_file_name, "%s%s", main_dir, file_name);
    res_file = fopen(full_file_name, "rb+");
    if (res_file == NULL){
        printf("[HTTP] The dir [%s] or file [%s] is not existed\n", main_dir, file_name);
        return;
    }

    fseek(res_file, 0, SEEK_END);
    int file_len = ftell(res_file);
    fseek(res_file, 0, SEEK_SET);

    char *type;
    type = http_get_type_by_suffix(suffix);
    if (type == NULL){
        printf("[HTTP] There is not the related content type\n");
        return;
    }

    char http_header[HTTP_BUF_SIZE];
    int hdr_len = sprintf(http_header, http_response_hdr_tmpl_OK, file_len, type);

    if(send(soc, http_header, hdr_len, 0) == SOCKET_ERROR){
        fclose(res_file);
        printf("[GET] Failed to response, socket error = %d\n", WSAGetLastError());
        return;
    }

    char read_buf[HTTP_BUF_SIZE];
    int read_len;
    do{
        read_len = fread(read_buf, sizeof(char), HTTP_BUF_SIZE, res_file);
        if (read_len > 0){
            send(soc, read_buf, read_len, 0);
            file_len -= read_len;
        }
    }while((read_len > 0) && (file_len > 0));

    fclose(res_file);
}

void http_sned_post_response(SOCKET soc, char *buf){
    char *request_begin = strchr(buf, '\n');
    while(*(request_begin + 1) != '\r') request_begin = strchr(request_begin + 2, '\n');
    request_begin += 3;

    char *request_end = strchr(request_begin, '\r');
    if(request_end == NULL){
        char *p;
        for(p = buf; *p != '\0'; p++);
        request_end = p;
    }

    *request_end = '\0';

    char *item_begin = request_begin, *item_end = request_begin;

    printf("[POST] Received POST form:\n");

    bool decode_success = true;
    char send_buf[HTTP_BUF_SIZE];
    send_buf[0] = '\0';
    char item_buff[HTTP_BUF_SIZE];

    while(item_end != request_end){
        item_end = strchr(item_begin, '&');
        if(item_end == NULL) item_end = request_end;

        *item_end = '\0';
        char *key = item_begin;
        char *divide = strchr(item_begin, '=');
        if(divide == NULL){
            decode_success = false;
            break;
        }
        *divide = '\0';
        char *value = divide + 1;

        sprintf(item_buff, "%s: %s\r\n", key, value);
        strcat(send_buf, item_buff);
        printf("%s", item_buff);

        item_begin = item_end + 1;
    }

    if(decode_success){
        printf("[POST] Decode POST requests SUCCESSED.\n");

        char http_header[HTTP_BUF_SIZE];
        int send_len = strlen(send_buf);
        int hdr_len = sprintf(http_header, http_response_hdr_tmpl_OK, send_len, http_get_type_by_suffix("html"));

        if(send(soc, http_header, hdr_len, 0) == SOCKET_ERROR){
            printf("[POST] Failed to response, socket error = %d\n", WSAGetLastError());
            return;
        }

        if(send(soc, send_buf, send_len, 0) == SOCKET_ERROR){
            printf("[POST] Failed to response, socket error = %d\n", WSAGetLastError());
            return;
        }

    }else{
        printf("[POST] Decode POST requests FAILED.\n");

        char http_header[HTTP_BUF_SIZE];
        int hdr_len = sprintf(http_header, http_response_hdr_tmpl_BAD_REQUEST, 0, http_get_type_by_suffix("html"));

        if(send(soc, http_header, hdr_len, 0) == SOCKET_ERROR){
            printf("[POST] Failed to response, socket error = %d\n", WSAGetLastError());
            return;
        }
    }
}

DWORD WINAPI http_send_response(LPVOID pM){
    ARG *arg = (ARG*) pM;

    SOCKET soc = arg->socket;

    char buf[HTTP_BUF_SIZE];
    memcpy(buf, arg->recv_buf, sizeof(arg->recv_buf));

    char *method_begin = buf, *method_end = strchr(buf, ' ');
    *method_end = '\0';

    if(strcmp(method_begin, "GET") == 0){
        printf("[HTTP] Received a GET request.\n");
        http_sned_get_response(soc, method_end + 1);
    }else if(strcmp(method_begin, "POST") == 0){
        printf("[HTTP] Received a POST request.\n");
        http_sned_post_response(soc, method_end + 1);
    }else{
        printf("[HTTP] Received a else request.\n");
        printf("%s %s\n", buf, method_end + 1);
    }
    printf("[SOCKET] Sending response finished, closing socket.");
    closesocket(soc);
    return 0;
}


int main(int argc, char **argv)
{
    unsigned short port = HTTP_DEF_PORT;

    if(argc >= 2) port = atoi(argv[1]);
    if(argc == 3) sprintf(main_dir, "%s", argv[2]);

    int dir_len = strlen(main_dir);
    if(main_dir[dir_len - 1] != '\\' || main_dir[dir_len - 1] != '/') main_dir[dir_len] = '\\';

    WSADATA wsaData;
    WSAStartup(MAKEWORD(2,0), &wsaData);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock == INVALID_SOCKET){
        printf("[SOCK] socket() Fails, error = %d\n", WSAGetLastError());
        return -1;
    }

    sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(sock, (sockaddr *) &serv_addr, sizeof(serv_addr)) == SOCKET_ERROR){
        closesocket(sock);
        printf("[SOCK] Failed to bind, error = %d\n", WSAGetLastError());
        return 0;
    }

    listen(sock, SOMAXCONN);
    printf("[SOCK] The server is running ......\n");

    int cur_connection = 0;

    while(true){
        sockaddr_in from_addr;
        int from_len = sizeof(from_addr);
        SOCKET acpt_soc = accept(sock, (sockaddr *) &from_addr, &from_len);
        if(acpt_soc == INVALID_SOCKET){
            closesocket(acpt_soc);
            printf("[SOCK] Failed to accept, error = %d\n", WSAGetLastError());
            break;
        }

        printf("[SOCK] Accepted address:[%s], port:[%d]\n",
            inet_ntoa(from_addr.sin_addr), ntohs(from_addr.sin_port));

        char recv_buf[HTTP_BUF_SIZE];
        int recv_len = recv(acpt_soc, recv_buf, HTTP_BUF_SIZE, 0);
        if(recv_len == SOCKET_ERROR){
            closesocket(acpt_soc);
            printf("[SOCK] Failed to recv, error = %d\n", WSAGetLastError());
            break;
        }

        recv_buf[recv_len] = '\0';

        args[cur_connection].socket = acpt_soc;
        memcpy(args[cur_connection].recv_buf, recv_buf, sizeof(recv_buf));

        CreateThread(NULL, 0, http_send_response, (LPVOID) &args[cur_connection], 0, NULL);

        cur_connection++;
        if(cur_connection == MAX_THREAD_NUM) cur_connection = 0;
    }

    closesocket(sock);
    WSACleanup();
    printf("[SOCK] The server is stopped.\n");

    return 0;
}
