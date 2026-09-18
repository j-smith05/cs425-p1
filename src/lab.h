#ifndef LAB_H
#define LAB_H

#include <stddef.h>

typedef int (*smtp_read_fn)(void *context, char *buffer, size_t size);
typedef int (*smtp_write_fn)(void *context, const char *buffer, size_t size);

typedef struct
{
    smtp_read_fn read;
    smtp_write_fn write;
    void *context;

    char input[4096];
    size_t input_start;
    size_t input_end;
} smtp_transport;

char *get_greeting(const char *restrict name);

int smtp_reply_code(const char *line);

char *smtp_command(const char *verb, const char *argument);

char *smtp_dot_stuff(const char *body);

char *smtp_data(const char *from, const char *to, const char *subject,
                const char *body);

int smtp_read_line(smtp_transport *transport, char *line, size_t size);

int smtp_read_reply(smtp_transport *transport, char *reply, size_t size);

int smtp_write_all(smtp_transport *transport, const char *data, size_t size);

int smtp_send_command(smtp_transport *transport, const char *command,
                      int expected_code, char *reply, size_t reply_size);

int smtp_session(smtp_transport *transport, const char *from, const char *to,
                 const char *helo_host, const char *subject,
                 const char *body);


int smtp_connect(const char *server, const char *port,
                 smtp_transport *transport);

int smtp_close(smtp_transport *transport);

int smtp_socket_read(void *context, char *buffer, size_t size);

int smtp_socket_write(void *context, const char *buffer, size_t size);

#endif // LAB_H