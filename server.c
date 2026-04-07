#include <stdio.h>
#include <stdlib.h> //for exit()
#include <string.h> //for strlen()
#include <unistd.h> //for read(), write() and close()
#include <arpa/inet.h> //networking stuff ip port socket

#define PORT 8080

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

        listen(server_socket, 5); //listen for incoming connections, max 5 in queue
        printf("HTTP Server running on port %d...\n", PORT);

        while(1){
            //accept client connection
            client_socket = accept(server_socket, (struct sockaddr *)&address, (socklen_t*)&addrlen);
            if (client_socket < 0){
                perror("accept failed");
                continue;
            }
            //read data from client
            read(client_socket, buffer, 30000);
            printf("Received request:\n%s\n", buffer);

            //send response to client
            char *response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "\r\n"
            "<html><body><h1>Hello from C Server </h1></body></html>";

            send(client_socket, response, strlen(response), 0);
            close(client_socket);
        }
        return 0;
}