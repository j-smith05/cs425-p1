#define _POSIX_C_SOURCE 200809L

#include "lab.h"
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>


int smtp_reply_code(const char *line)
{
  if (line == NULL || line[0] < '0' || line[0] > '9' ||
      line[1] < '0' || line[1] > '9' ||
      line[2] < '0' || line[2] > '9')
    return -1;

  return (line[0] - '0') * 100 + (line[1] - '0') * 10 + line[2] - '0';
}


char *smtp_command(const char *verb, const char *argument)
{
  int length = snprintf(NULL, 0, "%s%s\r\n", verb,
                        argument == NULL ? "" : argument);
  char *command;

  /* GCOVR_EXCL_START */
  if (length < 0)
    return NULL;
  /* GCOVR_EXCL_STOP */

  command = malloc((size_t)length + 1);

  /* GCOVR_EXCL_START */
  if (command != NULL)
  /* GCOVR_EXCL_STOP */
    (void)snprintf(command, (size_t)length + 1, "%s%s\r\n", verb,
                   argument == NULL ? "" : argument);

  return command;
}


static char *smtp_address_command(const char *verb, const char *address)
{
  int length = snprintf(NULL, 0, "%s<%s>\r\n", verb, address);
  char *command;

  /* GCOVR_EXCL_START */
  if (length < 0)
    return NULL;
  /* GCOVR_EXCL_STOP */

  command = malloc((size_t)length + 1);

  /* GCOVR_EXCL_START */
  if (command != NULL)
  /* GCOVR_EXCL_STOP */
    (void)snprintf(command, (size_t)length + 1, "%s<%s>\r\n", verb, address);

  return command;
}


char *smtp_dot_stuff(const char *body)
{
  size_t length = strlen(body);
  char *result = malloc(length * 2 + 1);
  size_t output = 0;
  int line_start = 1;

  /* GCOVR_EXCL_START */
  if (result == NULL)
    return NULL;
  /* GCOVR_EXCL_STOP */

  for (size_t index = 0; index < length; index++)
  {
    if (line_start && body[index] == '.')
      result[output++] = '.';

    result[output++] = body[index];
    line_start = body[index] == '\n';
  }

  result[output] = '\0';

  return result;
}


char *smtp_data(const char *from, const char *to, const char *subject,
                const char *body)
{
  char *stuffed = smtp_dot_stuff(body);
  int length;
  char *data;

  /* GCOVR_EXCL_START */
  if (stuffed == NULL)
    return NULL;
  /* GCOVR_EXCL_STOP */

  length = snprintf(NULL, 0,
                    "From: %s\r\nTo: %s\r\nSubject: %s\r\n\r\n%s%s.\r\n",
                    from, to, subject, stuffed,
                    stuffed[0] == '\0' || stuffed[strlen(stuffed) - 1] == '\n'
                        ? ""
                        : "\r\n");

  /* GCOVR_EXCL_START */
  if (length < 0)
  {
    free(stuffed);
    return NULL;
  }
  /* GCOVR_EXCL_STOP */

  data = malloc((size_t)length + 1);

  /* GCOVR_EXCL_START */
  if (data != NULL)
  /* GCOVR_EXCL_STOP */
    (void)snprintf(data, (size_t)length + 1,
                   "From: %s\r\nTo: %s\r\nSubject: %s\r\n\r\n%s%s.\r\n",
                   from, to, subject, stuffed,
                   stuffed[0] == '\0' || stuffed[strlen(stuffed) - 1] == '\n'
                       ? ""
                       : "\r\n");

  free(stuffed);

  return data;
}


int smtp_socket_read(void *context, char *buffer, size_t size)
{
  int fd = *(int *)context;
  ssize_t count = recv(fd, buffer, size, 0);

  return count < 0 ? -1 : (int)count;
}


int smtp_socket_write(void *context, const char *buffer, size_t size)
{
  int fd = *(int *)context;
  ssize_t count = send(fd, buffer, size, 0);

  return count < 0 ? -1 : (int)count;
}


int smtp_connect(const char *server, const char *port,
                 smtp_transport *transport)
{
  struct addrinfo hints = {0};
  struct addrinfo *addresses = NULL;
  struct addrinfo *address;
  int fd = -1;

  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(server, port, &hints, &addresses) != 0)
    return -1;

  for (address = addresses; address != NULL; address = address->ai_next)
  {
    fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);

    if (fd >= 0 && connect(fd, address->ai_addr, address->ai_addrlen) == 0)
      break;

    if (fd >= 0)
    {
      close(fd);
      fd = -1;
    }
  }

  freeaddrinfo(addresses);

  if (fd < 0)
    return -1;

  int *socket_context = malloc(sizeof(*socket_context));

  /* GCOVR_EXCL_START */
  if (socket_context == NULL)
  {
    close(fd);
    return -1;
  }
  /* GCOVR_EXCL_STOP */

  *socket_context = fd;

  transport->read = smtp_socket_read;
  transport->write = smtp_socket_write;
  transport->context = socket_context;
  transport->input_start = 0;
  transport->input_end = 0;

  return 0;
}


int smtp_close(smtp_transport *transport)
{
  if (transport != NULL && transport->context != NULL)
  {
    close(*(int *)transport->context);
    free(transport->context);
    transport->context = NULL;
  }

  return 0;
}


int smtp_read_line(smtp_transport *transport, char *line, size_t size)
{
  size_t length = 0;

  if (size == 0)
    return -1;

  while (length + 1 < size)
  {
    if (transport->input_start == transport->input_end)
    {
      int count = transport->read(transport->context, transport->input,
                                  sizeof(transport->input));

      if (count <= 0)
        return -1;

      transport->input_start = 0;
      transport->input_end = (size_t)count;
    }

    line[length++] = transport->input[transport->input_start++];

    if (length >= 2 && line[length - 2] == '\r' && line[length - 1] == '\n')
    {
      line[length] = '\0';
      return (int)length;
    }
  }

  return -1;
}


int smtp_read_reply(smtp_transport *transport, char *reply, size_t size)
{
  char line[4096];
  size_t used = 0;
  int code;

  reply[0] = '\0';

  do
  {
    int length = smtp_read_line(transport, line, sizeof(line));

    if (length < 0 || used + (size_t)length + 1 > size)
      return -1;

    memcpy(reply + used, line, (size_t)length);
    used += (size_t)length;
    reply[used] = '\0';

    code = smtp_reply_code(line);

    if (code < 0)
      return -1;

  } while (strlen(line) < 4 || line[3] != ' ');

  return code;
}


int smtp_write_all(smtp_transport *transport, const char *data, size_t size)
{
  size_t written = 0;

  while (written < size)
  {
    int count = transport->write(transport->context, data + written,
                                 size - written);

    if (count <= 0)
      return -1;

    written += (size_t)count;
  }

  return 0;
}


int smtp_send_command(smtp_transport *transport, const char *command,
                      int expected_code, char *reply, size_t reply_size)
{
  if (smtp_write_all(transport, command, strlen(command)) < 0 ||
      smtp_read_reply(transport, reply, reply_size) != expected_code)
    return -1;

  return 0;
}


int smtp_session(smtp_transport *transport, const char *from, const char *to,
                 const char *helo_host, const char *subject,
                 const char *body)
{
  char *command;
  char *data;
  char reply[16384];

  if (smtp_read_reply(transport, reply, sizeof(reply)) != 220)
  {
    fprintf(stderr, "Unexpected SMTP reply: %s", reply);
    return -1;
  }


  command = smtp_command("HELO ", helo_host);

  if (command == NULL || smtp_send_command(transport, command, 250, reply,
                                           sizeof(reply)) < 0)
  {
    fprintf(stderr, "Unexpected SMTP reply: %s", reply);
    free(command);
    return -1;
  }

  free(command);


  command = smtp_address_command("MAIL FROM:", from);

  /* GCOVR_EXCL_START */
  if (command == NULL)
    return -1;
  /* GCOVR_EXCL_STOP */

  if (smtp_send_command(transport, command, 250, reply, sizeof(reply)) < 0)
  {
    fprintf(stderr, "Unexpected SMTP reply: %s", reply);
    free(command);
    return -1;
  }

  free(command);


  command = smtp_address_command("RCPT TO:", to);

  /* GCOVR_EXCL_START */
  if (command == NULL)
    return -1;
  /* GCOVR_EXCL_STOP */

  if (smtp_send_command(transport, command, 250, reply, sizeof(reply)) < 0)
  {
    fprintf(stderr, "Unexpected SMTP reply: %s", reply);
    free(command);
    return -1;
  }

  free(command);


  command = smtp_command("DATA", NULL);

  if (command == NULL || smtp_send_command(transport, command, 354, reply,
                                           sizeof(reply)) < 0)
  {
    fprintf(stderr, "Unexpected SMTP reply: %s", reply);
    free(command);
    return -1;
  }

  free(command);


  data = smtp_data(from, to, subject, body);

  /* GCOVR_EXCL_START */
  if (data == NULL)
    return -1;
  /* GCOVR_EXCL_STOP */

  if (smtp_write_all(transport, data, strlen(data)) < 0 ||
      smtp_read_reply(transport, reply, sizeof(reply)) != 250)
  {
    fprintf(stderr, "Unexpected SMTP reply: %s", reply);
    free(data);
    return -1;
  }

  free(data);


  command = smtp_command("QUIT", NULL);

  if (command == NULL || smtp_send_command(transport, command, 221, reply,
                                           sizeof(reply)) < 0)
  {
    fprintf(stderr, "Unexpected SMTP reply: %s", reply);
    free(command);
    return -1;
  }

  free(command);

  return 0;
}