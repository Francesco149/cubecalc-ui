#ifndef SERIALIZATION_H
#define SERIALIZATION_H

#include "utils.c"
#include "graph.c"

// returns a Buf. all allocations use allocator and it's up to the caller to free them.
// designed to be used with the Arena allocator
char* packTree(Allocator const* allocator, TreeData* g);

// deserialize tree from rawData into g.
// rawData is a Buf
int unpackTree(TreeData* g, char* rawData);

char* packGlobals(char const* disclaimer);

// returns Buf containing disclaimer text
char* unpackGlobals(char* rawData);

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

char* packTree(Allocator const* allocator, TreeData* g) {
// override default allocator to use this memory arena so I don't have to manually free
// all the arrays

#undef allocatorDefault
#define allocatorDefault (*allocator)

  SavedNode* savedTree = 0;
  SavedNodeData* savedData = 0;
  SavedRect* savedRects = 0;

  size_t treeLen = BufLen(g->tree);
  (void)BufReserve(&savedTree, treeLen);
  (void)BufReserve(&savedData, treeLen);
  (void)BufReserve(&savedRects, treeLen);

  SavedComment* savedCommentData = 0;
  (void)BufReserve(&savedCommentData, BufLen(g->commentData));

  SavedResult* savedResultData = 0;
  (void)BufReserve(&savedResultData, BufLen(g->resultData));

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

    SavedNode* sn = &savedTree[i];
    saved_node__init(sn);
    sn->id = n->id;
    sn->data = sd;

    // we want to store the actual values of the enums rather than indices.
    // values are assumed to be stable, indices might change if we add new things ad it's
    // sorted in a specific way

    switch (n->type) {
      case NCOMMENT: {
        Comment* c = &g->commentData[n->data];
        SavedComment* sc = &savedCommentData[n->data];
        saved_comment__init(sc);
        sc->text = BufStrDupn(c->buf, c->len);

        sn->commentdata = sc;
        break;
      }
      case NCUBE: {
        sd->valuelo = cubeValues[d->value];
        break;
      }
      case NTIER: {
        sd->valuelo = tierValues[d->value];
        break;
      }
      case NCATEGORY: {
        sd->valuelo = categoryValues[d->value];
        break;
      }
      case NSTAT: {
        sd->valuehi = allLinesHi[d->value];
        sd->valuelo = allLinesLo[d->value];
        break;
      }
      case NAMOUNT:
      case NLEVEL: {
        sd->valuelo = d->value;
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
      case NSPLIT: {
        // for nodes, we store the node id to look it back up later
        sd->valuelo = g->tree[d->value].id;
        break;
      }
      case NREGION: {
        sd->valuelo = regionValues[d->value];
        break;
      }
    }
  }

  // now we figure out buckets and connections and copy to bucket arrays
  SavedBucket* buckets = 0;
  (void)BufReserve(&buckets, NLAST); // make sure we don't trigger realloc
  BufClear(buckets);

  SavedConnection* connections = 0;
  (void)BufReserve(&connections, numConnections);
  BufClear(connections);

  for (size_t type = 0; type < NLAST; ++type) {
    size_t bucketLen = BufLen(g->data[type]);
    if (!bucketLen) continue;

    SavedNode* savedNodes = 0;
    (void)BufReserve(&savedNodes, bucketLen);

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
  (void)BufReserve(&uniqueConnections, numConnections);
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

  char* out = 0;
  BufReserve(&out, saved_preset__get_packed_size(&preset));
  saved_preset__pack(&preset, (unsigned char*)out);

// restore default allocator
#undef allocatorDefault
#define allocatorDefault allocatorDefault_

  return out;
}

#define enumValueToIndex(d, id, name, vals, val) \
  _enumValueToIndex(d, id, name, vals, ArrayLength(vals), val)
int _enumValueToIndex(NodeData* d, int id, char const* name, int const* vals, size_t n, int val) {
  intmax_t i = _ArrayFindInt(vals, n, val);
  if (i < 0 || i > 0x7FFFFFFF) {
    fprintf(stderr, "node id %d has invalid %s index %jd, value 0x%08x\n", id, name, i, val);
    return 0;
  }
  d->value = (int)i;
  return 1;
}

int unpackTree(TreeData* g, char* rawData) {
  int res = 0;
  SavedPreset* preset = 0;
  int* treeById = 0;

  treeClear(g);

  preset = saved_preset__unpack(0, BufLen(rawData), (unsigned char*)rawData);

  if (!preset) {
    fprintf(stderr, "missing preset\n");
    goto cleanup;
  }

  for (size_t i = 0; i < preset->n_buckets; ++i) {
    SavedBucket* buck = preset->buckets[i];

#define savedToNodeTypeEntry(x) [SAVED_NODE_TYPE__##x] = x,
    int const savedToNodeType[] = { nodeTypes(savedToNodeTypeEntry) };
    int type = savedToNodeType[buck->type];

    for (size_t j = 0; j < buck->n_nodes; ++j) {
      SavedNode* sn = buck->nodes[j];

      // map tree id to tree index to convert the connections later
      if (sn->id + 1 > BufLen(treeById)) {
        (void)BufReserve(&treeById, sn->id + 1 - BufLen(treeById));
      }
      treeById[sn->id] = BufLen(g->tree);

      int in = treeAddId(sn->id, g, type, 0, 0);
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
        case NCUBE: {
          if (!enumValueToIndex(d, n->id, "cube", cubeValues, sd->valuelo)) goto cleanup;
          break;
        }
        case NTIER: {
          if (!enumValueToIndex(d, n->id, "tier", tierValues, sd->valuelo)) goto cleanup;
          break;
        }
        case NCATEGORY: {
          if (!enumValueToIndex(d, n->id, "category", categoryValues, sd->valuelo)) goto cleanup;
          break;
        }
        case NSTAT: {
          int idx = -1;
          ArrayEachi(allLinesLo, i) {
            if (allLinesLo[i] == sd->valuelo && allLinesHi[i] == sd->valuehi) {
              if (i > 0x7FFFFFFF) {
                fprintf(stderr, "index %jd out of range for stat index\n", i);
              } else {
                idx = (int)i;
              }
            }
          }
          if (idx < 0) {
            fprintf(stderr, "no stat match for lo: %08x hi: %08x\n", sd->valuelo, sd->valuehi);
            goto cleanup;
          }
          d->value = idx;
          break;
        }
        case NSPLIT: // for nsplit, the node id is looked up later
        case NAMOUNT:
        case NLEVEL: {
          d->value = sd->valuelo;
          break;
        }
        case NREGION: {
          if (!enumValueToIndex(d, n->id, "region", regionValues, sd->valuelo)) goto cleanup;
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

char* packGlobals(char const* disclaimer) {
  SavedGlobals glob;
  saved_globals__init(&glob);
  // NOTE: hopefully it doesn't try to modify it
  glob.disclaimer = (char*)disclaimer;

  char* out = 0;
  (void)BufReserve(&out, saved_globals__get_packed_size(&glob));
  saved_globals__pack(&glob, (unsigned char*)out);
  return out;
}

char* unpackGlobals(char* rawData) {
  SavedGlobals* glob = saved_globals__unpack(0, BufLen(rawData), (unsigned char*)rawData);
  char* res = BufStrDup(glob->disclaimer);
  saved_globals__free_unpacked(glob, 0);
  return res;
}

#endif
