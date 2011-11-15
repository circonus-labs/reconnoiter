/*
 * Copyright (c) 2011, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name OmniTI Computer Consulting, Inc. nor the names
 *       of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written
 *       permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "btrie.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <assert.h>
// BITS should be either 32 or 128
#define MAXBITS 128

typedef struct btrie_collapsed_node {
  struct btrie_collapsed_node *bit[2];
  void *data;
  uint32_t bits[MAXBITS/32];
  unsigned char prefix_len;
  unsigned char incidental;
#ifdef DEBUG_BTRIE
  char *long_desc;
#endif
} btrie_node;

#define BIT_AT(k,b) ((k[(b-1)/32] >> (31 - ((b-1)%32))) & 0x1)

static void drop_node(btrie_node *node, void (*f)(void *)) {
  if(node == NULL) return;
  if(node->bit[0]) drop_node(node->bit[0], f);
  if(node->bit[1]) drop_node(node->bit[1], f);
  if(node->data && f) f(node->data);
#ifdef DEBUG_BTRIE
  if(node->long_desc) free(node->long_desc);
#endif
  free(node);
}
void drop_tree(btrie *tree, void (*f)(void *)) {
  drop_node(*tree, f);
  *tree = NULL;
}
static inline int match_bpm(btrie_node *node,
                            uint32_t *key, unsigned char match_len) {
  register int i, m = (match_len-1)/32;
  if(match_len <= 0) return 1;
  for(i=0;i<=m;i++) {
    if(i<m) { /* we're matching a whole word */
      if(node->bits[i] != key[i]) return 0;
    }
    else {
      uint32_t mask = ((match_len % 32) == 0) ? 0xffffffff :
        ~(0xffffffff >> (match_len % 32));
      if((node->bits[i] & mask) != (key[i] & mask)) return 0;
    }
  }
  return 1;
}

static inline int calc_bits_in_commons(btrie_node *node,
                                       uint32_t *key,
                                       unsigned char match_len) {
 /* Largest common mask */
  int i;
  u_int32_t prefix_len = 0;

  for (i = 0; i < 32/match_len; i++) {
    u_int32_t mask = 0, trymask;

    while(mask != 0xffffffff) {
      trymask = (mask >> 1) | 0x80000000;
      if((node->bits[i] & trymask) != (key[i] & trymask)) {        break;
      }
      mask = trymask;
      prefix_len++;
    }

    if (mask != 0xffffffff) {
      break;
    }
  }
  return prefix_len;
}

static int
del_route(btrie *tree, uint32_t *key, unsigned char prefix_len,
          void (*f)(void *)) {
  btrie_node *parent = NULL, *node;
  node = *tree;
  while(node && node->prefix_len <= prefix_len &&
        match_bpm(node, key, node->prefix_len)) {
    if(node->prefix_len == prefix_len) {
      /* exact match */
      if(node->data && f) f(node->data);
      node->data = NULL;
      node->incidental = 1;
      if(node->bit[0] == NULL || node->bit[1] == NULL) {
        /* collapse (even if both are null) */
        parent->bit[BIT_AT(key, parent->prefix_len+1)] =
          node->bit[ (node->bit[0] == NULL) ? 1 : 0 ];
        node->bit[0] = node->bit[1] = NULL;
        drop_node(node, f);
      }
      return 1;
    }
    parent = node;
    node = parent->bit[BIT_AT(key, parent->prefix_len+1)];
  }
  return 0;
}
static int
find_bpm_route(btrie *tree, uint32_t *key, unsigned char prefix_len,
               btrie_node **rnode, btrie_node **explicit_container) {
  int stack_pos = -1, exact = 0;
  btrie_node *pstack[MAXBITS], *parent = NULL, *node;
  node = *tree;
  while(node && node->prefix_len <= prefix_len &&
        match_bpm(node, key, node->prefix_len)) {
#ifdef DEBUG_BTRIE
    char ipb[128];
    uint32_t addr[4];
    int i=0; \
    for(i=0;i<4;i++) addr[i] = (i*32)+1 >= prefix_len ? 0 : htonl(key[i]);
    inet_ntop(prefix_len > 32 ? AF_INET6 : AF_INET, addr, ipb, sizeof(ipb));
    fprintf(stderr, "%s looking at %s/%d\n", ipb, node->long_desc, node->prefix_len);
#endif
    parent = pstack[++stack_pos] = node;
    if(parent->prefix_len == prefix_len) {
      exact = 1;
      break;
    }
    node = parent->bit[BIT_AT(key, parent->prefix_len+1)];
  }
  if(rnode) *rnode = parent;
  while(stack_pos > 0 && pstack[stack_pos]->incidental == 1) stack_pos--;
  if(explicit_container)
    *explicit_container = stack_pos >= 0 ? pstack[stack_pos] : NULL;
  return exact;
}
void *
find_bpm_route_ipv6(btrie *tree, struct in6_addr *a, unsigned char *pl) {
  btrie_node *node = NULL;
  uint32_t ia[4], i;
  memcpy(ia, &a->s6_addr, sizeof(ia));
  for(i=0;i<4;i++) ia[i] = ntohl(ia[i]);
  find_bpm_route(tree, ia, 128, NULL, &node);
  if(node && pl) *pl = node->prefix_len;
  if(node && node->data) return node->data;
  return NULL;
}
void *
find_bpm_route_ipv4(btrie *tree, struct in_addr *a, unsigned char *pl) {
  btrie_node *node = NULL;
  uint32_t ia = ntohl(a->s_addr);
  find_bpm_route(tree, &ia, 32, NULL, &node);
  if(node && pl) *pl = node->prefix_len;
  if(node && node->data) return node->data;
  return NULL;
}

int
del_route_ipv6(btrie *tree, struct in6_addr *a, unsigned char prefix_len,
               void (*f)(void *)) {
  uint32_t ia[4], i;
  memcpy(ia, &a->s6_addr, sizeof(ia));
  for(i=0;i<4;i++) ia[i] = ntohl(ia[i]);
  return del_route(tree, ia, prefix_len, f);
}
int
del_route_ipv4(btrie *tree, struct in_addr *a, unsigned char prefix_len,
               void (*f)(void *)) {
  uint32_t ia = ntohl(a->s_addr);
  return del_route(tree, &ia, prefix_len, f);
}

void add_route(btrie *tree, uint32_t *key, unsigned char prefix_len,
               void *data) {
#ifdef DEBUG_BTRIE
  char ipb[128];
#define DA(n, pl, m) do { \
  uint32_t addr[4]; \
  int i=0; \
  for(i=0;i<4;i++) addr[i] = (i*32)+1 >= prefix_len ? 0 : htonl(key[i]); \
  inet_ntop(prefix_len > 32 ? AF_INET6 : AF_INET, addr, ipb, sizeof(ipb)); \
  (n)->long_desc = strdup(ipb); \
  fprintf(stderr, "N(%s/%d) -> %s\n", (n)->long_desc, pl, m ? m : "insert"); \
} while(0)
#else
#define DA(n, pl, m)
#endif
  btrie_node *node, *parent, *down, *newnode;
  int bits_in_common;

  assert(prefix_len <= MAXBITS);
  if(!*tree) {
    node = (btrie_node *)calloc(1, sizeof(*node));
    node->data = data;
    memcpy((void *)node->bits, (void *)key, 4*((prefix_len+31)/32));
    node->prefix_len = prefix_len;
    DA(node, prefix_len, NULL);
    *tree = node;
    return;
  }
  if(find_bpm_route(tree, key, prefix_len, &node, NULL)) {
    /* exact match */
    node->incidental = 0;
    node->data = data;
    return;
  }

  newnode = (btrie_node *)calloc(1, sizeof(*node));
  newnode->data = data;
  memcpy((void *)newnode->bits, (void *)key, 4*((prefix_len+31)/32));
  newnode->prefix_len = prefix_len;

  if(!node) down = *tree;
  else down = node->bit[BIT_AT(key, node->prefix_len+1)];
  if(!down) {
    node->bit[BIT_AT(key, node->prefix_len+1)] = newnode;
    DA(newnode, prefix_len, NULL);
    return;
  }
  /* here we must be inserting between node and down */
  bits_in_common = calc_bits_in_commons(down, key, prefix_len);
  parent = node;
  assert(bits_in_common <= prefix_len);
  assert(!parent || parent->prefix_len < prefix_len);

  /* we either need to make a new branch above down and newnode
   * or newnode can be the branch.  newnode can be the branch if
   * its prefix_len == bits_in_common */
  if(newnode->prefix_len == bits_in_common) {
    /* newnode can be the branch */
    int plen = parent ? parent->prefix_len+1 : 1;
    if(parent)
      assert(BIT_AT(newnode->bits, plen) == BIT_AT(down->bits, plen));
    newnode->bit[BIT_AT(down->bits, newnode->prefix_len+1)] = down;
    if(!parent) *tree = newnode;
    else parent->bit[BIT_AT(newnode->bits, plen)] = newnode;
    DA(newnode, prefix_len, NULL);
  }
  else {
    /* reparent */
    node = (btrie_node *)calloc(1, sizeof(*node));
    node->prefix_len = bits_in_common;
    node->incidental = 1;
    memcpy(node->bits, newnode->bits, sizeof(node->bits));
    DA(node, node->prefix_len, "incidental");
    assert(BIT_AT(down->bits, node->prefix_len+1) !=
           BIT_AT(newnode->bits, node->prefix_len+1));
    node->bit[BIT_AT(down->bits, node->prefix_len+1)] = down;
    node->bit[BIT_AT(newnode->bits, node->prefix_len+1)] = newnode;
    if(!parent) *tree = node;
    else parent->bit[BIT_AT(node->bits, parent->prefix_len+1)] = node;
    DA(newnode, prefix_len, NULL);
  }
}

void add_route_ipv4(btrie *tree, struct in_addr *a,
                    unsigned char prefix_len, void *data) {
  uint32_t ia = ntohl(a->s_addr);
  assert(prefix_len <= 32);
  add_route(tree, &ia, prefix_len, data);
}
void add_route_ipv6(btrie *tree, struct in6_addr *a,
                    unsigned char prefix_len, void *data) {
  uint32_t ia[4], i;
  memcpy(ia, &a->s6_addr, sizeof(ia));
  for(i=0;i<4;i++) ia[i] = ntohl(ia[i]);
  assert(prefix_len <= 128);
  add_route(tree, ia, prefix_len, data);
}

