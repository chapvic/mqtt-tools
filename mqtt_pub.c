/*
 * MQTT Publisher, v1.0 (License: GPLv3)
 * (c) 2026, Chapvic
 *
 * A multi-threaded MQTT publisher that reads metrics from an INI config file
 * and publishes them to a local MQTT broker at specified intervals.
 *
 * Supports remote metrics via $-source: subscribes to a remote broker topic
 * and republishes received values to the local broker.
 *
 * Build: cc -O2 -Wall -o mqtt_pub mqtt_pub.c -lmosquitto -lpthread
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <getopt.h>
#include <regex.h>
#include <sys/wait.h>
#include <mosquitto.h>

/* Fixed header - do not change */
#define APP_NAME    "MQTT Publisher"
#define APP_VERSION "v1.0"
#define APP_LICENSE "License: GPLv3"
#define APP_AUTHOR  "(c) 2026, Chapvic"

/* Buffer limits */
#define MAX_VALUE       256
#define MAX_SHELL_CMD   2048
#define MAX_SECTION     64
#define MAX_TOPIC       512
#define MAX_LINE        4096
#define MAX_METRICS     128
#define MAX_HOST        256
#define MAX_USER        64
#define MAX_PASS        256
#define MAX_KEY         64
#define MAX_RAW         8192
#define MAX_REMOTE_TOPIC 512
#define MAX_REMOTES     32
#define DEFAULT_CFG     "mqtt_pub.ini"
#define DEFAULT_HOST    "localhost"
#define DEFAULT_PORT    1883

/* Metric structure */
typedef struct {
    char    section[MAX_SECTION];
    char    topic[MAX_TOPIC];
    int     interval;
    int     qos;
    char    source[MAX_SHELL_CMD];
    char    key[MAX_KEY];
    bool    is_remote;
    int     remote_idx;
    char    remote_topic[MAX_REMOTE_TOPIC];
    char    last_value[MAX_VALUE];
    bool    has_last_value;
    pthread_t thread;
    bool    thread_started;
    pthread_mutex_t value_mutex;
} metric_t;

/* Remote broker connection */
typedef struct {
    char    host[MAX_HOST];
    int     port;
    char    user[MAX_USER];
    char    pass[MAX_PASS];
    bool    has_auth;
    struct mosquitto *mosq;
    bool    connected;
    bool    logged_down;
    bool    first_connect_done;
} remote_broker_t;

/* Global state */
static volatile sig_atomic_t g_running = 1;
static volatile sig_atomic_t g_mqtt_connected = 0;
static volatile sig_atomic_t g_mqtt_connect_rc = 0;
static struct mosquitto *g_mosq = NULL;
static pthread_mutex_t   g_mqtt_mutex = PTHREAD_MUTEX_INITIALIZER;

/* MQTT config */
static struct {
    char    host[MAX_HOST];
    int     port;
    char    user[MAX_USER];
    char    password[MAX_PASS];
    int     debug;
    bool    has_auth;
} g_mqtt;

/* Metrics array */
static metric_t g_metrics[MAX_METRICS];
static int     g_metric_count = 0;

/* Remote brokers */
static remote_broker_t g_remotes[MAX_REMOTES];
static int g_remote_count = 0;

/* Forward declarations */
static bool extract_key_value(const char *buf, const char *key,
                              char *out, size_t outsize);
static bool extract_regex_value(const char *buf, const char *pattern,
                                char *out, size_t outsize);
static bool read_source(const char *source, const char *key,
                        char *buf, size_t bufsize);

/* Signal handler */
static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* Trim whitespace in-place */
static void str_trim(char *s) {
    char *start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
        start++;
    if (start != s)
        memmove(s, start, strlen(start) + 1);
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                       s[len - 1] == '\r' || s[len - 1] == '\n'))
        s[--len] = '\0';
}

/* Safe bounded copy */
static void safe_copy(char *dst, const char *src, size_t dstsize) {
    size_t len = strlen(src);
    if (len >= dstsize)
        len = dstsize - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* Local mosquitto callbacks */
static void on_connect(struct mosquitto *mosq, void *obj, int rc) {
    (void)mosq; (void)obj;
    if (rc == MOSQ_ERR_SUCCESS) {
        g_mqtt_connected = 1;
        if (g_mqtt.debug)
            printf("[mqtt] connected to broker\n");
    } else {
        g_mqtt_connect_rc = rc;
        g_mqtt_connected = -1;
    }
}

static void on_disconnect(struct mosquitto *mosq, void *obj, int rc) {
    (void)mosq; (void)obj;
    bool was_connected = (g_mqtt_connected == 1);
    g_mqtt_connected = 0;
    if (was_connected && g_running) {
        if (rc == MOSQ_ERR_SUCCESS) {
            if (g_mqtt.debug)
                printf("[mqtt] disconnected from broker\n");
        } else {
            fprintf(stderr, "[mqtt] unexpected disconnect: %s\n",
                    mosquitto_strerror(rc));
        }
    }
}

/* Remote mosquitto callbacks */
static void remote_on_connect(struct mosquitto *mosq, void *obj, int rc) {
    remote_broker_t *rb = (remote_broker_t *)obj;
    if (rc == MOSQ_ERR_SUCCESS) {
        rb->connected = true;
        if (!rb->first_connect_done) {
            printf("[remote] %s:%d connected\n", rb->host, rb->port);
            rb->first_connect_done = true;
        } else if (rb->logged_down) {
            printf("[remote] %s:%d back online\n", rb->host, rb->port);
            rb->logged_down = false;
        }
        /* Re-subscribe to all metrics on this broker */
        for (int i = 0; i < g_metric_count; i++) {
            if (g_metrics[i].is_remote && g_metrics[i].remote_idx >= 0 &&
                &g_remotes[g_metrics[i].remote_idx] == rb) {
                int sub_rc = mosquitto_subscribe(mosq, NULL,
                                                 g_metrics[i].remote_topic, 0);
                if (g_mqtt.debug)
                    printf("[remote] subscribe %s -> %s (rc=%d)\n",
                           rb->host, g_metrics[i].remote_topic, sub_rc);
            }
        }
    } else {
        rb->connected = false;
        if (!rb->logged_down) {
            fprintf(stderr, "[remote] %s:%d unavailable: %s\n",
                    rb->host, rb->port, mosquitto_strerror(rc));
            rb->logged_down = true;
        }
    }
}

static void remote_on_disconnect(struct mosquitto *mosq, void *obj, int rc) {
    (void)mosq;
    remote_broker_t *rb = (remote_broker_t *)obj;
    rb->connected = false;
    if (g_running && !rb->logged_down) {
        fprintf(stderr, "[remote] %s:%d disconnected: %s\n",
                rb->host, rb->port, mosquitto_strerror(rc));
        rb->logged_down = true;
    }
}

static void remote_on_message(struct mosquitto *mosq, void *obj,
                              const struct mosquitto_message *msg) {
    (void)mosq; (void)obj;

    for (int i = 0; i < g_metric_count; i++) {
        if (!g_metrics[i].is_remote)
            continue;
        if (strcmp(g_metrics[i].remote_topic, msg->topic) != 0)
            continue;

        char value[MAX_VALUE];
        char raw[MAX_RAW];

        if (msg->payloadlen > 0) {
            size_t plen = (size_t)msg->payloadlen;
            if (plen >= sizeof(raw))
                plen = sizeof(raw) - 1;
            memcpy(raw, msg->payload, plen);
            raw[plen] = '\0';
        } else {
            raw[0] = '\0';
        }

        bool got_value = false;

        if (g_metrics[i].key[0] != '\0' &&
            strcmp(g_metrics[i].key, "-") != 0) {
            size_t klen = strlen(g_metrics[i].key);
            if (g_metrics[i].key[0] == '/' && klen > 2 &&
                g_metrics[i].key[klen - 1] == '/') {
                char pattern[MAX_KEY];
                size_t plen2 = klen - 2;
                if (plen2 >= sizeof(pattern))
                    plen2 = sizeof(pattern) - 1;
                memcpy(pattern, g_metrics[i].key + 1, plen2);
                pattern[plen2] = '\0';
                got_value = extract_regex_value(raw, pattern,
                                                 value, sizeof(value));
            } else {
                got_value = extract_key_value(raw, g_metrics[i].key,
                                              value, sizeof(value));
            }
        } else {
            safe_copy(value, raw, sizeof(value));
            str_trim(value);
            got_value = true;
        }

        if (!got_value) {
            if (g_mqtt.debug)
                fprintf(stderr, "[remote] key not found in message for [%s]\n",
                        g_metrics[i].section);
            continue;
        }

        pthread_mutex_lock(&g_metrics[i].value_mutex);
        safe_copy(g_metrics[i].last_value, value, sizeof(g_metrics[i].last_value));
        g_metrics[i].has_last_value = true;
        pthread_mutex_unlock(&g_metrics[i].value_mutex);

        pthread_mutex_lock(&g_mqtt_mutex);
        if (g_mosq && g_mqtt_connected == 1) {
            int pub_rc = mosquitto_publish(g_mosq, NULL,
                                           g_metrics[i].topic,
                                           (int)strlen(value), value,
                                           g_metrics[i].qos, false);
            if (g_mqtt.debug) {
                if (pub_rc == MOSQ_ERR_SUCCESS)
                    printf("[pub] %s = %s (from remote)\n",
                           g_metrics[i].topic, value);
                else
                    fprintf(stderr, "[err] publish to %s failed: %s\n",
                            g_metrics[i].topic, mosquitto_strerror(pub_rc));
            }
        }
        pthread_mutex_unlock(&g_mqtt_mutex);

        break;
    }
}

/* Extract value by literal key */
static bool extract_key_value(const char *buf, const char *key,
                              char *out, size_t outsize) {
    size_t keylen = strlen(key);
    const char *p = buf;
    while (*p) {
        if (strncmp(p, key, keylen) == 0 && p[keylen] == '=') {
            const char *vstart = p + keylen + 1;
            const char *vend = strchr(vstart, '\n');
            if (!vend)
                vend = vstart + strlen(vstart);
            size_t vlen = (size_t)(vend - vstart);
            if (vlen >= outsize)
                vlen = outsize - 1;
            memcpy(out, vstart, vlen);
            out[vlen] = '\0';
            str_trim(out);
            return true;
        }
        const char *nl = strchr(p, '\n');
        if (!nl)
            break;
        p = nl + 1;
    }
    return false;
}

/* Extract value using POSIX ERE regex */
static bool extract_regex_value(const char *buf, const char *pattern,
                                char *out, size_t outsize) {
    regex_t re;
    int rc = regcomp(&re, pattern, REG_EXTENDED | REG_NEWLINE);
    if (rc != 0) {
        char errbuf[128];
        regerror(rc, &re, errbuf, sizeof(errbuf));
        fprintf(stderr, "[err] invalid regex '%s': %s\n", pattern, errbuf);
        regfree(&re);
        return false;
    }

    regmatch_t matches[2];
    rc = regexec(&re, buf, 2, matches, 0);
    regfree(&re);

    if (rc != 0)
        return false;

    regmatch_t *m = (matches[1].rm_so >= 0) ? &matches[1] : &matches[0];
    size_t len = (size_t)(m->rm_eo - m->rm_so);
    if (len >= outsize)
        len = outsize - 1;
    memcpy(out, buf + m->rm_so, len);
    out[len] = '\0';
    str_trim(out);
    return true;
}

/* Read a value from source (file or shell command) */
static bool read_source(const char *source, const char *key,
                        char *buf, size_t bufsize) {
    char raw[MAX_RAW];
    size_t n;

    if (source[0] == '@') {
        FILE *p = popen(source + 1, "r");
        if (!p) {
            fprintf(stderr, "[err] popen failed for source '%s': %s\n",
                    source, strerror(errno));
            return false;
        }
        n = fread(raw, 1, sizeof(raw) - 1, p);
        if (ferror(p)) {
            fprintf(stderr, "[err] fread failed for source '%s': %s\n",
                    source, strerror(errno));
            pclose(p);
            return false;
        }
        raw[n] = '\0';
        int pstatus = pclose(p);
        if (pstatus == -1) {
            fprintf(stderr, "[err] pclose failed for source '%s': %s\n",
                    source, strerror(errno));
            return false;
        }
        if (pstatus != 0) {
            if (g_mqtt.debug)
                fprintf(stderr, "[warn] command '%s' exited with status %d\n",
                        source + 1, WEXITSTATUS(pstatus));
        }
    } else {
        int fd = open(source, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "[err] open failed for source '%s': %s\n",
                    source, strerror(errno));
            return false;
        }
        n = (size_t)read(fd, raw, sizeof(raw) - 1);
        close(fd);
        if ((ssize_t)n < 0) {
            fprintf(stderr, "[err] read failed for source '%s': %s\n",
                    source, strerror(errno));
            return false;
        }
        raw[n] = '\0';
    }

    if (key[0] != '\0' && strcmp(key, "-") != 0) {
        size_t klen = strlen(key);
        if (key[0] == '/' && klen > 2 && key[klen - 1] == '/') {
            char pattern[MAX_KEY];
            size_t plen = klen - 2;
            if (plen >= sizeof(pattern))
                plen = sizeof(pattern) - 1;
            memcpy(pattern, key + 1, plen);
            pattern[plen] = '\0';
            if (!extract_regex_value(raw, pattern, buf, bufsize)) {
                if (g_mqtt.debug)
                    fprintf(stderr, "[err] regex '%s' not found in '%s'\n",
                            pattern, source);
                return false;
            }
        } else {
            if (!extract_key_value(raw, key, buf, bufsize)) {
                if (g_mqtt.debug)
                    fprintf(stderr, "[err] key '%s' not found in '%s'\n",
                        key, source);
                return false;
            }
        }
    } else {
        safe_copy(buf, raw, bufsize);
        str_trim(buf);
    }

    return true;
}

/* Metric thread: publish local metrics at given interval */
static void *metric_thread(void *arg) {
    metric_t *m = (metric_t *)arg;
    char value[MAX_VALUE];

    while (g_running) {
        if (g_mqtt_connected == 1) {
            if (!read_source(m->source, m->key, value, sizeof(value))) {
                if (g_mqtt.debug)
                    fprintf(stderr, "[err] failed to read source for [%s]\n",
                            m->section);
            } else {
                pthread_mutex_lock(&g_mqtt_mutex);
                if (g_mosq && g_mqtt_connected == 1) {
                    int rc = mosquitto_publish(g_mosq, NULL, m->topic,
                                               (int)strlen(value), value,
                                               m->qos, false);
                    if (g_mqtt.debug) {
                        if (rc == MOSQ_ERR_SUCCESS)
                            printf("[pub] %s = %s\n", m->topic, value);
                        else
                            fprintf(stderr, "[err] publish to %s failed: %s\n",
                                    m->topic, mosquitto_strerror(rc));
                    }
                }
                pthread_mutex_unlock(&g_mqtt_mutex);
            }
        }
        for (int i = 0; i < m->interval * 10 && g_running; i++)
            usleep(100000);
    }
    return NULL;
}

/* Remote timer thread: republish last known value at interval */
static void *remote_timer_thread(void *arg) {
    metric_t *m = (metric_t *)arg;
    char value[MAX_VALUE];

    while (g_running) {
        pthread_mutex_lock(&m->value_mutex);
        bool have = m->has_last_value;
        if (have)
            safe_copy(value, m->last_value, sizeof(value));
        pthread_mutex_unlock(&m->value_mutex);

        if (have && g_mqtt_connected == 1) {
            pthread_mutex_lock(&g_mqtt_mutex);
            if (g_mosq && g_mqtt_connected == 1) {
                int rc = mosquitto_publish(g_mosq, NULL, m->topic,
                                           (int)strlen(value), value,
                                           m->qos, false);
                if (g_mqtt.debug) {
                    if (rc == MOSQ_ERR_SUCCESS)
                        printf("[pub] %s = %s (republish)\n", m->topic, value);
                    else
                        fprintf(stderr, "[err] republish to %s failed: %s\n",
                                m->topic, mosquitto_strerror(rc));
                }
            }
            pthread_mutex_unlock(&g_mqtt_mutex);
        }
        for (int i = 0; i < m->interval * 10 && g_running; i++)
            usleep(100000);
    }
    return NULL;
}

/* Parse $-source string: $[user:pass@]host:port/topic */
static bool parse_remote_source(const char *src,
                                char *user, size_t usize,
                                char *pass, size_t psize,
                                char *host, size_t hsize,
                                int *port,
                                char *topic, size_t tsize,
                                bool *has_auth) {
    const char *p = src + 1; /* skip $ */
    *has_auth = false;
    user[0] = '\0';
    pass[0] = '\0';
    host[0] = '\0';
    *port = DEFAULT_PORT;
    topic[0] = '\0';

    /* Find first / — separates host:port from topic */
    const char *slash = strchr(p, '/');
    if (!slash) {
        fprintf(stderr, "[err] no topic in remote source: %s\n", src);
        return false;
    }

    /* Check for auth: user:pass@ before host:port */
    const char *at = NULL;
    for (const char *c = p; c < slash; c++) {
        if (*c == '@') {
            at = c;
            break;
        }
    }

    const char *host_start = p;
    if (at) {
        *has_auth = true;
        const char *colon = NULL;
        for (const char *c = p; c < at; c++) {
            if (*c == ':') {
                colon = c;
                break;
            }
        }
        if (colon) {
            size_t ulen = (size_t)(colon - p);
            if (ulen >= usize) ulen = usize - 1;
            memcpy(user, p, ulen);
            user[ulen] = '\0';
            size_t plen = (size_t)(at - colon - 1);
            if (plen >= psize) plen = psize - 1;
            memcpy(pass, colon + 1, plen);
            pass[plen] = '\0';
        } else {
            size_t ulen = (size_t)(at - p);
            if (ulen >= usize) ulen = usize - 1;
            memcpy(user, p, ulen);
            user[ulen] = '\0';
            pass[0] = '\0';
        }
        host_start = at + 1;
    }

    /* Parse host:port */
    size_t hp_len = (size_t)(slash - host_start);
    if (hp_len == 0 || hp_len >= hsize) {
        fprintf(stderr, "[err] invalid host in remote source: %s\n", src);
        return false;
    }

    const char *colon = NULL;
    for (size_t i = 0; i < hp_len; i++) {
        if (host_start[i] == ':') {
            colon = &host_start[i];
            break;
        }
    }

    if (colon) {
        size_t hlen = (size_t)(colon - host_start);
        if (hlen == 0 || hlen >= hsize) {
            fprintf(stderr, "[err] invalid host in remote source: %s\n", src);
            return false;
        }
        memcpy(host, host_start, hlen);
        host[hlen] = '\0';
        *port = atoi(colon + 1);
        if (*port < 1 || *port > 65535) {
            fprintf(stderr, "[err] invalid port in remote source: %s\n", src);
            return false;
        }
    } else {
        size_t hlen = hp_len;
        if (hlen >= hsize) hlen = hsize - 1;
        memcpy(host, host_start, hlen);
        host[hlen] = '\0';
    }

    /* Basic host validation: non-empty, valid characters */
    if (host[0] == '\0') {
        fprintf(stderr, "[err] empty host in remote source: %s\n", src);
        return false;
    }
    for (const char *c = host; *c; c++) {
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
              (*c >= '0' && *c <= '9') || *c == '.' || *c == '-')) {
            fprintf(stderr, "[err] invalid character '%c' in host: %s\n",
                    *c, host);
            return false;
        }
    }

    /* Topic */
    const char *topic_start = slash + 1;
    size_t tlen = strlen(topic_start);
    if (tlen == 0) {
        fprintf(stderr, "[err] empty topic in remote source: %s\n", src);
        return false;
    }
    if (tlen >= tsize) tlen = tsize - 1;
    memcpy(topic, topic_start, tlen);
    topic[tlen] = '\0';

    return true;
}

/* Print help */
static void print_help(void) {
    printf("%s, %s\n", APP_NAME, APP_VERSION);
    printf("%s\n", APP_AUTHOR);
    printf("\n");
    printf("Usage: mqtt_pub [-f <config_file>] [-h]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -f <config_file>   # full path to INI config file\n");
    printf("  -h                # print this help and exit\n");
    printf("\n");
    printf("Config file structure (default: %s in current directory):\n", DEFAULT_CFG);
    printf("  [MQTT]                       # optional section (connection settings)\n");
    printf("    host=<host>                # default - localhost\n");
    printf("    port=<port>                # default - 1883\n");
    printf("    user=<user>                # username for auth\n");
    printf("    password=<password>        # password for auth\n");
    printf("    debug=<0|1>                # default - 0 (quiet), 1 = verbose\n");
    printf("\n");
    printf("  [metric_name]                # one section per metric\n");
    printf("    topic=<mqtt_topic>         # MQTT topic to publish to\n");
    printf("    interval=<seconds>         # publish interval (optional for $-source)\n");
    printf("    qos=<0|1|2>                # QoS level, default - 0\n");
    printf("    source=<file>              # read from file\n");
    printf("    source=@shell_command       # read from shell command output\n");
    printf("    source=$[user:pass@]host:port/topic  # subscribe to remote broker\n");
    printf("    key=<key_name>             # extract value by key (key=value format)\n");
    printf("    key=/regex/                # extract value using POSIX ERE regex\n");
    printf("    key=-                      # use entire payload as value\n");
    printf("\n");
    printf("Additional info:\n");
    printf("  If [MQTT] section is absent, connection is made without auth.\n");
    printf("  If source starts with '@', the rest is executed as a shell command.\n");
    printf("  If source starts with '$', program subscribes to a remote broker topic\n");
    printf("    and republishes received values to the local topic.\n");
    printf("  If key is set, the source is searched for key=value or regex match.\n");
    printf("  For $-source without interval, publishing is event-driven.\n");
    printf("  For $-source with interval, last value is republished if no new data.\n");
    printf("\n");
    printf("Rules:\n");
    printf("  [MQTT] must be the first section if present.\n");
    printf("  Section names must be unique, max length 64 bytes.\n");
    printf("  Topics must be unique, max length 512 bytes.\n");
    printf("  Shell commands max length 2048 bytes.\n");
    printf("  On config errors the program exits with an error message.\n");
    printf("\n%s\n", APP_LICENSE);
}

/* Parse a key=value line */
static bool parse_kv(const char *line, char *key, size_t keysize,
                     char *val, size_t valsize) {
    const char *eq = strchr(line, '=');
    if (!eq)
        return false;
    size_t klen = (size_t)(eq - line);
    if (klen >= keysize)
        return false;
    memcpy(key, line, klen);
    key[klen] = '\0';
    str_trim(key);
    safe_copy(val, eq + 1, valsize);
    str_trim(val);
    return true;
}

/* Check if topic exists */
static bool topic_exists(const char *topic) {
    for (int i = 0; i < g_metric_count; i++) {
        if (strcmp(g_metrics[i].topic, topic) == 0)
            return true;
    }
    return false;
}

/* Check if section name exists */
static bool section_exists(const char *name) {
    if (strcmp(name, "MQTT") == 0)
        return false;
    for (int i = 0; i < g_metric_count; i++) {
        if (strcmp(g_metrics[i].section, name) == 0)
            return true;
    }
    return false;
}

/* Load and parse config file */
static bool load_config(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: cannot open config file '%s': %s\n",
                filename, strerror(errno));
        return false;
    }

    safe_copy(g_mqtt.host, DEFAULT_HOST, sizeof(g_mqtt.host));
    g_mqtt.port = DEFAULT_PORT;
    g_mqtt.debug = 0;
    g_mqtt.has_auth = false;

    char line[MAX_LINE];
    char section[MAX_SECTION] = "";
    bool mqtt_seen = false;
    bool metrics_started = false;
    int  lineno = 0;

    while (fgets(line, sizeof(line), f)) {
        lineno++;
        str_trim(line);
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';')
            continue;

        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (!end) {
                fprintf(stderr, "Error: malformed section header at line %d\n", lineno);
                fclose(f);
                return false;
            }
            *end = '\0';
            char name[MAX_SECTION];
            safe_copy(name, line + 1, sizeof(name));
            str_trim(name);

            if (strcmp(name, "MQTT") == 0) {
                if (metrics_started) {
                    fprintf(stderr, "Error: [MQTT] must be the first section\n");
                    fclose(f);
                    return false;
                }
                if (mqtt_seen) {
                    fprintf(stderr, "Error: duplicate [MQTT] section\n");
                    fclose(f);
                    return false;
                }
                mqtt_seen = true;
                safe_copy(section, "MQTT", sizeof(section));
                continue;
            }

            if (section_exists(name)) {
                fprintf(stderr, "Error: duplicate section [%s]\n", name);
                fclose(f);
                return false;
            }
            if (g_metric_count >= MAX_METRICS) {
                fprintf(stderr, "Error: too many metric sections (max %d)\n",
                        MAX_METRICS);
                fclose(f);
                return false;
            }
            metrics_started = true;
            memset(&g_metrics[g_metric_count], 0, sizeof(metric_t));
            safe_copy(g_metrics[g_metric_count].section, name,
                     sizeof(g_metrics[0].section));
            g_metrics[g_metric_count].interval = 0;
            g_metrics[g_metric_count].qos = 0;
            g_metrics[g_metric_count].is_remote = false;
            g_metrics[g_metric_count].remote_idx = -1;
            g_metrics[g_metric_count].remote_topic[0] = '\0';
            g_metrics[g_metric_count].last_value[0] = '\0';
            g_metrics[g_metric_count].has_last_value = false;
            g_metrics[g_metric_count].thread_started = false;
            pthread_mutex_init(&g_metrics[g_metric_count].value_mutex, NULL);
            safe_copy(section, name, sizeof(section));
            g_metric_count++;
            continue;
        }

        char key[MAX_LINE], val[MAX_LINE];
        if (!parse_kv(line, key, sizeof(key), val, sizeof(val))) {
            fprintf(stderr, "Error: malformed line %d: %s\n", lineno, line);
            fclose(f);
            return false;
        }

        if (strcmp(section, "MQTT") == 0) {
            if (strcmp(key, "host") == 0) {
                safe_copy(g_mqtt.host, val, sizeof(g_mqtt.host));
            } else if (strcmp(key, "port") == 0) {
                g_mqtt.port = atoi(val);
                if (g_mqtt.port < 1 || g_mqtt.port > 65535) {
                    fprintf(stderr, "Error: invalid port '%s'\n", val);
                    fclose(f);
                    return false;
                }
            } else if (strcmp(key, "user") == 0) {
                safe_copy(g_mqtt.user, val, sizeof(g_mqtt.user));
                g_mqtt.has_auth = true;
            } else if (strcmp(key, "password") == 0) {
                safe_copy(g_mqtt.password, val, sizeof(g_mqtt.password));
                g_mqtt.has_auth = true;
            } else if (strcmp(key, "debug") == 0) {
                g_mqtt.debug = atoi(val);
            }
        } else if (section[0] != '\0' && g_metric_count > 0) {
            metric_t *m = &g_metrics[g_metric_count - 1];
            if (strcmp(key, "topic") == 0) {
                if (strlen(val) >= MAX_TOPIC) {
                    fprintf(stderr, "Error: topic exceeds %d bytes\n", MAX_TOPIC);
                    fclose(f);
                    return false;
                }
                if (topic_exists(val)) {
                    fprintf(stderr, "Error: duplicate topic '%s'\n", val);
                    fclose(f);
                    return false;
                }
                safe_copy(m->topic, val, sizeof(m->topic));
            } else if (strcmp(key, "interval") == 0) {
                m->interval = atoi(val);
            } else if (strcmp(key, "qos") == 0) {
                m->qos = atoi(val);
                if (m->qos < 0 || m->qos > 2) {
                    fprintf(stderr, "Error: invalid qos '%s' (must be 0-2)\n", val);
                    fclose(f);
                    return false;
                }
            } else if (strcmp(key, "source") == 0) {
                if (strlen(val) >= MAX_SHELL_CMD) {
                    fprintf(stderr, "Error: source exceeds %d bytes: %s\n",
                            MAX_SHELL_CMD, val);
                    fclose(f);
                    return false;
                }
                safe_copy(m->source, val, sizeof(m->source));
                if (m->source[0] == '$') {
                    m->is_remote = true;
                }
            } else if (strcmp(key, "key") == 0) {
                if (strlen(val) >= MAX_KEY) {
                    fprintf(stderr, "Error: key exceeds %d bytes: %s\n",
                            MAX_KEY, val);
                    fclose(f);
                    return false;
                }
                safe_copy(m->key, val, sizeof(m->key));
            }
        }
    }
    fclose(f);

    /* Validate metrics */
    for (int i = 0; i < g_metric_count; i++) {
        metric_t *m = &g_metrics[i];
        if (m->topic[0] == '\0') {
            fprintf(stderr, "Error: metric [%s] has no topic\n", m->section);
            return false;
        }
        if (m->source[0] == '\0') {
            fprintf(stderr, "Error: metric [%s] has no source\n", m->section);
            return false;
        }
        if (m->interval <= 0 && !m->is_remote) {
            fprintf(stderr, "Error: metric [%s] has invalid interval (must be > 0)\n",
                    m->section);
            return false;
        }
        if (m->interval < 0) {
            fprintf(stderr, "Error: metric [%s] has negative interval\n", m->section);
            return false;
        }
    }

    return true;
}

/* Setup remote broker connections — non-fatal: errors logged but don't stop program */
static void setup_remote_brokers(void) {
    char r_user[MAX_USER], r_pass[MAX_PASS], r_host[MAX_HOST], r_topic[MAX_REMOTE_TOPIC];
    int r_port;
    bool r_auth;

    for (int i = 0; i < g_metric_count; i++) {
        if (!g_metrics[i].is_remote)
            continue;

        if (!parse_remote_source(g_metrics[i].source,
                                 r_user, sizeof(r_user),
                                 r_pass, sizeof(r_pass),
                                 r_host, sizeof(r_host),
                                 &r_port,
                                 r_topic, sizeof(r_topic),
                                 &r_auth)) {
            fprintf(stderr, "[remote] failed to parse source for [%s] — skipping\n",
                    g_metrics[i].section);
            g_metrics[i].remote_idx = -1;
            continue;
        }

        safe_copy(g_metrics[i].remote_topic, r_topic, sizeof(g_metrics[i].remote_topic));

        /* Find or create remote broker entry */
        int rb_idx = -1;
        for (int j = 0; j < g_remote_count; j++) {
            if (strcmp(g_remotes[j].host, r_host) == 0 &&
                g_remotes[j].port == r_port) {
                rb_idx = j;
                break;
            }
        }

        if (rb_idx < 0) {
            if (g_remote_count >= MAX_REMOTES) {
                fprintf(stderr, "[remote] too many remote brokers (max %d) — skipping [%s]\n",
                        MAX_REMOTES, g_metrics[i].section);
                g_metrics[i].remote_idx = -1;
                continue;
            }
            rb_idx = g_remote_count;
            memset(&g_remotes[rb_idx], 0, sizeof(remote_broker_t));
            safe_copy(g_remotes[rb_idx].host, r_host, sizeof(g_remotes[rb_idx].host));
            g_remotes[rb_idx].port = r_port;
            g_remotes[rb_idx].has_auth = r_auth;
            if (r_auth) {
                safe_copy(g_remotes[rb_idx].user, r_user, sizeof(g_remotes[rb_idx].user));
                safe_copy(g_remotes[rb_idx].pass, r_pass, sizeof(g_remotes[rb_idx].pass));
            }
            g_remotes[rb_idx].connected = false;
            g_remotes[rb_idx].logged_down = false;
            g_remotes[rb_idx].first_connect_done = false;
            g_remote_count++;

            /* Create mosquitto instance for this remote broker */
            struct mosquitto *rm = mosquitto_new(NULL, true, &g_remotes[rb_idx]);
            if (!rm) {
                fprintf(stderr, "[remote] cannot create mosquitto instance for %s:%d — skipping\n",
                        r_host, r_port);
                g_remote_count--;
                g_metrics[i].remote_idx = -1;
                continue;
            }

            mosquitto_connect_callback_set(rm, remote_on_connect);
            mosquitto_disconnect_callback_set(rm, remote_on_disconnect);
            mosquitto_message_callback_set(rm, remote_on_message);

            if (r_auth) {
                mosquitto_username_pw_set(rm, r_user, r_pass);
            }

            /* Auto-reconnect with backoff: 1s initial, 60s max, linear */
            mosquitto_reconnect_delay_set(rm, 1, 60, false);

            int rc = mosquitto_loop_start(rm);
            if (rc != MOSQ_ERR_SUCCESS) {
                fprintf(stderr, "[remote] loop_start for %s:%d failed: %s — skipping\n",
                        r_host, r_port, mosquitto_strerror(rc));
                mosquitto_destroy(rm);
                g_remote_count--;
                g_metrics[i].remote_idx = -1;
                continue;
            }

            /* Non-blocking connect — if broker is down, loop will retry */
            rc = mosquitto_connect_async(rm, r_host, r_port, 60);
            if (rc != MOSQ_ERR_SUCCESS) {
                fprintf(stderr, "[remote] connect_async for %s:%d failed: %s — will retry\n",
                        r_host, r_port, mosquitto_strerror(rc));
                /* NOT fatal — reconnect_delay_set will retry */
            }

            g_remotes[rb_idx].mosq = rm;

            if (g_mqtt.debug)
                printf("[remote] %s:%d created (for [%s])\n",
                       r_host, r_port, g_metrics[i].section);
        }

        g_metrics[i].remote_idx = rb_idx;

        if (g_mqtt.debug)
            printf("[remote] [%s] -> %s:%d/%s (idx=%d)\n",
                   g_metrics[i].section, r_host, r_port, r_topic, rb_idx);
    }

    fflush(stdout);
}

/* Cleanup all resources */
static void cleanup_all(void) {
    /* stop metric threads */
    for (int i = 0; i < g_metric_count; i++) {
        if (g_metrics[i].thread_started) {
            pthread_join(g_metrics[i].thread, NULL);
            g_metrics[i].thread_started = false;
        }
        pthread_mutex_destroy(&g_metrics[i].value_mutex);
    }

    /* stop remote brokers — force=true to avoid hanging on poll() */
    for (int i = 0; i < g_remote_count; i++) {
        if (g_remotes[i].mosq) {
            mosquitto_disconnect(g_remotes[i].mosq);
            mosquitto_loop_stop(g_remotes[i].mosq, true);
            mosquitto_destroy(g_remotes[i].mosq);
            g_remotes[i].mosq = NULL;
        }
    }

    /* stop local broker — force=true to avoid hanging */
    if (g_mosq) {
        mosquitto_disconnect(g_mosq);
        mosquitto_loop_stop(g_mosq, true);
        mosquitto_destroy(g_mosq);
        g_mosq = NULL;
    }

    mosquitto_lib_cleanup();
    pthread_mutex_destroy(&g_mqtt_mutex);
}

/* main */
int main(int argc, char *argv[]) {
    const char *cfg_file = DEFAULT_CFG;
    int opt;

    while ((opt = getopt(argc, argv, "f:h")) != -1) {
        switch (opt) {
        case 'f':
            cfg_file = optarg;
            break;
        case 'h':
            print_help();
            return 0;
        default:
            print_help();
            return 1;
        }
    }

    printf("%s, %s (%s)\n", APP_NAME, APP_VERSION, APP_LICENSE);
    printf("%s\n", APP_AUTHOR);
    printf("\n");

    if (!load_config(cfg_file))
        return 1;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    mosquitto_lib_init();

    /* create local broker connection */
    g_mosq = mosquitto_new(NULL, true, NULL);
    if (!g_mosq) {
        fprintf(stderr, "Error: cannot create mosquitto instance\n");
        return 1;
    }

    mosquitto_connect_callback_set(g_mosq, on_connect);
    mosquitto_disconnect_callback_set(g_mosq, on_disconnect);

    if (g_mqtt.has_auth) {
        mosquitto_username_pw_set(g_mosq, g_mqtt.user, g_mqtt.password);
    }

    mosquitto_reconnect_delay_set(g_mosq, 1, 60, false);

    int rc = mosquitto_loop_start(g_mosq);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Error: cannot start mosquitto loop: %s\n",
                mosquitto_strerror(rc));
        cleanup_all();
        return 1;
    }

    printf("Broker  : %s:%d\n", g_mqtt.host, g_mqtt.port);
    printf("Auth    : %s\n", g_mqtt.has_auth ? "yes" : "no");
    printf("Debug   : %s\n", g_mqtt.debug ? "on" : "off");
    printf("Metrics : %d\n", g_metric_count);
    for (int i = 0; i < g_metric_count; i++) {
        printf("  [%s] topic=%s interval=%d qos=%d source=%s key=%s\n",
               g_metrics[i].section, g_metrics[i].topic,
               g_metrics[i].interval, g_metrics[i].qos,
               g_metrics[i].source,
               g_metrics[i].key[0] ? g_metrics[i].key : "-");
    }
    printf("Running... (Ctrl+C to stop)\n");
    fflush(stdout);

    /* connect to local broker with retry */
    bool logged_error = false;
    while (g_running) {
        g_mqtt_connected = 0;
        rc = mosquitto_connect(g_mosq, g_mqtt.host, g_mqtt.port, 60);
        if (rc == MOSQ_ERR_SUCCESS) {
            for (int i = 0; i < 20 && g_running; i++) {
                if (g_mqtt_connected != 0)
                    break;
                usleep(100000);
            }
            if (g_mqtt_connected == 1) {
                logged_error = false;
                break;
            }
            if (!logged_error) {
                if (g_mqtt_connected == -1)
                    fprintf(stderr, "[mqtt] cannot connect to %s:%d: %s\n",
                            g_mqtt.host, g_mqtt.port,
                            mosquitto_strerror(g_mqtt_connect_rc));
                else
                    fprintf(stderr, "[mqtt] cannot connect to %s:%d: timeout\n",
                            g_mqtt.host, g_mqtt.port);
                logged_error = true;
            }
        } else {
            if (!logged_error) {
                fprintf(stderr, "[mqtt] cannot connect to %s:%d: %s\n",
                        g_mqtt.host, g_mqtt.port, mosquitto_strerror(rc));
                logged_error = true;
            }
        }
        if (g_running) {
            for (int i = 0; i < 30 && g_running; i++)
                usleep(100000); /* 3 sec retry */
        }
    }

    if (!g_running) {
        cleanup_all();
        printf("\nShutting down...\nDone.\n");
        return 0;
    }

    /* Start LOCAL metric threads first — so remote issues don't block local */
    for (int i = 0; i < g_metric_count; i++) {
        if (g_metrics[i].is_remote)
            continue;
        if (pthread_create(&g_metrics[i].thread, NULL,
                           metric_thread, &g_metrics[i]) != 0) {
            fprintf(stderr, "Error: cannot create thread for [%s]\n",
                    g_metrics[i].section);
        } else {
            g_metrics[i].thread_started = true;
        }
    }

    /* Setup remote brokers — non-fatal, doesn't block local metrics */
    setup_remote_brokers();

    /* Start REMOTE timer threads (for metrics with interval > 0) */
    for (int i = 0; i < g_metric_count; i++) {
        if (!g_metrics[i].is_remote)
            continue;
        if (g_metrics[i].remote_idx < 0)
            continue; /* remote setup failed for this metric */
        if (g_metrics[i].interval > 0) {
            if (pthread_create(&g_metrics[i].thread, NULL,
                               remote_timer_thread, &g_metrics[i]) != 0) {
                fprintf(stderr, "Error: cannot create timer thread for [%s]\n",
                        g_metrics[i].section);
            } else {
                g_metrics[i].thread_started = true;
            }
        }
        /* event-driven (no interval): no thread, remote_on_message handles it */
    }

    /* Main loop — wait for signal */
    while (g_running)
        sleep(1);

    printf("\nShutting down...\n");
    fflush(stdout);

    cleanup_all();
    printf("Done.\n");
    return 0;
}
