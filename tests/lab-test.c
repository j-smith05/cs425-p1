#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/wait.h>

#include "harness/unity.h"
#include "../src/lab.h"


typedef struct
{
  const char *input;
  size_t input_position;
  size_t read_limit;

  char output[32768];
  size_t output_length;
  size_t write_limit;

  int fail_read;
  int fail_write;
} fake_server;


int fake_read(void *context, char *buffer, size_t size)
{
  fake_server *server = context;
  size_t remaining = strlen(server->input) - server->input_position;
  size_t amount = remaining < size ? remaining : size;

  if (server->read_limit != 0 && amount > server->read_limit)
    amount = server->read_limit;

  if (server->fail_read || amount == 0)
    return server->fail_read ? -1 : 0;

  memcpy(buffer, server->input + server->input_position, amount);
  server->input_position += amount;

  return (int)amount;
}


int fake_write(void *context, const char *buffer, size_t size)
{
  fake_server *server = context;
  size_t amount = size;

  if (server->fail_write)
    return -1;

  if (server->write_limit != 0 && amount > server->write_limit)
    amount = server->write_limit;

  memcpy(server->output + server->output_length, buffer, amount);
  server->output_length += amount;

  return (int)amount;
}


smtp_transport fake_transport(fake_server *server)
{
  smtp_transport transport = {fake_read, fake_write, server, {0}, 0, 0};
  return transport;
}


void setUp(void)
{
}


void tearDown(void)
{
}


void test_pure_helpers(void)
{
  char *value;

  TEST_ASSERT_EQUAL_INT(250, smtp_reply_code("250 hello\r\n"));
  TEST_ASSERT_EQUAL_INT(-1, smtp_reply_code("bad\r\n"));

  value = smtp_command("HELO ", "localhost");
  TEST_ASSERT_EQUAL_STRING("HELO localhost\r\n", value);
  free(value);

  value = smtp_command("DATA", NULL);
  TEST_ASSERT_EQUAL_STRING("DATA\r\n", value);
  free(value);

  value = smtp_dot_stuff("one\n.two\n..three");
  TEST_ASSERT_EQUAL_STRING("one\n..two\n...three", value);
  free(value);

  value = smtp_data("from@example.com", "to@example.com", "subject", "body");
  TEST_ASSERT_EQUAL_STRING("From: from@example.com\r\nTo: to@example.com\r\n"
                           "Subject: subject\r\n\r\nbody\r\n.\r\n",
                           value);
  free(value);

  value = smtp_data("from", "to", "", "");
  TEST_ASSERT_NOT_NULL(value);
  TEST_ASSERT_NOT_NULL(strstr(value, "Subject: \r\n\r\n.\r\n"));
  free(value);
}


void test_reply_code_errors(void)
{
  TEST_ASSERT_EQUAL_INT(-1, smtp_reply_code(NULL));
  TEST_ASSERT_EQUAL_INT(-1, smtp_reply_code("2x0 bad\r\n"));
  TEST_ASSERT_EQUAL_INT(-1, smtp_reply_code("25x bad\r\n"));
}


void test_dot_stuff_edges(void)
{
  char *value;

  value = smtp_dot_stuff(".first\nsecond\n.third");
  TEST_ASSERT_EQUAL_STRING("..first\nsecond\n..third", value);
  free(value);

  value = smtp_dot_stuff("normal");
  TEST_ASSERT_EQUAL_STRING("normal", value);
  free(value);
}


void test_read_line_and_reply(void)
{
  fake_server server = {"250-first\r\n250 final\r\n", 0, 2, {0}, 0, 0, 0, 0};
  smtp_transport transport = fake_transport(&server);
  char line[64];
  char reply[128];

  TEST_ASSERT_EQUAL_INT(-1, smtp_read_line(&transport, line, 0));
  TEST_ASSERT_EQUAL_INT(11, smtp_read_line(&transport, line, sizeof(line)));
  TEST_ASSERT_EQUAL_STRING("250-first\r\n", line);

  server.input_position = 0;
  transport = fake_transport(&server);

  TEST_ASSERT_EQUAL_INT(250, smtp_read_reply(&transport, reply, sizeof(reply)));
  TEST_ASSERT_EQUAL_STRING("250-first\r\n250 final\r\n", reply);
}


void test_read_errors(void)
{
  fake_server server = {"250 okay\r\n", 0, 0, {0}, 0, 0, 0, 0};
  smtp_transport transport = fake_transport(&server);
  char line[4];
  char reply[4];

  TEST_ASSERT_EQUAL_INT(-1, smtp_read_line(&transport, line, sizeof(line)));

  server.input = "250 okay\r\n";
  server.input_position = 0;
  TEST_ASSERT_EQUAL_INT(-1, smtp_read_reply(&transport, reply, sizeof(reply)));

  server.input = "x\r\n";
  server.input_position = 0;
  TEST_ASSERT_EQUAL_INT(-1, smtp_read_reply(&transport, reply, sizeof(reply)));

  server.fail_read = 1;
  TEST_ASSERT_EQUAL_INT(-1, smtp_read_line(&transport, line, sizeof(line)));
}


void test_write_and_command_errors(void)
{
  fake_server server = {"250 okay\r\n", 0, 0, {0}, 0, 2, 0, 0};
  smtp_transport transport = fake_transport(&server);
  char reply[64];

  TEST_ASSERT_EQUAL_INT(0, smtp_write_all(&transport, "hello", 5));
  TEST_ASSERT_EQUAL_STRING("hello", server.output);

  TEST_ASSERT_EQUAL_INT(0, smtp_send_command(&transport, "NOOP\r\n", 250,
                                             reply, sizeof(reply)));

  server.fail_write = 1;

  TEST_ASSERT_EQUAL_INT(-1, smtp_write_all(&transport, "x", 1));
  TEST_ASSERT_EQUAL_INT(-1, smtp_send_command(&transport, "x", 250,
                                              reply, sizeof(reply)));

  server.fail_write = 0;
  server.write_limit = 0;
  server.input = "";
  server.input_position = 0;

  TEST_ASSERT_EQUAL_INT(-1, smtp_send_command(&transport, "x", 250,
                                              reply, sizeof(reply)));
}


void test_session_success(void)
{
  fake_server server = {
      "220 ready\r\n250 hello\r\n250 from\r\n250 to\r\n"
      "354 data\r\n250 queued\r\n221 bye\r\n",
      0,
      1,
      {0},
      0,
      3,
      0,
      0
  };

  smtp_transport transport = fake_transport(&server);

  TEST_ASSERT_EQUAL_INT(0, smtp_session(&transport, "from@example.com",
                                        "to@example.com", "localhost", "hi",
                                        ".line"));

  TEST_ASSERT_NOT_NULL(strstr(server.output, "HELO localhost\r\n"));
  TEST_ASSERT_NOT_NULL(strstr(server.output, "..line"));
  TEST_ASSERT_NOT_NULL(strstr(server.output, "QUIT\r\n"));
}


void test_session_wrong_statuses(void)
{
  const char *responses[] = {
      "500 no\r\n",
      "220 ready\r\n500 no\r\n",
      "220 ready\r\n250 yes\r\n500 no\r\n",
      "220 ready\r\n250 yes\r\n250 yes\r\n500 no\r\n",
      "220 ready\r\n250 yes\r\n250 yes\r\n250 yes\r\n354 data\r\n500 no\r\n",
      "220 ready\r\n250 yes\r\n250 yes\r\n250 yes\r\n500 no\r\n",
      "220 ready\r\n250 yes\r\n250 yes\r\n250 yes\r\n354 data\r\n250 yes\r\n500 no\r\n"
  };

  for (size_t index = 0;
       index < sizeof(responses) / sizeof(responses[0]);
       index++)
  {
    fake_server server = {responses[index], 0, 0, {0}, 0, 0, 0, 0};
    smtp_transport transport = fake_transport(&server);

    TEST_ASSERT_EQUAL_INT(-1,
                          smtp_session(&transport, "a", "b", "c", "d", "e"));
  }
}


void test_session_hangup(void)
{
  fake_server server = {"220 ready\r\n250 yes\r\n", 0, 0, {0}, 0, 0, 0, 0};
  smtp_transport transport = fake_transport(&server);

  TEST_ASSERT_EQUAL_INT(-1,
                        smtp_session(&transport, "a", "b", "c", "d", "e"));
}


void test_socket_functions(void)
{
  int sockets[2];
  char buffer[8];

  TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));

  TEST_ASSERT_EQUAL_INT(3, (int)write(sockets[1], "abc", 3));
  TEST_ASSERT_EQUAL_INT(3,
                        smtp_socket_read(&sockets[0], buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_MEMORY("abc", buffer, 3);

  TEST_ASSERT_EQUAL_INT(3, smtp_socket_write(&sockets[0], "xyz", 3));
  TEST_ASSERT_EQUAL_INT(3, (int)read(sockets[1], buffer, sizeof(buffer)));

  close(sockets[0]);
  close(sockets[1]);

  TEST_ASSERT_EQUAL_INT(
      -1, smtp_connect("invalid.invalid", "25", &(smtp_transport){0}));

  TEST_ASSERT_EQUAL_INT(
      -1, smtp_connect("127.0.0.1", "1", &(smtp_transport){0}));
}


void test_socket_connect(void)
{
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in address = {0};
  socklen_t address_length = sizeof(address);
  smtp_transport transport = {0};
  char port[16];

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;

  TEST_ASSERT_TRUE(listener >= 0);

  TEST_ASSERT_EQUAL_INT(
      0, bind(listener, (struct sockaddr *)&address, sizeof(address)));

  TEST_ASSERT_EQUAL_INT(0, listen(listener, 1));

  TEST_ASSERT_EQUAL_INT(
      0, getsockname(listener, (struct sockaddr *)&address, &address_length));

  (void)snprintf(port, sizeof(port), "%u",
                 (unsigned)ntohs(address.sin_port));

  TEST_ASSERT_EQUAL_INT(0, smtp_connect("127.0.0.1", port, &transport));
  TEST_ASSERT_EQUAL_INT(0, smtp_close(&transport));

  close(listener);
}


void test_close(void)
{
  int sockets[2];
  int *fd = malloc(sizeof(*fd));
  smtp_transport transport = {0};

  TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));

  *fd = sockets[0];
  transport.context = fd;

  TEST_ASSERT_EQUAL_INT(0, smtp_close(&transport));
  TEST_ASSERT_NULL(transport.context);
  TEST_ASSERT_EQUAL_INT(0, smtp_close(NULL));

  close(sockets[1]);
}


int main(void)
{
  UNITY_BEGIN();

  RUN_TEST(test_pure_helpers);
  RUN_TEST(test_reply_code_errors);
  RUN_TEST(test_dot_stuff_edges);
  RUN_TEST(test_read_line_and_reply);
  RUN_TEST(test_read_errors);
  RUN_TEST(test_write_and_command_errors);
  RUN_TEST(test_session_success);
  RUN_TEST(test_session_wrong_statuses);
  RUN_TEST(test_session_hangup);
  RUN_TEST(test_socket_functions);
  RUN_TEST(test_socket_connect);
  RUN_TEST(test_close);

  return UNITY_END();
}