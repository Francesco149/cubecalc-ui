#ifndef SERIALIZATION_H
#define SERIALIZATION_H

#include "utils.c"
#include "graph.c"

// returns a Buf. all allocations use allocator and it's up to the caller to free them.
// designed to be used with the Arena allocator
u8* packTree(Allocator const* allocator, TreeData* g);

// deserialize tree from rawData into g.
// rawData is a Buf
int unpackTree(TreeData* g, u8* rawData);

#endif

#if defined(SERIALIZATION_IMPLEMENTATION) && !defined(SERIALIZATION_UNIT)
#define SERIALIZATION_UNIT

#include "thirdparty/protobuf-c/protobuf-c.c"
#include "proto/cubecalc.pb-c.h"
#include "proto/cubecalc.pb-c.c"

#include <stdio.h> // TODO: remove fprintfs

static int SavedConnectionEqual(SavedConnection* a, SavedConnection* b) {
  return (
    (a->fromid == b->fromid && a->toid == b->  toid) ||
    (a->fromid == b->  toid && a->toid == b->fromid)
  );
}

u8* packTree(Allocator const* allocator, TreeData* g) {
// override default allocator to use this memory arena so I don't have to manually free
// all the arrays

#undef allocatorDefault
#define allocatorDefault (*allocator)

  SavedNode* savedTree = 0;
  SavedNodeData* savedData = 0;
  SavedRect* savedRects = 0;

  size_t treeLen = BufLen(g->tree);
  BufReserve(&savedTree, treeLen);
  BufReserve(&savedData, treeLen);
  BufReserve(&savedRects, treeLen);

  SavedComment* savedCommentData = 0;
  BufReserve(&savedCommentData, BufLen(g->commentData));

  SavedResult* savedResultData = 0;
  BufReserve(&savedResultData, BufLen(g->resultData));

  size_t numConnections = 0;

  // first, convert everything into serialized structs, with no care for buckets
  BufEachi(g->tree, i) {
    Node* n = &g->tree[i];
    NodeData* d = &g->data[n->type][n->data];
    numConnections += BufLen(n->connections);

    SavedRect* sr = &savedRects[i];
    saved_rect__init(sr);
    sr->x = d->bounds.x;
    sr->y = d->bounds.y;
    sr->w = d->bounds.w;
    sr->h = d->bounds.h;

    SavedNodeData* sd = &savedData[i];
    saved_node_data__init(sd);
    sd->bounds = sr;

    if (n->type == NSPLIT) {
      sd->value = g->tree[d->value].id;
    } else {
      sd->value = d->value;
    }

    SavedNode* sn = &savedTree[i];
    saved_node__init(sn);
    sn->id = n->id;
    sn->data = sd;

    switch (n->type) {
      case NCOMMENT: {
        Comment* c = &g->commentData[n->data];
        SavedComment* sc = &savedCommentData[n->data];
        saved_comment__init(sc);
        sc->text = BufStrDupn(c->buf, c->len);

        sn->commentdata = sc;
        break;
      }
      case NRESULT: {
        Result* r = &g->resultData[n->data];
        SavedResult* sr = &savedResultData[n->data];
        saved_result__init(sr);
        sr->page = r->page;
        sr->perpage = r->perPage;
        sn->resultdata = sr;
        break;
      }
    }
  }

  // now we figure out buckets and connections and copy to bucket arrays
  SavedBucket* buckets = 0;
  BufReserve(&buckets, NLAST); // make sure we don't trigger realloc
  BufClear(buckets);

  SavedConnection* connections = 0;
  BufReserve(&connections, numConnections);
  BufClear(connections);

  for (size_t type = 0; type < NLAST; ++type) {
    size_t bucketLen = BufLen(g->data[type]);
    if (!bucketLen) continue;

    SavedNode* savedNodes = 0;
    BufReserve(&savedNodes, bucketLen);

    BufEachi(g->data[type], i) {
      NodeData* d = &g->data[type][i];
      Node* n = &g->tree[d->node];
      savedNodes[i] = savedTree[d->node];

      BufEach(int, n->connections, conn) {
        Node* to = &g->tree[*conn];
        SavedConnection* sc = BufAlloc(&connections);
        saved_connection__init(sc);
        sc->fromid = n->id;
        sc->toid = to->id;
      }
    }

    SavedBucket* buck = BufAlloc(&buckets);
    saved_bucket__init(buck);
    #define nodeTypeToSavedEntry(x) [x] = SAVED_NODE_TYPE__##x,
    int const nodeTypeToSaved[] = { nodeTypes(nodeTypeToSavedEntry) };
    buck->type = nodeTypeToSaved[type];
    BufToProto(buck, nodes, savedNodes);
  }

  // prune redundant connections
  SavedConnection* uniqueConnections = 0;
  BufReserve(&uniqueConnections, numConnections);
  BufClear(uniqueConnections);

  BufEach(SavedConnection, connections, conn) {
    BufEach(SavedConnection, uniqueConnections, uniq) {
      if (SavedConnectionEqual(conn, uniq)) {
        goto nextConn;
      }
    }

    *BufAlloc(&uniqueConnections) = *conn;
nextConn:;
  }

  SavedPreset preset;
  saved_preset__init(&preset);
  BufToProto(&preset, buckets, buckets);
  BufToProto(&preset, connections, uniqueConnections);

  u8* out = 0;
  BufReserve(&out, saved_preset__get_packed_size(&preset));
  saved_preset__pack(&preset, out);

// restore default allocator
#undef allocatorDefault
#define allocatorDefault allocatorDefault_

  return out;
}

int unpackTree(TreeData* g, u8* rawData) {
  int res = 0;
  SavedPreset* preset = 0;
  int* treeById = 0;

  treeClear(g);

  preset = saved_preset__unpack(0, BufLen(rawData), rawData);

  if (!preset) {
    fprintf(stderr, "missing preset\n");
    goto cleanup;
  }

  for (size_t i = 0; i < preset->n_buckets; ++i) {
    SavedBucket* buck = preset->buckets[i];

#define savedToNodeTypeEntry(x) [SAVED_NODE_TYPE__##x] = x,
    int const savedToNodeType[] = { nodeTypes(savedToNodeTypeEntry) };
    int type = savedToNodeType[buck->type];

    BufReserveZero(&g->data[type], buck->n_nodes);

    switch (type) {
      case NCOMMENT: BufReserveZero(&g->commentData, buck->n_nodes); break;
      case NRESULT: BufReserveZero(&g->resultData, buck->n_nodes); break;
    }

    for (size_t j = 0; j < buck->n_nodes; ++j) {
      SavedNode* sn = buck->nodes[j];

      // map tree id to tree index to convert the connections later
      if (sn->id + 1 > BufLen(treeById)) {
        BufReserve(&treeById, sn->id + 1 - BufLen(treeById));
      }
      treeById[sn->id] = BufLen(g->tree);

      int in = treeAdd(g, type, 0, 0);
      if (in < 0) {
        goto cleanup;
      }

      Node* n = &g->tree[in];

      SavedNodeData* sd = sn->data;
      if (!sd) {
        fprintf(stderr, "node id %d missing data\n", n->id);
        goto cleanup;
      }

      SavedRect* r = sd->bounds;
      if (!r) {
        fprintf(stderr, "node id %d missing bounds\n", n->id);
        goto cleanup;
      }

      NodeData* d = &g->data[type][n->data];
      d->bounds = nk_rect(r->x, r->y, r->w, r->h);
      d->value = sd->value;

      switch (type) {
        case NCOMMENT: {
          SavedComment* sc = sn->commentdata;
          if (!sc) {
            fprintf(stderr, "node id %d missing commentData\n", n->id);
            goto cleanup;
          }
          Comment* c = &g->commentData[n->data];
          snprintf(c->buf, sizeof(c->buf), "%s", sc->text);
          c->len = strlen(sc->text);
          break;
        }
        case NRESULT: {
          SavedResult* sr = sn->resultdata;
          if (!sr) {
            fprintf(stderr, "node id %d missing resultData\n", n->id);
            goto cleanup;
          }
          Result* r = &g->resultData[n->data];
          r->page = sr->page;
          r->perPage = sr->perpage;
          break;
        }
      }

    }
  }

  // remember, these are also references to nodes which need to be converted from id to idx
  BufEach(NodeData, g->data[NSPLIT], d) {
    d->value = treeById[d->value];
  }

  for (size_t i = 0; i < preset->n_connections; ++i) {
    SavedConnection* con = preset->connections[i];
    int from = treeById[con->fromid];
    int   to = treeById[con->  toid];
    treeLink(g, from, to);
  }

  res = 1;

cleanup:
  saved_preset__free_unpacked(preset, 0);
  BufFree(&treeById);

  return res;
}

#endif
