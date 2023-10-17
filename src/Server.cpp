#include <iostream>
#include <cstdlib>
#include <string>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <unordered_map>

std::unordered_map<std::string, std::string> store;

int handle_request(int client_fd);
int accept_new_connection(int server_fd);

int main(int argc, char **argv) {
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
   std::cerr << "Failed to create server socket\n";
   return 1;
  }
  
  // Since the tester restarts your program quite often, setting REUSE_PORT
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(6379);
  
  if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
    std::cerr << "Failed to bind to port 6379\n";
    return 1;
  }
  
  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }

  // Event loop begins
  fd_set current, ready;
  FD_ZERO(&current);
  FD_SET(server_fd, &current);

  while (true) {
    ready = current;

    if (select(FD_SETSIZE, &ready, nullptr, nullptr, nullptr) < 0) {
      std::cerr << "select failed\n";
      return 1;
    }

    for (unsigned int i = 0; i < FD_SETSIZE; ++i) {
      if (FD_ISSET(i, &ready)) {
        if (i == server_fd) {
          int client_fd = accept_new_connection(server_fd);
          if (client_fd < 0) {
            std::cerr << "Client connection failed\n";
            return 1;
          }

          FD_SET(client_fd, &current);
        } else {
          int bytes = handle_request(i);
          if (bytes < 0) {
            std::cerr << "Error handling client request\n";
            return 1;
          }
        }
      }
    }
  }

  close(server_fd);

  return 0;
}

int handle_request(int client_fd) {
  char buffer[1024];

  int bytes = recv(client_fd, buffer, 1024, 0);
  if (bytes <= 0) {
    return bytes;
  }

  if (buffer[0] == '*') {
    int size = buffer[1] - '0';

    std::string resp(buffer, 4,  bytes);
    std::stringstream ss(resp);

    std::string arr[size];
    for (unsigned int i = 0; i < size; ++i) {
      std::string temp; ss >> temp;
      ss >> arr[i];
    }
    
    if (arr[0] == "echo") {
      std::string reply = "+" + arr[1] + "\r\n";
      int send_bytes = reply.length();
      char char_array[send_bytes + 1];
      strcpy(char_array, reply.c_str());

      if (send(client_fd, char_array, send_bytes, 0) < 0) {
        std::cerr << "Failed to send to socket\n";
      }
    } else if (arr[0] == "set") {
      store.insert_or_assign(arr[1], arr[2]);

      if (send(client_fd, "+OK\r\n", 5, 0) < 0) {
        std::cerr << "Failed to send to socket\n";
      }
    } else if (arr[0] == "get") {
      std::string reply = "+" + store.at(arr[1]) + "\r\n";
      int send_bytes = reply.length();
      char char_array[send_bytes + 1];
      strcpy(char_array, reply.c_str());

      if (send(client_fd, char_array, send_bytes, 0) < 0) {
        std::cerr << "Failed to send to socket\n";
      }
    } else {
      if (send(client_fd, "+PONG\r\n", 7, 0) < 0) {
        std::cerr << "Failed to send to socket\n";
      }
    }
  }

  return bytes;
}

int accept_new_connection(int server_fd) {
  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);

  return accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
}
