#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#define handle_error(msg) \
  do {                    \
    perror(msg);          \
    exit(EXIT_FAILURE);   \
  } while (0)

int keep_running = 1;
int client_socket_fd;
const char *output_filename = "/var/tmp/aesdsocketdata";

void finish() {
  printf("Finishing...\n");
  keep_running = 0;

  syslog(LOG_NOTICE, "Caught signal, exiting");
}

int main(void) {
  signal(SIGINT, finish);
  signal(SIGTERM, finish);

  FILE *output_file = fopen(output_filename, "a+");

  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;  // use my IP

  int server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket_fd == -1) {
    handle_error("socket");
  }

  int opt = 1;
  if (setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                 &opt, sizeof(opt))) {
    handle_error("setsockopt");
  }

  int flags = fcntl(server_socket_fd, F_GETFL);
  if (flags < 0) {
    handle_error("fcntl");
  }
  flags = fcntl(server_socket_fd, F_SETFL, flags | O_NONBLOCK);
  if (flags < 0) {
    handle_error("fcntl");
  }

  struct sockaddr_in server_address;
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(9000);

  if (bind(server_socket_fd, (struct sockaddr *)&server_address,
           sizeof(server_address)) < 0) {
    handle_error("bind");
  }

  if (listen(server_socket_fd, 3) < 0) {
    handle_error("listen");
  }

  int buffer_size = 1024 * 1024 * 4;
  char *buffer = calloc(buffer_size, sizeof(char));
  if (buffer == NULL) {
    handle_error("calloc");
  }

  openlog("ecen-5713", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL1);
  syslog(LOG_NOTICE, "Program started by User %d", getuid());

  while (keep_running) {
    printf("Waiting for connections... ");
    fflush(stdout);
    int client_socket_fd;
    struct sockaddr_storage their_addr;  // connector's address information
    socklen_t addrlen = sizeof(their_addr);

    if ((client_socket_fd = accept(
             server_socket_fd, (struct sockaddr *)&their_addr, &addrlen)) < 0) {
      if (keep_running == 0) {
        break;
      } else if (errno == EWOULDBLOCK) {
        usleep(500000);
        printf(":(\n");
        continue;
      }
      handle_error("accept");
    }

    // Logs message to the syslog “Accepted connection from xxx” where XXXX is
    // the IP address of the connected client.
    syslog(LOG_INFO, "Accepted connection from %s",
           inet_ntoa(((struct sockaddr_in *)&their_addr)->sin_addr));

    ssize_t valread = read(client_socket_fd, buffer,
                           buffer_size - 1);  // subtract 1 for the null
                                              // terminator at the end
    printf("Read %ld bytes\n", valread);
    buffer[valread] = 0;

    fseek(output_file, 0, SEEK_END);
    printf("POS %ld\n", ftell(output_file));
    char *tok = strtok(buffer, "\n");
    while (tok != NULL) {
      fprintf(output_file, "%s\n", tok);
      tok = strtok(NULL, "\n");
    }

    printf("POS %ld\n", ftell(output_file));
    long file_size = ftell(output_file);

    if (file_size > buffer_size) {
      printf("Expanding buffer to %ld\n", file_size + 1);
      buffer_size = file_size * 2;
      buffer = realloc(buffer, buffer_size);
    }

    rewind(output_file);
    fread(buffer, file_size, 1, output_file);
    buffer[file_size] = 0;
    fseek(output_file, 0, SEEK_END);

    printf("Sending %ld bytes\n", file_size);
    send(client_socket_fd, buffer, file_size, 0);
    close(client_socket_fd);
  }
  printf("Freeing memory and closing descriptors\n");
  free(buffer);
  closelog();
  fclose(output_file);
  close(server_socket_fd);

  printf("Removing %s\n", output_filename);
  unlink(output_filename);
  return 0;
}

