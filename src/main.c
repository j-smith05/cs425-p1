#define _POSIX_C_SOURCE 200809L

#include "lab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#ifdef TEST
#define main main_exclude
#endif


static void print_usage(const char *program)
{
    printf("Usage: %s -f <from> -t <to> [-s subject] [-b body] [-p port]\n"
           "          [-H helo-host] <server>\n\n"
           "  -f <from>       envelope sender, for example you@example.com\n"
           "  -t <to>         envelope recipient\n"
           "  -s <subject>    subject line (default: empty)\n"
           "  -b <body>       message body (default: empty)\n"
           "  -p <port>       port or service name (default: 25)\n"
           "  -H <helo-host>  host name sent with HELO (default: localhost)\n"
           "  <server>        host name or address of the mail server\n",
           program);
}


int main(int argc, char *argv[])
{
    const char *from = NULL;
    const char *to = NULL;
    const char *subject = "";
    const char *body = "";
    const char *port = "25";
    const char *helo_host = "localhost";
    const char *server = NULL;

    int opt;


    if (argc == 1)
    {
        print_usage(argv[0]);
        return 0;
    }


    opterr = 0;

    while ((opt = getopt(argc, argv, ":f:t:s:b:p:H:")) != -1)
    {
        switch (opt)
        {
        case 'f':
            from = optarg;
            break;

        case 't':
            to = optarg;
            break;

        case 's':
            subject = optarg;
            break;

        case 'b':
            body = optarg;
            break;

        case 'p':
            port = optarg;
            break;

        case 'H':
            helo_host = optarg;
            break;

        case ':':
            fprintf(stderr, "Option -%c requires an argument.\n", optopt);
            print_usage(argv[0]);
            return 1;

        case '?':
            fprintf(stderr, "Unknown option: -%c.\n", optopt);
            print_usage(argv[0]);
            return 1;

        default:
            return 1;
        }
    }

    if (optind != argc - 1 || from == NULL || to == NULL)
    {
        print_usage(argv[0]);
        return 1;
    }

    server = argv[optind];

    smtp_transport transport = {0};
    int result;

    if (smtp_connect(server, port, &transport) < 0)
    {
        fprintf(stderr, "Could not connect to %s:%s.\n", server, port);
        return 2;
    }

    result = smtp_session(&transport, from, to, helo_host, subject, body);

    smtp_close(&transport);

    if (result < 0)
    {
        fprintf(stderr, "SMTP session failed.\n");
        return 2;
    }

    return 0;
}
