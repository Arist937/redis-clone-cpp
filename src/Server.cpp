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
#include <unordered_map>
#include <chrono>
#include <poll.h>
#include <fcntl.h>

std::unordered_map<std::string, std::string> store;
std::unordered_map<std::string, std::chrono::high_resolution_clock::duration> expirations;

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

  // Set socket to be non-blocking
  int flags = fcntl(server_fd, F_GETFL, 0);
  if (flags == -1) {
    std::cerr << "Error getting flags\n";
    return 1;
  }

  if (fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    std::cerr << "Error setting non-blocking flag\n";
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

  // Set up event loop vars
  struct pollfd fds[100];
  memset(fds, 0, sizeof(fds));

  fds[0].fd = server_fd;
  fds[0].events = POLLIN;

  nfds_t nfds = 1;
  int timeout = (3 * 60 * 1000);

  // Event loop begins
  do {
    std::cout << "Polling...\n";
    int rc = poll(fds, nfds, timeout);
    if (rc < 0) {
      std::cerr << "Poll failed\n";
      break;
    }

    if (rc == 0) {
      std::cerr << "Poll timed out\n";
      break;
    }

    nfds_t current_nfds = nfds;
    for (int i = 0; i < current_nfds; ++i) {
      if (fds[i].revents & POLLIN) {
        // Case: Incoming connection
        if (fds[i].fd == server_fd) {
          std::cout << "Accepting incoming clients...\n";
          int client_fd;
          // Loop to accept all incoming connections
          do {
            client_fd = accept(server_fd, NULL, NULL);
            if (client_fd < 0) {
              std::cerr << "Client connection failed\n";
              break;
            }

            fds[nfds].fd = client_fd;
            fds[nfds].events = POLLIN;
            ++nfds;
          } while (client_fd != -1);
        }
        // Case: Client is ready for data
        else {
          std::cout << "Handling client request...\n";
          int bytes = handle_request(fds[i].fd);
          if (bytes < 0) {
            std::cerr << "Error handling client request\n";
            return 1;
          }

          if (bytes == 0) {
            std::cout << "Client closed\n";
            memset(fds + i, 0, sizeof(pollfd));
            close(fds[i].fd);
          }
        }
      }
    }
  } while (true);

  for (int i = 0; i < nfds; ++i) {
    if(fds[i].fd >= 0) {
      close(fds[i].fd);
    }
  }

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

      if (size > 3) {
        if (arr[3] == "px") {
          int ms = std::stoi(arr[4]);
          auto expiry_time = std::chrono::high_resolution_clock::now().time_since_epoch() + std::chrono::milliseconds(ms);

          expirations.insert_or_assign(arr[1], expiry_time);
        }
      }

      if (send(client_fd, "+OK\r\n", 5, 0) < 0) {
        std::cerr << "Failed to send to socket\n";
      }
    } else if (arr[0] == "get") {
      if (expirations.contains(arr[1])) {
        auto now = std::chrono::high_resolution_clock::now().time_since_epoch();

        if (now >= expirations.at(arr[1])) {
          if (send(client_fd, "$-1\r\n", 5, 0) < 0) {
            std::cerr << "Failed to send to socket\n";
          }

          return bytes;
        }
      }

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
