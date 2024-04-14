#pragma once

#include "basic_types.h"
#include "hv/hatomic.h"
#include "hv/hloop.h"
#include "buffer_pool.h"

#define MAX_CHAIN_LEN 30

#define DISCARD_CONTEXT(x)                                   \
    do                                                       \
    {                                                        \
        assert(x->payload != NULL);                          \
        reuseBuffer(buffer_pools[x->line->tid], x->payload); \
        x->payload = NULL;                                   \
    } while (0)

// no memset 0
typedef struct line_s
{
    hloop_t *loop;
    uint16_t tid;
    uint16_t refc;
    uint16_t lcid;
    uint8_t auth_cur;
    uint8_t auth_max;

    socket_context_t src_ctx;
    socket_context_t dest_ctx;
    void *chains_state[];

} line_t;

// no memset 0
typedef struct context_s
{
    line_t *line;
    hio_t *src_io;
    shift_buffer_t *payload;
    int fd;
    bool init;
    bool est;
    bool first;
    bool fin;
} context_t;

typedef struct tunnel_s
{
    void *state;
    hloop_t **loops;
    struct tunnel_s *dw, *up;

    void (*upStream)(struct tunnel_s *self, context_t *c);
    void (*packetUpStream)(struct tunnel_s *self, context_t *c);
    void (*downStream)(struct tunnel_s *self, context_t *c);
    void (*packetDownStream)(struct tunnel_s *self, context_t *c);

    size_t chain_index;

} tunnel_t;

tunnel_t *newTunnel();

void destroyTunnel(tunnel_t *self);
void chain(tunnel_t *self, tunnel_t *next);
void defaultUpStream(tunnel_t *self, context_t *c);
void defaultPacketUpStream(tunnel_t *self, context_t *c);
void defaultDownStream(tunnel_t *self, context_t *c);
void defaultPacketDownStream(tunnel_t *self, context_t *c);

extern struct hloop_s **loops; // ww.h runtime api
inline line_t *newLine(uint16_t tid)
{
    size_t size = sizeof(line_t) + (sizeof(void *) * MAX_CHAIN_LEN);
    line_t *result = malloc(size);
    // memset(result, 0, size);
    result->tid = tid;
    result->refc = 1;
    result->lcid = MAX_CHAIN_LEN - 1;
    result->auth_cur = 0;
    result->auth_max = 0;
    result->loop = loops[tid];
    result->chains_state = {0};
    // to set a port we need to know the AF family, default v4
    result->dest_ctx.addr.sa = (struct sockaddr){.sa_family = AF_INET, .sa_data = {0}};
    result->src_ctx.addr.sa = (struct sockaddr){.sa_family = AF_INET, .sa_data = {0}};
    return result;
}
inline size_t reserveChainStateIndex(line_t *l)
{
    size_t result = l->lcid;
    l->lcid -= 1;
    return result;
}
inline void destroyLine(line_t *l)
{
    l->refc -= 1;
    // check line
    if (l->refc > 0)
        return;

#ifdef DEBUG
    // there should not be any conn-state alive at this point
    for (size_t i = 0; i < MAX_CHAIN_LEN; i++)
    {
        assert(l->chains_state[i] == NULL);
    }

    if (l->dest_ctx.domain != NULL && !l->dest_ctx.domain_is_constant_memory)
        assert(l->dest_ctx.domain == NULL);

#endif

    // if (l->dest_ctx.domain != NULL && !l->dest_ctx.domain_is_constant_memory)
    //     free(l->dest_ctx.domain);
    free(l);
}
inline void destroyContext(context_t *c)
{
    assert(c->payload == NULL);
    destroyLine(c->line);
    free(c);
}
inline context_t *newContext(line_t *line)
{
    context_t *new_ctx = malloc(sizeof(context_t));
    *new_ctx = (context_t){.line = line}; // yes, everything else is zero
    line->refc += 1;
    return new_ctx;
}

inline context_t *newContextFrom(context_t *source)
{
    source->line->refc += 1;
    context_t *new_ctx = malloc(sizeof(context_t));
    *new_ctx = *source;
    new_ctx->payload = NULL;
    new_ctx->init = false;
    new_ctx->est = false;
    new_ctx->first = false;
    new_ctx->fin = false;
    return new_ctx;
}
inline context_t *newEstContext(line_t *line)
{
    context_t *c = newContext(line);
    c->est = true;
    return c;
}

inline context_t *newFinContext(line_t *line)
{
    context_t *c = newContext(line);
    c->fin = true;
    return c;
}

inline context_t *newInitContext(line_t *line)
{
    context_t *c = newContext(line);
    c->init = true;
    return c;
}
inline context_t *switchLine(context_t *c, line_t *line)
{
    line->refc += 1;
    destroyLine(c->line);
    c->line = line;
    return c;
}

static inline line_t *lockLine(line_t *line)
{
    line->refc++;
    return line;
}
static inline void unLockLine(line_t *line) { destroyLine(line); }

static inline void markAuthenticationNodePresence(line_t *line) { line->auth_max += 1; }
static inline void markAuthenticated(line_t *line) { line->auth_cur += 1; }
static inline bool isAuthenticated(line_t *line) { return line->auth_cur > 0; }
static inline bool isFullyAuthenticated(line_t *line) { return line->auth_cur >= line->auth_max; }
