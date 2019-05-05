// statsd-c-client
// Copyright (c) 2012 Roman Shterenzon
// https://github.com/romanbsd/statsd-c-client
// Released under the MIT license.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>
#include "statsd-client.h"

#define MAX_MSG_LEN 100



statsd_link *statsd_init_with_namespace(const char *host, int port, const char *ns_)
{
    if (!host || !port || !ns_)
        return NULL;

    size_t len = strlen(ns_);

    statsd_link *temp = statsd_init(host, port);
    if(!temp)
        return NULL;

    if ( (temp->ns = malloc(len + 2)) == NULL ) {
        perror("malloc");
        return NULL;
    }
    strcpy(temp->ns, ns_);
    temp->ns[len++] = '.';
    temp->ns[len] = 0;

    return temp;
}

statsd_link *statsd_init(const char *host, int port)
{
    if (!host || !port)
        return NULL;
    
    statsd_link *temp = calloc(1, sizeof(statsd_link));
    if (!temp) {
        fprintf(stderr, "calloc() failed");
        goto err;
    }

    if ((temp->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        perror("socket");
        goto err;
    }

    memset(&temp->server, 0, sizeof(temp->server));
    temp->server.sin_family = AF_INET;
    temp->server.sin_port = htons(port);

    struct addrinfo *result = NULL, hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    int error;
    if ( (error = getaddrinfo(host, NULL, &hints, &result)) ) {
        fprintf(stderr, "%s\n", gai_strerror(error));
        goto err;
    }
    memcpy(&(temp->server.sin_addr), &((struct sockaddr_in*)result->ai_addr)->sin_addr, sizeof(struct in_addr));
    freeaddrinfo(result);

    srandom(time(NULL));

    return temp;

err:
    if (temp) {
        free(temp);
    }

    return NULL;
}

void statsd_finalize(statsd_link *link)
{
    if (!link) return;

    // close socket
    if (link->sock != -1) {
        close(link->sock);
        link->sock = -1;
    }

    // freeing ns
    if (link->ns) {
        free(link->ns);
        link->ns = NULL;
    }

    // free whole link
    free(link);
}

/* will change the original string */
static void cleanup(char *stat)
{
    char *p;
    for (p = stat; *p; p++) {
        if (*p == ':' || *p == '|' || *p == '@') {
            *p = '_';
        }
    }
}

static int should_send(float sample_rate)
{
    if (sample_rate < 1.0) {
        float p = ((float)random() / RAND_MAX);
        return sample_rate > p;
    } else {
        return 1;
    }
}

int statsd_send(statsd_link *link, const char *message)
{
    if (!link) return -2;
    int slen = sizeof(link->server);

    if (sendto(link->sock, message, strlen(message), 0, (struct sockaddr *) &link->server, slen) == -1) {
        perror("sendto");
        return -1;
    }
    return 0;
}

static int send_stat(statsd_link *link, char *stat, size_t value, const char *type, float sample_rate)
{
    char message[MAX_MSG_LEN];
    if (!should_send(sample_rate)) {
        return 0;
    }

    statsd_prepare(link, stat, value, type, sample_rate, message, MAX_MSG_LEN, 0);

    return statsd_send(link, message);
}

void statsd_prepare(statsd_link *link, char *stat, size_t value, const char *type, float sample_rate, char *message, size_t buflen, int lf)
{
    if (!link) return;
    
    cleanup(stat);
    if (sample_rate == 1.0) {
        snprintf(message, buflen, "%s%s:%zd|%s%s", link->ns ? link->ns : "", stat, value, type, lf ? "\n" : "");
    } else {
        snprintf(message, buflen, "%s%s:%zd|%s|@%.5f%s", link->ns ? link->ns : "", stat, value, type, sample_rate, lf ? "\n" : "");
    }
}

/* public interface */
int statsd_count(statsd_link *link, char *stat, size_t value, float sample_rate)
{
    return send_stat(link, stat, value, "c", sample_rate);
}

int statsd_dec(statsd_link *link, char *stat, float sample_rate)
{
    return statsd_count(link, stat, -1, sample_rate);
}

int statsd_inc(statsd_link *link, char *stat, float sample_rate)
{
    return statsd_count(link, stat, 1, sample_rate);
}

int statsd_gauge(statsd_link *link, char *stat, size_t value)
{
    return send_stat(link, stat, value, "g", 1.0);
}

int statsd_timing(statsd_link *link, char *stat, size_t ms)
{
    return statsd_timing_with_sample_rate(link, stat, ms, 1.0);
}

int statsd_timing_with_sample_rate(statsd_link *link, char *stat, size_t ms, float sample_rate)
{
    return send_stat(link, stat, ms, "ms", sample_rate);
}
