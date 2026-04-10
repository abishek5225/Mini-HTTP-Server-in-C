#include <stdio.h>
#include <stdlib.h> //for exit()
#include <string.h> //for strlen()
#include <unistd.h> //for read(), write() and close()
#include <arpa/inet.h> //networking stuff ip port socket
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>

//configuration
#define PORT 8080
#define BACKLOG 10
#define RECV_BUFFER_SIZE 8192
#define MAX_FIELD_LEN 256
#define REQUESTS_FILE "requests.txt"
#define REQUEST_ID_FILE "request_id.txt"

//ansi colors
#define CLR_RED     "\033[0;31m"
#define CLR_GREEN   "\033[0;32m"
#define CLR_YELLOW  "\033[0;33m"
#define CLR_CYAN    "\033[0;36m"
#define CLR_BOLD    "\033[1m"
#define CLR_RESET   "\033[0m"

//shared css for all pages via macro

#define SHARED_CSS \
"<style>" \
":root{--dark:#1a1a2e;--mid:#16213e;--card:#0f3460;" \
"      --accent:#e94560;--text:#eaeaea;--muted:#8899aa;--green:#27ae60;}" \
"*{box-sizing:border-box;margin:0;padding:0}" \
"body{font-family:'Courier New',monospace;background:var(--dark);" \
"     color:var(--text);min-height:100vh;}" \
"header{background:var(--mid);border-bottom:3px solid var(--accent);" \
"       padding:18px 40px;display:flex;align-items:center;gap:16px;" \
"       position:sticky;top:0;z-index:99;}" \
"header .logo{font-size:1.6rem;font-weight:700;color:var(--accent);" \
"             letter-spacing:2px;text-transform:uppercase;}" \
"header .sub{color:var(--muted);font-size:.8rem;letter-spacing:1px;}" \
"nav{margin-left:auto;display:flex;gap:12px;}" \
"nav a{color:var(--text);text-decoration:none;padding:6px 14px;" \
"      border:1px solid var(--muted);border-radius:4px;font-size:.8rem;" \
"      transition:all .2s;}" \
"nav a:hover,nav a.active{border-color:var(--accent);color:var(--accent);}" \
"main{max-width:900px;margin:40px auto;padding:0 24px;}" \
"h1{font-size:1.5rem;color:var(--accent);margin-bottom:6px;" \
"   text-transform:uppercase;letter-spacing:2px;}" \
".sub2{color:var(--muted);margin-bottom:28px;font-size:.85rem;}" \
".card{background:var(--mid);border:1px solid var(--card);" \
"      border-radius:8px;padding:28px;margin-bottom:24px;}" \
"label{display:block;color:var(--muted);font-size:.78rem;" \
"      text-transform:uppercase;letter-spacing:1px;margin-bottom:6px;}" \
"input,textarea{width:100%%;background:#0a0a1a;" \
"  color:var(--text);border:1px solid var(--card);" \
"  border-radius:4px;padding:10px 14px;font-family:inherit;" \
"  font-size:.9rem;margin-bottom:18px;transition:border .2s;}" \
"input:focus,textarea:focus{outline:none;border-color:var(--accent);}" \
"textarea{resize:vertical;min-height:100px;}" \
"button{background:var(--accent);color:#fff;border:none;" \
"       padding:12px 28px;border-radius:4px;cursor:pointer;" \
"       font-family:inherit;font-size:1rem;font-weight:700;" \
"       letter-spacing:1px;text-transform:uppercase;transition:opacity .2s;}" \
"button:hover{opacity:.85;}" \
"a.btn{display:inline-block;background:var(--accent);color:#fff;" \
"      text-decoration:none;padding:8px 18px;border-radius:4px;" \
"      font-size:.8rem;font-weight:700;letter-spacing:1px;}" \
"a.btn-ghost{display:inline-block;background:var(--card);color:var(--text);" \
"            text-decoration:none;padding:8px 18px;border-radius:4px;" \
"            font-size:.8rem;font-weight:700;letter-spacing:1px;margin-left:10px;}" \
".badge{display:inline-block;padding:3px 10px;border-radius:12px;" \
"       font-size:.72rem;font-weight:700;letter-spacing:1px;}" \
".badge-red{background:rgba(192,57,43,.2);color:#e74c3c;border:1px solid #c0392b;}" \
".req-card{border-left:3px solid var(--accent);padding:16px 20px;" \
"          background:#0a0a1a;border-radius:0 6px 6px 0;margin-bottom:16px;}" \
".req-meta{font-size:.75rem;color:var(--muted);margin-bottom:8px;}" \
".req-name{font-size:1rem;font-weight:700;color:var(--accent);}" \
".req-msg{margin:8px 0;font-size:.9rem;line-height:1.5;}" \
".req-loc{font-size:.8rem;color:var(--muted);}" \
".req-loc span{color:var(--text);}" \
".empty{text-align:center;padding:60px 0;color:var(--muted);font-size:1.1rem;}" \
"footer{text-align:center;padding:24px;color:var(--muted);font-size:.75rem;" \
"       border-top:1px solid var(--mid);margin-top:48px;}" \
"@keyframes pulse{0%%,100%%{opacity:1}50%%{opacity:.4}}" \
".live-dot{display:inline-block;width:8px;height:8px;background:var(--green);" \
"          border-radius:50%%;margin-right:6px;animation:pulse 1.6s infinite;}" \
".alert-box{background:rgba(233,69,96,.1);border:1px solid var(--accent);" \
"           border-radius:6px;padding:14px 18px;margin-bottom:20px;" \
"           color:var(--accent);font-size:.85rem;}" \
".toolbar{display:flex;align-items:center;justify-content:space-between;" \
"         margin-bottom:20px;}" \
".toolbar-info{color:var(--muted);font-size:.82rem;}" \
"</style>"


void send_home(int client_socket){
    char *response =
   "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n\r\n"
        "<html>"
        "<head><title>Submit Help Request</title></head>"
        "<body>"
        "<h1> Emergency Help System</h1>"
        "<form method='POST' action='/help'>"
        "Name: <input type='text' name='name'><br><br>"
        "Message: <input type='text' name='message'><br><br>"
        "Location: <input type='text' name='location'><br><br>"
        "<button type='submit'>Send Help Request</button>"
        "</form>"
        "<br><a href='/requests'>View Requests</a>"
        "</body></html>";
    send(client_socket, response, strlen(response), 0);
}

int main(){
        int server_socket, client_socket;
        struct sockaddr_in address; //stores ip + port info
        int addrlen = sizeof(address); //size of address
        char buffer[30000] = {0}; //buffer to store data from client like http request

        //create socket
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket == 0){
            perror("socket failed");
            exit(EXIT_FAILURE);
        }

        address.sin_family = AF_INET; //ipv4
        address.sin_addr.s_addr = INADDR_ANY; //bind to all ip
        address.sin_port = htons(PORT); //convert port to network byte order

        bind(server_socket, (struct sockaddr *)&address, sizeof(address)); //attach socket to ip + port

        listen(server_socket, 100); //listen for incoming connections, max 5 in queue
        printf(" Server running on port %d...\n", PORT);

        while(1){
            //accept client connection
            client_socket = accept(server_socket, (struct sockaddr *)&address, (socklen_t*)&addrlen);

            if (client_socket < 0){
                perror("accept failed");
                continue;
            }

            memset(buffer, 0, sizeof(buffer));//clear buffer everytime before reading new data

            //read data from client
            read(client_socket, buffer, 30000);
            printf("Received request:\n%s\n", buffer);

           if(strncmp(buffer, "GET / ", 6) == 0 ){
                send_home(client_socket);
           }else if (strncmp(buffer, "GET /favicon.ico", 16) == 0){
                //ignore favicon requests
           }else{
            char *not_found = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\n<html><body><h1>404 Not Found</h1></body></html>";
                send(client_socket, not_found, strlen(not_found), 0);
           
           }
            close(client_socket);
        }
        return 0;
}