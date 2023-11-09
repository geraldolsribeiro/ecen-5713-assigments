#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#define handle_error(msg) \
  do {                    \
    perror(msg);          \
    exit(EXIT_FAILURE);   \
  } while (0)

const int max_number_of_threads = 10;
int keep_running = 1;
const char *output_filename = "/var/tmp/aesdsocketdata";

pthread_mutex_t queue_mutex;
pthread_cond_t queue_condition;

void finish() {
  keep_running = 0;
  for (int t = 0; t < max_number_of_threads; ++t) {
    pthread_cond_signal(&queue_condition);
  }
  syslog(LOG_NOTICE, "Caught signal, exiting");
}

typedef struct Task {
  pthread_mutex_t *writing_output_mutex;
  FILE *output_file;
  int client_socket_fd;
  int id;
} Task;

Task task_queue[256];
int task_counter = 0;

void submit_task(Task task) {
  pthread_mutex_lock(&queue_mutex);
  task_queue[task_counter] = task;
  task_counter++;
  pthread_mutex_unlock(&queue_mutex);
  pthread_cond_signal(&queue_condition);
}

void execute_task(Task *task, int *thread_buffer_size, char **thread_buffer) {
  ssize_t valread =
      read(task->client_socket_fd, *thread_buffer, *thread_buffer_size - 1);
  (*thread_buffer)[valread] = 0;

  pthread_mutex_lock(task->writing_output_mutex);

  fseek(task->output_file, 0, SEEK_END);
  char *tok = strtok(*thread_buffer, "\n");
  while (tok != NULL) {
    fprintf(task->output_file, "%s\n", tok);
    tok = strtok(NULL, "\n");
  }

  long file_size = ftell(task->output_file);

  if (file_size > *thread_buffer_size) {
    *thread_buffer_size = file_size * 2;
    *thread_buffer = realloc(*thread_buffer, *thread_buffer_size);
  }

  rewind(task->output_file);
  fread(*thread_buffer, file_size, 1, task->output_file);
  (*thread_buffer)[file_size] = 0;
  fseek(task->output_file, 0, SEEK_END);

  pthread_mutex_unlock(task->writing_output_mutex);

  send(task->client_socket_fd, *thread_buffer, file_size, 0);
  close(task->client_socket_fd);
}

void *start_thread(void *args) {
  int thread_buffer_size = 10 * 1024 * 1024;
  char *thread_buffer = calloc(thread_buffer_size, sizeof(char));
  if (thread_buffer == NULL) {
    handle_error("calloc");
  }

  while (keep_running == 1) {
    Task task;
    pthread_mutex_lock(&queue_mutex);
    while (task_counter == 0 && keep_running == 1) {
      pthread_cond_wait(&queue_condition, &queue_mutex);
    }
    if (keep_running == 0) {
      pthread_mutex_unlock(&queue_mutex);
      break;
    }

    task = task_queue[0];
    for (int q = 0; q < task_counter - 1; ++q) {
      task_queue[q] = task_queue[q + 1];
    }
    task_counter--;
    pthread_mutex_unlock(&queue_mutex);
    execute_task(&task, &thread_buffer_size, &thread_buffer);
  }
  free(thread_buffer);
  return NULL;
}

void *timestamp_thread(void *args) {
  Task *task = (Task *)args;

  while (0 && keep_running) {
    for (int i = 0; i < 10 && keep_running == 1; i++) {
      sleep(1);
    }
    char s[100];
    time_t t = time(NULL);
    struct tm *tmp;
    tmp = localtime(&t);
    strftime(s, sizeof(s), "%Y-%m-%d %H:%M:%S", tmp);

    pthread_mutex_lock(task->writing_output_mutex);
    fprintf(task->output_file, "timestamp:%s\n", s);
    pthread_mutex_unlock(task->writing_output_mutex);
  }
  return NULL;
}

int main(int argc, char **argv) {
  int is_daemon = 0;

  for (int i = 1; i < argc; i++) {
    if (strncmp(argv[i], "-d", 2) == 0) {
      is_daemon = 1;
      break;
    }
  }

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

  if (is_daemon) {
    pid_t pid = fork();
    if (pid < 0) {
      perror("Fail to fork");
      exit(EXIT_FAILURE);
    }
    if (pid > 0) {
      // parent process
      exit(EXIT_SUCCESS);
    }

    /* if (setsid() < 0) { */
    /*   exit(EXIT_FAILURE); */
    /* } */
    /*  */
    /* signal(SIGCHLD, SIG_IGN); */
    /* signal(SIGHUP, SIG_IGN); */
    /*  */
    /* pid = fork(); */
    /* if (pid < 0) { */
    /*   perror("Fail to fork (second time)"); */
    /*   exit(EXIT_FAILURE); */
    /* } */
    /* if (pid > 0) { */
    /*   // parent process */
    /*   exit(EXIT_SUCCESS); */
    /* } */
    /* umask(0); */
    /* chdir("/"); */
    /* for (int x = sysconf(_SC_OPEN_MAX); x >= 0; x--) { */
    /*   close(x); */
    /* } */
  }

  if (listen(server_socket_fd, 3) < 0) {
    handle_error("listen");
  }

  openlog("ecen-5713", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL1);
  syslog(LOG_NOTICE, "Program started by User %d", getuid());

  pthread_t timestamp_thread_id;
  pthread_t thread_pool[max_number_of_threads];
  int is_pool_free_on_position[max_number_of_threads];
  memset(&is_pool_free_on_position, 1, sizeof(is_pool_free_on_position));
  pthread_mutex_t writing_output_mutex;
  int counter = 0;

  pthread_mutex_init(&writing_output_mutex, NULL);
  pthread_mutex_init(&queue_mutex, NULL);
  pthread_cond_init(&queue_condition, NULL);

  Task timestamp_task = {.writing_output_mutex = &writing_output_mutex,
                         .output_file = output_file};

  pthread_create(&timestamp_thread_id, NULL, timestamp_thread, &timestamp_task);

  for (int t = 0; t < max_number_of_threads; ++t) {
    if (pthread_create(&thread_pool[t], NULL, start_thread, NULL) != 0) {
      handle_error("pthread_create");
    }
  }

  while (keep_running) {
    int client_socket_fd;
    struct sockaddr_storage their_addr;  // connector's address information
    socklen_t addrlen = sizeof(their_addr);

    if ((client_socket_fd = accept(
             server_socket_fd, (struct sockaddr *)&their_addr, &addrlen)) < 0) {
      if (keep_running == 0) {
        break;
      } else if (errno == EWOULDBLOCK) {
        usleep(1000);
        continue;
      }
      handle_error("accept");
    }

    counter++;
    // printf("%d Accepted fd: %d\n", counter, client_socket_fd);

    syslog(LOG_INFO, "Accepted connection from %s",
           inet_ntoa(((struct sockaddr_in *)&their_addr)->sin_addr));

    Task task = {.writing_output_mutex = &writing_output_mutex,
                 .client_socket_fd = client_socket_fd,
                 .output_file = output_file,
                 .id = counter};

    submit_task(task);
  }

  for (int t = 0; t < max_number_of_threads; ++t) {
    if (pthread_join(thread_pool[t], NULL) != 0) {
      handle_error("pthread_create");
    }
  }

  pthread_join(timestamp_thread_id, NULL);

  pthread_mutex_destroy(&queue_mutex);
  pthread_mutex_destroy(&writing_output_mutex);
  pthread_cond_destroy(&queue_condition);

  fclose(output_file);
  close(server_socket_fd);
  unlink(output_filename);
  closelog();
  return 0;
}

