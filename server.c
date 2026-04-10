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

//cleans up child processes after they exit.
static void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

//url decode function to convert %20 to space and handle other url encoded characters
static void url_decode(const char *src, char *dst, size_t dst_len) {
    size_t i = 0, j = 0;
    while (src[i] && j + 1 < dst_len) {
        if (src[i] == '%' && src[i+1] && src[i+2]) {
            char hex[3] = { src[i+1], src[i+2], '\0' };
            dst[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else if (src[i] == '+') {
            dst[j++] = ' ';
            i++;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
}

//extract key = value pairs from url encoded form data 
static int extract_field(const char *body, const char *key,
                          char *out, size_t out_len) {
    char search[MAX_FIELD_LEN + 2];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(body, search);
    if (!p) return 0;
    p += strlen(search);
 
    char raw[RECV_BUFFER_SIZE] = {0};
    size_t k = 0;
    while (*p && *p != '&' && k + 1 < sizeof(raw))
        raw[k++] = *p++;
    raw[k] = '\0';
 
    url_decode(raw, out, out_len);
    return 1;
}

//strip characters
static void sanitize(char *s, size_t max_len) {
    size_t len = strnlen(s, max_len);
    for (size_t i = 0; i < len; i++) {
        if      (s[i] == '<')              s[i] = '(';
        else if (s[i] == '>')              s[i] = ')';
        else if (s[i] == '\n' || s[i] == '\r') s[i] = ' ';
        else if (s[i] == '|')              s[i] = '-';
    }
}

//read and increment persistent counter; returns next request id  (starts 1)
static int next_request_id(void) {
    int id = 1;
    FILE *f = fopen(REQUEST_ID_FILE, "r");
    if (f) { int r = fscanf(f, "%d", &id); (void)r; fclose(f); id++; }
    f = fopen(REQUEST_ID_FILE, "w");
    if (f) { fprintf(f, "%d", id); fclose(f); }
    return id;
}

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


#define HTML_HEADER_NAV \
"<header>" \
"  <div><div class='logo'>&#9888; EHS</div>" \
"  <div class='sub'>Emergency Help System &mdash; Offline LAN Mode</div></div>" \
"  <nav>" \
"    <a href='/'>Submit</a>" \
"    <a href='/requests'>View Requests</a>" \
"    <a href='/status'>Status</a>" \
"  </nav>" \
"</header><main>"

#define HTML_FOOT \
"</main><footer>Emergency Help System &bull; Offline LAN &bull; " \
"No internet required</footer></body></html>"

//page builders

static void build_homepage(char *buf, size_t len) {
    snprintf(buf, len,
        "<!DOCTYPE html><html lang='en'><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Emergency Help System</title>"
        SHARED_CSS
        "</head><body>"
        HTML_HEADER_NAV
        "<h1>Submit Help Request</h1>"
        "<p class='sub2'>Fill in the form below. Your request will be "
        "recorded and visible to all responders on this network.</p>"
        "<div class='card'>"
        "  <div class='alert-box'>"
        "    &#9888;&nbsp;<strong>EMERGENCY USE ONLY.</strong>&nbsp;"
        "    All submissions are logged with a timestamp."
        "  </div>"
        "  <form method='POST' action='/help'>"
        "    <label for='name'>Your Name</label>"
        "    <input type='text' id='name' name='name' "
        "           placeholder='Full name' maxlength='100' required>"
        "    <label for='location'>Location</label>"
        "    <input type='text' id='location' name='location' "
        "           placeholder='e.g. Room 204, Building A' "
        "           maxlength='150' required>"
        "    <label for='message'>Describe the Emergency</label>"
        "    <textarea id='message' name='message' "
        "              placeholder='Describe what help you need...' "
        "              maxlength='500' required></textarea>"
        "    <button type='submit'>&#128680; Send Help Request</button>"
        "  </form>"
        "</div>"
        HTML_FOOT
    );
}

static void build_success_page(char *buf, size_t len, int req_id) {
    snprintf(buf, len,
        "<!DOCTYPE html><html lang='en'><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Request Sent – EHS</title>"
        SHARED_CSS
        "</head><body>"
        HTML_HEADER_NAV
        "<h1>Request Submitted</h1>"
        "<p class='sub2'>Your emergency request has been recorded.</p>"
        "<div class='card'>"
        "  <p style='font-size:1.1rem;margin-bottom:14px;'>"
        "    <strong style='color:#27ae60;'>&#10003; Request #%04d received.</strong>"
        "  </p>"
        "  <p style='color:var(--muted);font-size:.9rem;line-height:1.6;'>"
        "    Your request is now stored and visible to all responders "
        "    on this local network. Stay calm and remain at your stated "
        "    location if it is safe to do so."
        "  </p>"
        "  <br>"
        "  <a class='btn' href='/'>&#43; New Request</a>"
        "  <a class='btn-ghost' href='/requests'>View All Requests</a>"
        "</div>"
        HTML_FOOT,
        req_id
    );
}

static void build_requests_page(char *buf, size_t buf_len) {
    /* Build the entries HTML into a temporary buffer first */
    char entries[65536] = "";
    int  count = 0;
 
    FILE *f = fopen(REQUESTS_FILE, "r");
    if (f) {
        char line[1024];
        /* File line format:  ID|timestamp|name|location|message */
        while (fgets(line, sizeof(line), f)) {
            size_t l = strlen(line);
            if (l > 0 && line[l-1] == '\n') line[l-1] = '\0';
            if (strlen(line) < 5) continue;
 
            char *id_s   = strtok(line, "|");
            char *ts_s   = strtok(NULL, "|");
            char *name_s = strtok(NULL, "|");
            char *loc_s  = strtok(NULL, "|");
            char *msg_s  = strtok(NULL, "|");
            if (!id_s || !ts_s || !name_s || !loc_s || !msg_s) continue;
 
            char entry[2048];
            snprintf(entry, sizeof(entry),
                "<div class='req-card'>"
                "  <div class='req-meta'>"
                "    <span class='badge badge-red'>REQ #%s</span>&nbsp;&nbsp;%s"
                "  </div>"
                "  <div class='req-name'>%s</div>"
                "  <div class='req-msg'>%s</div>"
                "  <div class='req-loc'>&#128205; Location: <span>%s</span></div>"
                "</div>",
                id_s, ts_s, name_s, msg_s, loc_s
            );
            if (strlen(entries) + strlen(entry) + 1 < sizeof(entries))
                strncat(entries, entry, sizeof(entries) - strlen(entries) - 1);
            count++;
        }
        fclose(f);
    }
 char count_info[128];
    if (count == 0) {
        snprintf(entries, sizeof(entries),
            "<div class='empty'>&#128203; No help requests on file yet.</div>");
        snprintf(count_info, sizeof(count_info), "0 requests on file");
    } else {
        snprintf(count_info, sizeof(count_info),
                 "%d request%s on file", count, count == 1 ? "" : "s");
    }

    snprintf(buf, buf_len,
        "<!DOCTYPE html><html lang='en'><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<meta http-equiv='refresh' content='30'>"   /* auto-refresh */
        "<title>All Requests – EHS</title>"
        SHARED_CSS
        "</head><body>"
        "<header>"
        "  <div><div class='logo'>&#9888; EHS</div>"
        "  <div class='sub'>Emergency Help System &mdash; Offline LAN Mode</div></div>"
        "  <nav>"
        "    <a href='/'>Submit</a>"
        "    <a href='/requests' class='active'>View Requests</a>"
        "    <a href='/status'>Status</a>"
        "  </nav>"
        "</header><main>"
        "<h1>Active Help Requests</h1>"
        "<p class='sub2'>"
        "  <span class='live-dot'></span>"
        "  This page auto-refreshes every 30 seconds."
        "</p>"
        "<div class='toolbar'>"
        "  <span class='toolbar-info'>%s</span>"
        "  <a class='btn' href='/'>&#43; New Request</a>"
        "</div>"
        "%s"
        HTML_FOOT,
        count_info, entries
    );
}

//http response helpers
static void send_response(int fd,
                           int code, const char *status,
                           const char *ctype,
                           const char *body, size_t blen) {
    char hdr[512];
    int  hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        code, status, ctype, blen);
    send(fd, hdr, hlen, 0);
    if (body && blen > 0)
        send(fd, body, blen, 0);
}
 
static void send_html(int fd, int code, const char *status, const char *html) {
    send_response(fd, code, status, "text/html; charset=utf-8",
                  html, strlen(html));
}
 
static void send_404(int fd) {
    const char *body =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<title>404 – EHS</title>" SHARED_CSS "</head><body>"
        HTML_HEADER_NAV
        "<h1>404 – Not Found</h1>"
        "<p class='sub2'>The requested page does not exist.</p>"
        "<a class='btn' href='/'>Go Home</a>"
        HTML_FOOT;
    send_html(fd, 404, "Not Found", body);
}
 
static void send_405(int fd) {
    const char *body =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<title>405 – EHS</title>" SHARED_CSS "</head><body>"
        HTML_HEADER_NAV
        "<h1>405 – Method Not Allowed</h1>"
        "<p class='sub2'>This endpoint does not support that HTTP method.</p>"
        "<a class='btn' href='/'>Go Home</a>"
        HTML_FOOT;
    send_html(fd, 405, "Method Not Allowed", body);
}

//route handlers

static void handle_home(int fd) {
    char buf[32768];
    build_homepage(buf, sizeof(buf));
    send_html(fd, 200, "OK", buf);
}
 
static void handle_post_help(int fd, const char *req_buf) {
    /* Locate HTTP body (after blank line) */
    const char *body = strstr(req_buf, "\r\n\r\n");
    if (!body) { send_405(fd); return; }
    body += 4;
 
    char name[MAX_FIELD_LEN]     = "";
    char location[MAX_FIELD_LEN] = "";
    char message[MAX_FIELD_LEN]  = "";
 
    extract_field(body, "name",     name,     sizeof(name));
    extract_field(body, "location", location, sizeof(location));
    extract_field(body, "message",  message,  sizeof(message));
 
    sanitize(name,     sizeof(name));
    sanitize(location, sizeof(location));
    sanitize(message,  sizeof(message));
 
    /* Basic validation */
    if (strlen(name) == 0 || strlen(message) == 0) {
        const char *err =
            "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
            "<title>Error – EHS</title>" SHARED_CSS "</head><body>"
            HTML_HEADER_NAV
            "<h1>Invalid Submission</h1>"
            "<p class='sub2'>Name and message are required fields.</p>"
            "<a class='btn' href='/'>Try Again</a>"
            HTML_FOOT;
        send_html(fd, 400, "Bad Request", err);
        return;
    }
 
    /* Timestamp */
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);
 
    int req_id = next_request_id();
 
    /* Persist: ID|timestamp|name|location|message */
    FILE *f = fopen(REQUESTS_FILE, "a");
    if (f) {
        fprintf(f, "%04d|%s|%s|%s|%s\n",
                req_id, timestamp, name, location, message);
        fclose(f);
        printf(CLR_GREEN "[+] REQ #%04d — %s — loc: %s\n" CLR_RESET,
               req_id, name, location);
    } else {
        fprintf(stderr, CLR_RED "[-] Cannot open %s: %s\n" CLR_RESET,
                REQUESTS_FILE, strerror(errno));
    }
 
    char success[8192];
    build_success_page(success, sizeof(success), req_id);
    send_html(fd, 200, "OK", success);
}
 
static void handle_view_requests(int fd) {
    char buf[131072];
    build_requests_page(buf, sizeof(buf));
    send_html(fd, 200, "OK", buf);
}
 
static void handle_status(int fd) {
    int count = 0;
    FILE *f = fopen(REQUESTS_FILE, "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) count++;
        fclose(f);
    }
    char json[256];
    snprintf(json, sizeof(json),
             "{\"status\":\"running\",\"requests_stored\":%d}", count);
    send_response(fd, 200, "OK", "application/json", json, strlen(json));
}

//request dispatcher
static void dispatch(int client_fd, const char *buf) {
    if (strlen(buf) < 10) { send_404(client_fd); return; }
 
    char method[8]  = {0};
    char path[256]  = {0};
    sscanf(buf, "%7s %255s", method, path);
 
    printf(CLR_CYAN "[>] %s %s\n" CLR_RESET, method, path);
 
    if      (strcmp(path, "/")         == 0) {
        if (strcmp(method, "GET")  == 0) handle_home(client_fd);
        else                             send_405(client_fd);
    } else if (strcmp(path, "/help")   == 0) {
        if (strcmp(method, "POST") == 0) handle_post_help(client_fd, buf);
        else                             send_405(client_fd);
    } else if (strcmp(path, "/requests") == 0) {
        if (strcmp(method, "GET")  == 0) handle_view_requests(client_fd);
        else                             send_405(client_fd);
    } else if (strcmp(path, "/status") == 0) {
        if (strcmp(method, "GET")  == 0) handle_status(client_fd);
        else                             send_405(client_fd);
    } else {
        send_404(client_fd);
    }

}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    if (argc >= 2) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Usage: %s [port]  (default %d)\n",
                    argv[0], DEFAULT_PORT);
            return 1;
        }
    }
    //create tcp socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }
 
    // Allow quick reuse of the port after restart
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
 
    // 2. Bind to all interfaces on specified port
    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);
 
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(server_fd); return 1;
    }
 
    //listen
    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen"); close(server_fd); return 1;
    }
 
    // Reap zombie children automatically 
    struct sigaction sa = {0};
    sa.sa_handler = sigchld_handler;
    sa.sa_flags   = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);
     
    printf(CLR_BOLD "  Emergency Help System — Offline LAN Server\n" CLR_RESET);
    printf("  ─────────────────────────────────────────────\n");
    printf("  " CLR_GREEN "Listening" CLR_RESET "     http://0.0.0.0:%d\n", port);
    printf("  " CLR_GREEN "Storage"   CLR_RESET "      %s\n", REQUESTS_FILE);
    printf("  " CLR_YELLOW "Routes" CLR_RESET "       "
           "GET /  |  POST /help  |  GET /requests  |  GET /status\n");
    printf("  Press Ctrl+C to stop.\n\n");
 
    // Accept loop  fork() per connection for concurrency 
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
 
        int client_fd = accept(server_fd,
                               (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }
 
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr,
                  client_ip, sizeof(client_ip));
        printf(CLR_YELLOW "[*] Connection from %s\n" CLR_RESET, client_ip);
 
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close(client_fd);
            continue;
        }
 
        if (pid == 0) {
            // Child: handle one request then exit
            close(server_fd);
 
            char req_buf[RECV_BUFFER_SIZE + 1] = {0};
            ssize_t bytes = recv(client_fd, req_buf, RECV_BUFFER_SIZE, 0);
            if (bytes > 0) {
                req_buf[bytes] = '\0';
                dispatch(client_fd, req_buf);
            }
 
            close(client_fd);
            exit(0);
        }
 
        // Parent: close duplicated fd loop 
        close(client_fd);
    }
 
    close(server_fd);
    return 0;
}