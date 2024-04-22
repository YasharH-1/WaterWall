#pragma once

#include "basic_types.h"
#include "buffer_pool.h"
#include "hatomic.h"
#include "hloop.h"
#include "ww.h"

#define MAX_CHAIN_LEN (16 * 2)

#define STATE(x)      ((void *) ((x)->state))
#define CSTATE(x)     ((void *) ((((x)->line->chains_state)[self->chain_index])))
#define CSTATE_MUT(x) ((x)->line->chains_state)[self->chain_index]

typedef struct line_s
{
    uint8_t          tid;
    uint16_t         refc;
    uint8_t          lcid;
    uint8_t          auth_cur;
    uint8_t          auth_max;
    bool             alive;
    hloop_t *        loop;
    socket_context_t src_ctx;
    socket_context_t dest_ctx;
    void *           chains_state[];

} line_t;

typedef struct context_s
{
    line_t *        line;
    hio_t *         src_io;
    shift_buffer_t *payload;
    int             fd;
    bool            init;
    bool            est;
    bool            first;
    bool            fin;
} context_t;

struct tunnel_s;

typedef void (*TunnelFlowRoutine)(struct tunnel_s *, struct context_s *);

struct tunnel_s
{
    void *           state;
    hloop_t **       loops;
    struct tunnel_s *dw, *up;

    TunnelFlowRoutine upStream;
    TunnelFlowRoutine downStream;

    uint8_t chain_index;
};

typedef struct tunnel_s tunnel_t;

tunnel_t *newTunnel();

void destroyTunnel(tunnel_t *self);
void chain(tunnel_t *from, tunnel_t *to);
void chainDown(tunnel_t *from, tunnel_t *to);
void chainUp(tunnel_t *from, tunnel_t *to);
void defaultUpStream(tunnel_t *self, context_t *c);
void defaultDownStream(tunnel_t *self, context_t *c);

inline line_t *newLine(uint8_t tid)
{
    size_t  size   = sizeof(line_t) + (sizeof(void *) * MAX_CHAIN_LEN);
    line_t *result = malloc(size);
    // memset(result, 0, size);
    *result = (line_t){
        .tid      = tid,
        .refc     = 1,
        .lcid     = MAX_CHAIN_LEN - 1,
        .auth_cur = 0,
        .auth_max = 0,
        .loop     = loops[tid],
        .alive    = true,
        // to set a port we need to know the AF family, default v4
        .dest_ctx = (socket_context_t){.addr.sa = (struct sockaddr){.sa_family = AF_INET, .sa_data = {0}}},
        .src_ctx  = (socket_context_t){.addr.sa = (struct sockaddr){.sa_family = AF_INET, .sa_data = {0}}},
    };
    memset(&(result->chains_state), 0, (sizeof(void *) * MAX_CHAIN_LEN));
    return result;
}
inline uint8_t reserveChainStateIndex(line_t *l)
{
    uint8_t result = l->lcid;
    l->lcid -= 1;
    return result;
}
inline void internalUnRefLine(line_t *l)
{
    l->refc -= 1;
    // check line
    if (l->refc > 0)
    {
        return;
    }

    assert(l->alive == false);

    // there should not be any conn-state alive at this point
    for (size_t i = 0; i < MAX_CHAIN_LEN; i++)
    {
        assert(l->chains_state[i] == NULL);
    }

    assert(l->src_ctx.domain == NULL); // impossible (source domain?)

    if (l->dest_ctx.domain != NULL && ! l->dest_ctx.domain_constant)
    {
        free(l->dest_ctx.domain);
    }

    free(l);
}
inline void destroyLine(line_t *l)
{
    l->alive = false;
    internalUnRefLine(l);
}
inline void destroyContext(context_t *c)
{
    assert(c->payload == NULL);
    internalUnRefLine(c->line);
    free(c);
}
inline context_t *newContext(line_t *line)
{
    context_t *new_ctx = malloc(sizeof(context_t));
    *new_ctx           = (context_t){.line = line};
    line->refc += 1;
    return new_ctx;
}

inline context_t *newContextFrom(context_t *source)
{
    source->line->refc += 1;
    context_t *new_ctx = malloc(sizeof(context_t));
    *new_ctx           = (context_t){.line = source->line, .src_io = source->src_io};
    return new_ctx;
}
inline context_t *newEstContext(line_t *line)
{
    context_t *c = newContext(line);
    c->est       = true;
    return c;
}

inline context_t *newFinContext(line_t *line)
{
    context_t *c = newContext(line);
    c->fin       = true;
    return c;
}

inline context_t *newInitContext(line_t *line)
{
    context_t *c = newContext(line);
    c->init      = true;
    return c;
}
inline context_t *switchLine(context_t *c, line_t *line)
{
    line->refc += 1;
    internalUnRefLine(c->line);
    c->line = line;
    return c;
}
inline bool isAlive(line_t *line)
{
    return line->alive;
}
// when you don't have a context from a line, you cant guess the line is free()ed or not
// so you should use locks before losing the last context
inline void lockLine(line_t *line)
{
    line->refc++;
}
inline void unLockLine(line_t *line)
{
    destroyLine(line);
}

inline void markAuthenticationNodePresence(line_t *line)
{
    line->auth_max += 1;
}
inline void markAuthenticated(line_t *line)
{
    line->auth_cur += 1;
}
inline bool isAuthenticated(line_t *line)
{
    return line->auth_cur > 0;
}
inline bool isFullyAuthenticated(line_t *line)
{
    return line->auth_cur >= line->auth_max;
}


inline buffer_pool_t *geBufferPool(uint8_t tid)
{
    return buffer_pools[tid];
}
inline buffer_pool_t *getLineBufferPool(line_t *l)
{
    return buffer_pools[l->tid];
}
inline buffer_pool_t *getContextBufferPool(context_t *c)
{
    return  buffer_pools[c->line->tid];
}
inline void reuseContextBuffer(context_t *c)
{
    assert(c->payload != NULL);
    reuseBuffer(getContextBufferPool(c), c->payload);
    c->payload = NULL;
}