/**
 * @file gateway.c
 * @brief Gateway discovery, assignment, topology, and forwarding state machine.
 */
#include "corelib/gateway.h"
#include "internal/corelib_internal.h"

#include <stdalign.h>
#include <string.h>

#define GATEWAY_SIGNATURE 0x50444757u
#define CONTROL_DISCOVER 3u
#define CONTROL_NODE_FOUND 4u
#define CONTROL_ADDRESS_ASSIGN 5u
#define CONTROL_ADDRESS_ACK 6u
#define CONTROL_NODE_READY 7u
#define CONTROL_NODE_REMOVED 9u
#define CONTROL_ROUTE_ERROR 10u
#define CONTROL_SESSION_END 11u
#define CONTROL_CONTROL_ERROR 12u
#define TLV_NODE_UUID 1u
#define TLV_NODE_ADDRESS 2u
#define TLV_PARENT_ADDRESS 3u
#define TLV_LINK_ID 4u
#define TLV_LINK_PROFILE_ID 5u
#define TLV_CAPABILITIES 6u
#define TLV_STATUS 7u
#define TLV_HEARTBEAT_INTERVAL 9u
#define TLV_OFFENDING_MESSAGE_ID 10u
#define TLV_DISCOVERY_TOKEN 11u
#define GATEWAY_MAX_CONTROL_SLOTS 8u

typedef struct {
  bool used;
  bool available;
  corelib_link_id_t id;
  corelib_link_profile_id_t profile;
  corelib_link_role_t role;
  void *transport;
} gateway_link_t;

typedef struct {
  bool used;
  uint8_t uuid[16];
  uint32_t capabilities;
  uint16_t address;
  uint16_t parent;
  uint8_t depth;
  corelib_route_state_t state;
  corelib_link_id_t next_link;
} gateway_route_t;

typedef struct {
  bool used;
  bool callback_pending;
  uint8_t token[16];
  corelib_link_id_t link;
  uint64_t deadline;
} gateway_discovery_t;

typedef struct {
  bool used;
  uint8_t uuid[16];
  uint8_t token[16];
  uint32_t capabilities;
  corelib_link_id_t link;
  uint64_t deadline;
} gateway_candidate_t;

typedef struct {
  bool used;
  bool callback_pending;
  corelib_bootstrap_assignment_t value;
  uint32_t capabilities;
  uint64_t deadline;
} gateway_assignment_t;

typedef struct {
  bool used;
  corelib_link_id_t link;
  corelib_link_id_t origin;
  uint32_t work;
  uint32_t sequence;
  uint8_t priority;
  uint8_t frame[64];
} gateway_forward_t;

typedef struct {
  bool used;
  uint32_t session;
  uint32_t message_id;
  uint16_t source;
  uint16_t destination;
  uint16_t length;
  uint8_t count;
  uint8_t priority;
  uint8_t hop;
  uint64_t started;
} gateway_control_assembly_t;

struct corelib_gateway_context {
  uint32_t signature;
  bool in_call;
  bool flushing;
  uint64_t now;
  uint32_t sequence;
  uint32_t next_message;
  uint32_t next_work;
  uint16_t upstream_peer_address;
  corelib_gateway_config_t config;
  corelib_config_t application_config;
  corelib_context_t *device;
  gateway_control_assembly_t assemblies[GATEWAY_MAX_CONTROL_SLOTS];
};

static void clear_gateway(corelib_gateway_context_t *g, bool keep_links);
static void remove_routes_for_link(corelib_gateway_context_t *g, corelib_link_id_t link);

/**
 * @brief Advance a wrapping counter while reserving zero as an invalid value.
 * @param[in] value Current counter value.
 * @return Next non-zero counter value.
 */
static uint32_t next_nonzero(uint32_t value) {
  uint32_t result;
  if (value == UINT32_MAX) {
    result = 1u;
  } else {
    result = value + 1u;
  }
  return result;
}
static void clear_link_work(corelib_gateway_context_t *g, corelib_link_id_t link);

_Static_assert(sizeof(gateway_link_t) <= CORELIB_GATEWAY_ENTRY_STORAGE_SIZE,
               "gateway link entry too large");
_Static_assert(sizeof(gateway_route_t) <= CORELIB_GATEWAY_ENTRY_STORAGE_SIZE,
               "gateway route entry too large");
_Static_assert(sizeof(gateway_discovery_t) <= CORELIB_GATEWAY_ENTRY_STORAGE_SIZE,
               "gateway discovery entry too large");
_Static_assert(sizeof(gateway_candidate_t) <= CORELIB_GATEWAY_ENTRY_STORAGE_SIZE,
               "gateway candidate entry too large");
_Static_assert(sizeof(gateway_assignment_t) <= CORELIB_GATEWAY_ENTRY_STORAGE_SIZE,
               "gateway assignment entry too large");
_Static_assert(sizeof(gateway_forward_t) <= CORELIB_GATEWAY_ENTRY_STORAGE_SIZE,
               "gateway forwarding entry too large");
_Static_assert(sizeof(corelib_gateway_context_t) <=
                   CORELIB_GATEWAY_CONTEXT_STORAGE_SIZE,
               "increase gateway context storage size");

/**
 * @brief Entry at.
 * @param[in] storage Value supplied through `storage`.
 * @param[in] index Bounded storage index.
 * @return Matching internal entry, or null when none is available.
 */
static void *entry_at(corelib_entry_storage_t *storage, size_t index) {
  uint8_t *entries = (uint8_t *)storage->entries;
  return (void *)&entries[index * storage->entry_size];
}

/**
 * @brief Address a byte within caller-owned storage.
 * @param[in,out] base Start of the validated storage region.
 * @param[in] offset Bounded byte offset within the region.
 * @return Address of the selected byte.
 */
static uint8_t *gateway_byte_at(uint8_t *base, size_t offset) {
  return &base[offset];
}

/**
 * @brief Test whether caller-owned storage meets an alignment requirement.
 * @param[in] memory Storage address to test.
 * @param[in] alignment Required power-of-two alignment.
 * @return True when the address is suitably aligned; otherwise false.
 */
static bool gateway_pointer_is_aligned(const void *memory, size_t alignment) {
  return ((uintptr_t)memory % (uintptr_t)alignment) == (uintptr_t)0u;
}

/**
 * @brief Read-only entry at a bounded storage index.
 * @param[in] storage Value supplied through `storage`.
 * @param[in] index Bounded storage index.
 * @return Matching internal entry.
 */
static const void *entry_at_const(const corelib_entry_storage_t *storage, size_t index) {
  const uint8_t *entries = (const uint8_t *)storage->entries;
  return (const void *)&entries[index * storage->entry_size];
}

/**
 * @brief Link at.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] i Value supplied through `i`.
 * @return Matching internal entry, or null when none is available.
 */
static gateway_link_t *link_at(corelib_gateway_context_t *g, size_t i) {
  return (gateway_link_t *)entry_at(&g->config.storage.links, i);
}
/**
 * @brief Route at.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] i Value supplied through `i`.
 * @return Matching internal entry, or null when none is available.
 */
static gateway_route_t *route_at(corelib_gateway_context_t *g, size_t i) {
  return (gateway_route_t *)entry_at(&g->config.storage.routes, i);
}
/**
 * @brief Discovery at.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] i Value supplied through `i`.
 * @return Matching internal entry, or null when none is available.
 */
static gateway_discovery_t *discovery_at(corelib_gateway_context_t *g, size_t i) {
  return (gateway_discovery_t *)entry_at(&g->config.storage.discoveries, i);
}
/**
 * @brief Candidate at.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] i Value supplied through `i`.
 * @return Matching internal entry, or null when none is available.
 */
static gateway_candidate_t *candidate_at(corelib_gateway_context_t *g, size_t i) {
  return (gateway_candidate_t *)entry_at(&g->config.storage.candidates, i);
}
/**
 * @brief Assignment at.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] i Value supplied through `i`.
 * @return Matching internal entry, or null when none is available.
 */
static gateway_assignment_t *assignment_at(corelib_gateway_context_t *g, size_t i) {
  return (gateway_assignment_t *)entry_at(&g->config.storage.assignments, i);
}
/**
 * @brief Forward at.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] i Value supplied through `i`.
 * @return Matching internal entry, or null when none is available.
 */
static gateway_forward_t *forward_at(corelib_gateway_context_t *g, size_t i) {
  return (gateway_forward_t *)entry_at(&g->config.storage.forwarding, i);
}

/**
 * @brief Find link.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] id Value supplied through `id`.
 * @return Matching internal entry, or null when none is available.
 */
static gateway_link_t *find_link(corelib_gateway_context_t *g, corelib_link_id_t id) {
  size_t i;
  for (i = 0; i < g->config.storage.links.capacity; ++i) {
    gateway_link_t *link = link_at(g, i);
    if (link->used && link->id == id) {
      return link;
    }
  }
  return NULL;
}

/**
 * @brief Upstream.
 * @param[in,out] g Gateway context used by the operation.
 * @return Matching internal entry, or null when none is available.
 */
static gateway_link_t *upstream(corelib_gateway_context_t *g) {
  size_t i;
  for (i = 0; i < g->config.storage.links.capacity; ++i) {
    gateway_link_t *link = link_at(g, i);
    if (link->used && link->role == CORELIB_LINK_UPSTREAM) {
      return link;
    }
  }
  return NULL;
}

/**
 * @brief Find route address.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] address Value supplied through `address`.
 * @return Matching internal entry, or null when none is available.
 */
static gateway_route_t *find_route_address(corelib_gateway_context_t *g, uint16_t address) {
  size_t i;
  for (i = 0; i < g->config.storage.routes.capacity; ++i) {
    gateway_route_t *route = route_at(g, i);
    if (route->used && route->address == address) {
      return route;
    }
  }
  return NULL;
}

/**
 * @brief Find route uuid.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] uuid Value supplied through `uuid`.
 * @return Matching internal entry, or null when none is available.
 */
static gateway_route_t *find_route_uuid(corelib_gateway_context_t *g, const uint8_t uuid[16]) {
  size_t i;
  for (i = 0; i < g->config.storage.routes.capacity; ++i) {
    gateway_route_t *route = route_at(g, i);
    if (route->used && memcmp(route->uuid, uuid, 16) == 0) {
      return route;
    }
  }
  return NULL;
}

/**
 * @brief Gateway diag.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] code Value supplied through `code`.
 * @param[in] status Value supplied through `status`.
 */
static void gateway_diag(corelib_gateway_context_t *g, corelib_diagnostic_t code, corelib_status_t status) {
  if (g->application_config.callbacks.diagnostic != NULL) {
    const bool entered = g->in_call;
    g->in_call = true;
    g->application_config.callbacks.diagnostic(g->application_config.callbacks.user, code, status);
    g->in_call = entered;
  }
}

/**
 * @brief Flush.
 * @param[in,out] g Gateway context used by the operation.
 * @return Operation status.
 */
static corelib_status_t flush(corelib_gateway_context_t *g) {
  corelib_status_t status = CORELIB_OK;
  bool finished = false;
  if (g->flushing) {
    return CORELIB_OK;
  }
  g->flushing = true;
  while (!finished) {
    gateway_forward_t *chosen = NULL;
    size_t i;
    for (i = 0; i < g->config.storage.forwarding.capacity; ++i) {
      gateway_forward_t *item = forward_at(g, i);
      if (item->used &&
          (chosen == NULL || item->priority < chosen->priority ||
           (item->priority == chosen->priority && item->sequence < chosen->sequence))) {
        chosen = item;
      }
    }
    if (chosen == NULL) {
      finished = true;
    } else {
      gateway_link_t *link = find_link(g, chosen->link);
      if (link == NULL || !link->available) {
        chosen->used = false;
      } else {
        const bool entered = g->in_call;
        corelib_send_result_t result;
        g->in_call = true;
        result = g->application_config.callbacks.send_frame(
            g->application_config.callbacks.user, link->id, link->transport,
            chosen->frame);
        g->in_call = entered;
        if (result == CORELIB_SEND_BUSY) {
          status = CORELIB_BUSY;
        } else if (result == CORELIB_SEND_FAILED) {
          const uint32_t work = chosen->work;
          for (i = 0; i < g->config.storage.forwarding.capacity; ++i) {
            gateway_forward_t *item = forward_at(g, i);
            if (item->used && item->work == work) {
              item->used = false;
            }
          }
          link->available = false;
          if (link->role == CORELIB_LINK_UPSTREAM) {
            (void)corelib_reset(g->device);
            clear_gateway(g, true);
          } else {
            clear_link_work(g, link->id);
            remove_routes_for_link(g, link->id);
          }
          gateway_diag(g, CORELIB_DIAGNOSTIC_SEND_FAILED, CORELIB_INVALID_STATE);
          status = CORELIB_INVALID_STATE;
        } else {
          chosen->used = false;
        }
      }
    }
    if (status != CORELIB_OK) {
      finished = true;
    }
  }
  g->flushing = false;
  return status;
}

/**
 * @brief Queue bytes.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] link Gateway link used by the operation.
 * @param[in] bytes Encoded byte buffer used by the operation.
 * @param[in] origin Value supplied through `origin`.
 * @return Operation status.
 */
static corelib_status_t queue_bytes(corelib_gateway_context_t *g, corelib_link_id_t link, const uint8_t bytes[64], corelib_link_id_t origin) {
  size_t i;
  uint32_t work = 0;
  for (i = 0; i < g->config.storage.forwarding.capacity; ++i) {
    const gateway_forward_t *queued = forward_at(g, i);
    if (queued->used && queued->origin == origin &&
        memcmp(&queued->frame[2], &bytes[2], 12) == 0) {
      work = queued->work;
      break;
    }
  }
  if (work == 0u) {
    g->next_work = next_nonzero(g->next_work);
    work = g->next_work;
  }
  for (i = 0; i < g->config.storage.forwarding.capacity; ++i) {
    gateway_forward_t *item = forward_at(g, i);
    if (!item->used) {
      item->used = true;
      item->link = link;
      item->origin = origin;
      item->work = work;
      ++g->sequence;
      item->sequence = g->sequence;
      item->priority = bytes[19];
      (void)memcpy(item->frame, bytes, 64);
      {
        const corelib_status_t status = flush(g);
        return status == CORELIB_BUSY ? CORELIB_OK : status;
      }
    }
  }
  gateway_diag(g, CORELIB_DIAGNOSTIC_RESOURCE_LIMIT, CORELIB_CAPACITY_EXCEEDED);
  return CORELIB_CAPACITY_EXCEEDED;
}

/**
 * @brief Free forward slots.
 * @param[in,out] g Gateway context used by the operation.
 * @return Computed size or bounded index.
 */
static size_t free_forward_slots(corelib_gateway_context_t *g) {
  size_t i;
  size_t count = 0;
  for (i = 0; i < g->config.storage.forwarding.capacity; ++i) {
    if (!forward_at(g, i)->used) {
      ++count;
    }
  }
  return count;
}

/**
 * @brief Store bytes.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] link Gateway link used by the operation.
 * @param[in] bytes Encoded byte buffer used by the operation.
 * @param[in] work Value supplied through `work`.
 * @return Operation status.
 */
static corelib_status_t store_bytes(corelib_gateway_context_t *g, corelib_link_id_t link, const uint8_t bytes[64], uint32_t work) {
  size_t i;
  for (i = 0; i < g->config.storage.forwarding.capacity; ++i) {
    gateway_forward_t *item = forward_at(g, i);
    if (!item->used) {
      item->used = true;
      item->link = link;
      item->origin = 0u;
      item->work = work;
      ++g->sequence;
      item->sequence = g->sequence;
      item->priority = bytes[19];
      (void)memcpy(item->frame, bytes, 64);
      return CORELIB_OK;
    }
  }
  return CORELIB_CAPACITY_EXCEEDED;
}

/**
 * @brief Local send.
 * @param[in,out] user Value supplied through `user`.
 * @param[in] link_id Value supplied through `link_id`.
 * @param[in,out] transport Value supplied through `transport`.
 * @param[in] frame PFP frame used by the operation.
 * @return Computed internal value.
 */
static corelib_send_result_t local_send(void *user, corelib_link_id_t link_id, void *transport, const uint8_t frame[64]) {
  corelib_gateway_context_t *g = user;
  const gateway_link_t *up = upstream(g);
  (void)link_id;
  (void)transport;
  if (up == NULL || !up->available) {
    return CORELIB_SEND_FAILED;
  }
  return queue_bytes(g, up->id, frame, 0u) == CORELIB_OK
             ? CORELIB_SEND_ACCEPTED
             : CORELIB_SEND_BUSY;
}

/**
 * @brief Local transaction.
 * @param[in,out] user Value supplied through `user`.
 * @param[in] value Value used by the operation.
 */
static void local_transaction(void *user, const corelib_transaction_t *value) {
  corelib_gateway_context_t *g = user;
  const bool entered = g->in_call;
  g->in_call = true;
  if (g->application_config.callbacks.transaction != NULL) {
    g->application_config.callbacks.transaction(g->application_config.callbacks.user, value);
  }
  g->in_call = entered;
}
/**
 * @brief Local session.
 * @param[in,out] user Value supplied through `user`.
 * @param[in] state Value supplied through `state`.
 * @param[in] session Value supplied through `session`.
 * @param[in] address Value supplied through `address`.
 */
static void local_session(void *user, corelib_session_state_t state, uint32_t session, uint16_t address) {
  corelib_gateway_context_t *g = user;
  const bool entered = g->in_call;
  g->in_call = true;
  if (g->application_config.callbacks.session_changed != NULL) {
    g->application_config.callbacks.session_changed(g->application_config.callbacks.user, state, session, address);
  }
  g->in_call = entered;
}
/**
 * @brief Local node.
 * @param[in,out] user Value supplied through `user`.
 * @param[in] uuid Value supplied through `uuid`.
 * @param[in] reachable Value supplied through `reachable`.
 * @param[in] address Value supplied through `address`.
 */
static void local_node(void *user, const uint8_t uuid[16], bool reachable, uint16_t address) {
  corelib_gateway_context_t *g = user;
  const bool entered = g->in_call;
  g->in_call = true;
  if (g->application_config.callbacks.node_changed != NULL) {
    g->application_config.callbacks.node_changed(g->application_config.callbacks.user, uuid, reachable, address);
  }
  g->in_call = entered;
}
/**
 * @brief Local diagnostic.
 * @param[in,out] user Value supplied through `user`.
 * @param[in] code Value supplied through `code`.
 * @param[in] status Value supplied through `status`.
 */
static void local_diagnostic(void *user, corelib_diagnostic_t code, corelib_status_t status) {
  gateway_diag((corelib_gateway_context_t *)user, code, status);
}

/**
 * @brief U16.
 * @param[in] p Value supplied through `p`.
 * @return Computed internal value.
 */
static uint16_t u16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
/**
 * @brief U32.
 * @param[in] p Value supplied through `p`.
 * @return Computed internal value.
 */
static uint32_t u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
/**
 * @brief W16.
 * @param[in,out] p Value supplied through `p`.
 * @param[in] v Value supplied through `v`.
 */
static void w16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
/**
 * @brief W32.
 * @param[in,out] p Value supplied through `p`.
 * @param[in] v Value supplied through `v`.
 */
static void w32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

typedef struct {
  const uint8_t *value[12];
  uint8_t length[12];
  bool critical[12];
} control_fields_t;

/**
 * @brief Decode control.
 * @param[in] bytes Encoded byte buffer used by the operation.
 * @param[in] size Number of valid bytes.
 * @param[in,out] opcode Value supplied through `opcode`.
 * @param[in,out] transaction Value supplied through `transaction`.
 * @param[in,out] fields Value supplied through `fields`.
 * @return True when the condition or operation succeeds; otherwise false.
 */
static bool decode_control(const uint8_t *bytes, size_t size, uint8_t *opcode, uint32_t *transaction, control_fields_t *fields) {
  size_t offset = 8;
  uint8_t previous = 0;
  if (size < 8u || bytes[0] != 1u || bytes[2] != 0u || bytes[3] != 0u) {
    return false;
  }
  (void)memset(fields, 0, sizeof(*fields));
  *opcode = bytes[1];
  *transaction = u32(&bytes[4]);
  while (offset < size) {
    uint8_t encoded;
    uint8_t id;
    uint8_t length;
    if (size - offset < 2u) {
      return false;
    }
    encoded = bytes[offset];
    ++offset;
    id = (uint8_t)(encoded & 0x7fu);
    length = bytes[offset];
    ++offset;
    if (id == 0u || id <= previous || length == 0u || length > size - offset) {
      return false;
    }
    if (id > 11u) {
      if ((encoded & 0x80u) != 0u) {
        return false;
      }
    } else {
      const bool valid_length =
          ((id == TLV_NODE_UUID || id == TLV_DISCOVERY_TOKEN) && length == 16u) ||
          ((id == TLV_NODE_ADDRESS || id == TLV_PARENT_ADDRESS ||
            id == TLV_LINK_ID || id == TLV_STATUS) &&
           length == 2u) ||
          ((id == TLV_LINK_PROFILE_ID || id == TLV_CAPABILITIES ||
            id == TLV_HEARTBEAT_INTERVAL || id == TLV_OFFENDING_MESSAGE_ID) &&
           length == 4u) ||
          (id == 8u && length <= 16u &&
           corelib_control_valid_utf8(&bytes[offset], length));
      if (!valid_length) {
        return false;
      }
      fields->value[id] = &bytes[offset];
      fields->length[id] = length;
      fields->critical[id] = (encoded & 0x80u) != 0u;
    }
    previous = id;
    offset += length;
  }
  return offset == size;
}

/**
 * @brief Required.
 * @param[in] f Value supplied through `f`.
 * @param[in] id Value supplied through `id`.
 * @param[in] length Value supplied through `length`.
 * @return True when the condition or operation succeeds; otherwise false.
 */
static bool required(const control_fields_t *f, uint8_t id, uint8_t length) {
  return f->value[id] != NULL && f->length[id] == length && f->critical[id];
}

/**
 * @brief Only fields.
 * @param[in] f Value supplied through `f`.
 * @param[in] allowed Value supplied through `allowed`.
 * @return True when the condition or operation succeeds; otherwise false.
 */
static bool only_fields(const control_fields_t *f, uint16_t allowed) {
  uint8_t id;
  for (id = 1u; id <= 11u; ++id) {
    if (f->value[id] != NULL && (allowed & (uint16_t)((uint16_t)1u << id)) == 0u) {
      return false;
    }
  }
  return true;
}

/**
 * @brief Ctl header.
 * @param[in,out] out Value supplied through `out`.
 * @param[in] opcode Value supplied through `opcode`.
 * @param[in] transaction Value supplied through `transaction`.
 * @return Computed size or bounded index.
 */
static size_t ctl_header(uint8_t *out, uint8_t opcode, uint32_t transaction) {
  (void)memset(out, 0, 8);
  out[0] = 1;
  out[1] = opcode;
  w32(&out[4], transaction);
  return 8;
}
/**
 * @brief Tlv.
 * @param[in,out] out Value supplied through `out`.
 * @param[in] offset Value supplied through `offset`.
 * @param[in] id Value supplied through `id`.
 * @param[in] critical Value supplied through `critical`.
 * @param[in] value Value used by the operation.
 * @param[in] length Value supplied through `length`.
 * @return Computed size or bounded index.
 */
static size_t tlv(uint8_t *out, size_t offset, uint8_t id, bool critical, const void *value, uint8_t length) {
  size_t cursor = offset;
  out[cursor] = (uint8_t)(id | (critical ? 0x80u : 0u));
  ++cursor;
  out[cursor] = length;
  ++cursor;
  (void)memcpy(&out[cursor], value, length);
  return cursor + length;
}
/**
 * @brief Tlv16.
 * @param[in,out] out Value supplied through `out`.
 * @param[in] offset Value supplied through `offset`.
 * @param[in] id Value supplied through `id`.
 * @param[in] value Value used by the operation.
 * @return Computed size or bounded index.
 */
static size_t tlv16(uint8_t *out, size_t offset, uint8_t id, uint16_t value) {
  uint8_t b[2];
  w16(b, value);
  return tlv(out, offset, id, true, b, 2);
}
/**
 * @brief Tlv32.
 * @param[in,out] out Value supplied through `out`.
 * @param[in] offset Value supplied through `offset`.
 * @param[in] id Value supplied through `id`.
 * @param[in] value Value used by the operation.
 * @param[in] critical Value supplied through `critical`.
 * @return Computed size or bounded index.
 */
static size_t tlv32(uint8_t *out, size_t offset, uint8_t id, uint32_t value, bool critical) {
  uint8_t b[4];
  w32(b, value);
  return tlv(out, offset, id, critical, b, 4);
}

/**
 * @brief Enqueue control.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] link Gateway link used by the operation.
 * @param[in] destination Value supplied through `destination`.
 * @param[in] source Value supplied through `source`.
 * @param[in] message Message used by the operation.
 * @param[in] size Number of valid bytes.
 * @param[in] work Value supplied through `work`.
 * @return Operation status.
 */
static corelib_status_t enqueue_control(corelib_gateway_context_t *g, corelib_link_id_t link, uint16_t destination, uint16_t source, const uint8_t *message, size_t size, uint32_t work) {
  corelib_pfp_frame_t frame;
  size_t index;
  const size_t count = (size + 39u) / 40u;
  uint32_t message_id;
  g->next_message = next_nonzero(g->next_message);
  message_id = g->next_message;
  if (count == 0u || count > 255u || size > UINT16_MAX) {
    return CORELIB_INVALID_ARGUMENT;
  }
  (void)memset(&frame, 0, sizeof(frame));
  frame.type = CORELIB_PFP_CONTROL;
  frame.destination = destination;
  frame.source = source;
  frame.session_id = g->device->session_id;
  frame.message_id = message_id;
  frame.frame_count = (uint8_t)count;
  frame.message_length = (uint16_t)size;
  frame.hop_limit = 8;
  frame.priority = CORELIB_DEFAULT_PRIORITY;
  for (index = 0; index < count; ++index) {
    uint8_t encoded[64];
    const size_t offset = index * 40u;
    size_t chunk = size - offset;
    if (chunk > 40u) {
      chunk = 40u;
    }
    frame.frame_index = (uint8_t)(index + 1u);
    (void)memset(frame.payload, 0, 40u);
    (void)memcpy(frame.payload, &message[offset], chunk);
    if (corelib_pfp_encode(&frame, encoded) != CORELIB_OK) {
      return CORELIB_INVALID_FRAME;
    }
    {
      corelib_status_t status = store_bytes(g, link, encoded, work);
      if (status != CORELIB_OK) {
        return status;
      }
    }
  }
  return CORELIB_OK;
}

/**
 * @brief Send control.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] link Gateway link used by the operation.
 * @param[in] destination Value supplied through `destination`.
 * @param[in] message Message used by the operation.
 * @param[in] size Number of valid bytes.
 * @return Operation status.
 */
static corelib_status_t send_control(corelib_gateway_context_t *g, corelib_link_id_t link, uint16_t destination, const uint8_t *message, size_t size) {
  const uint32_t work = next_nonzero(g->next_message);
  corelib_status_t status = enqueue_control(
      g, link, destination, g->device->local_address, message, size, work);
  if (status != CORELIB_OK) {
    return status;
  }
  status = flush(g);
  return status == CORELIB_BUSY ? CORELIB_OK : status;
}

/**
 * @brief Send control as root.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] link Gateway link used by the operation.
 * @param[in] destination Value supplied through `destination`.
 * @param[in] message Message used by the operation.
 * @param[in] size Number of valid bytes.
 * @return Operation status.
 */
static corelib_status_t send_control_as_root(corelib_gateway_context_t *g, corelib_link_id_t link, uint16_t destination, const uint8_t *message, size_t size) {
  const uint32_t work = next_nonzero(g->next_message);
  corelib_status_t status = enqueue_control(
      g, link, destination, CORELIB_ROOT_ADDRESS, message, size, work);
  if (status != CORELIB_OK) {
    return status;
  }
  status = flush(g);
  return status == CORELIB_BUSY ? CORELIB_OK : status;
}

/**
 * @brief Upstream peer.
 * @param[in,out] g Gateway context used by the operation.
 * @return Computed internal value.
 */
static uint16_t upstream_peer(const corelib_gateway_context_t *g) {
  return g->upstream_peer_address;
}

/**
 * @brief Topology.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] route Value supplied through `route`.
 * @param[in] reachable Value supplied through `reachable`.
 */
static void topology(corelib_gateway_context_t *g, const gateway_route_t *route, bool reachable) {
  if (g->config.callbacks.topology_changed != NULL) {
    corelib_topology_event_t event;
    (void)memset(&event, 0, sizeof(event));
    (void)memcpy(event.node_uuid, route->uuid, 16);
    event.capabilities = route->capabilities;
    event.node_address = route->address;
    event.parent_address = route->parent;
    event.ingress_link = route->next_link;
    event.state = route->state;
    event.reachable = reachable;
    const bool entered = g->in_call;
    g->in_call = true;
    g->config.callbacks.topology_changed(g->application_config.callbacks.user, &event);
    g->in_call = entered;
  }
}

/**
 * @brief Allocate route.
 * @param[in,out] g Gateway context used by the operation.
 * @return Matching internal entry, or null when none is available.
 */
static gateway_route_t *allocate_route(corelib_gateway_context_t *g) {
  size_t i;
  for (i = 0; i < g->config.storage.routes.capacity; ++i) {
    gateway_route_t *route = route_at(g, i);
    if (!route->used) {
      return route;
    }
  }
  return NULL;
}

/**
 * @brief Relay ready.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] route Value supplied through `route`.
 * @param[in] transaction Value supplied through `transaction`.
 * @return Operation status.
 */
static corelib_status_t relay_ready(corelib_gateway_context_t *g, const gateway_route_t *route, uint32_t transaction) {
  uint8_t message[64];
  size_t n = ctl_header(message, CONTROL_NODE_READY, transaction);
  const gateway_link_t *up = upstream(g);
  n = tlv(message, n, TLV_NODE_UUID, true, route->uuid, 16);
  n = tlv16(message, n, TLV_NODE_ADDRESS, route->address);
  n = tlv16(message, n, TLV_PARENT_ADDRESS, route->parent);
  n = tlv32(message, n, TLV_CAPABILITIES, route->capabilities, true);
  n = tlv16(message, n, TLV_STATUS, 0);
  return up == NULL ? CORELIB_INVALID_STATE
                    : send_control(g, up->id, upstream_peer(g), message, n);
}

/**
 * @brief Emit removed.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] route Value supplied through `route`.
 * @param[in] status Value supplied through `status`.
 */
static void emit_removed(corelib_gateway_context_t *g, const gateway_route_t *route, uint16_t status) {
  const gateway_link_t *up = upstream(g);
  uint8_t message[64];
  size_t n;
  if (up == NULL || !up->available || g->device->session_state == CORELIB_SESSION_INACTIVE) {
    return;
  }
  n = ctl_header(message, CONTROL_NODE_REMOVED, 0);
  n = tlv(message, n, TLV_NODE_UUID, true, route->uuid, 16);
  n = tlv16(message, n, TLV_NODE_ADDRESS, route->address);
  n = tlv16(message, n, TLV_PARENT_ADDRESS, route->parent);
  n = tlv16(message, n, TLV_STATUS, status);
  (void)enqueue_control(g, up->id, upstream_peer(g), g->device->local_address,
                        message, n, next_nonzero(g->next_message));
}

/**
 * @brief Send route error.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] offending Value supplied through `offending`.
 * @param[in] status Value supplied through `status`.
 */
static void send_route_error(corelib_gateway_context_t *g, const corelib_pfp_frame_t *offending, uint16_t status) {
  const gateway_link_t *egress = NULL;
  uint8_t message[32];
  size_t n;
  if (offending->source == CORELIB_ROOT_ADDRESS) {
    egress = upstream(g);
  } else {
    const gateway_route_t *reverse = find_route_address(g, offending->source);
    if (reverse != NULL && reverse->state == CORELIB_ROUTE_READY) {
      egress = find_link(g, reverse->next_link);
    }
  }
  if (egress == NULL || !egress->available) {
    return;
  }
  n = ctl_header(message, CONTROL_ROUTE_ERROR, 0);
  n = tlv16(message, n, TLV_STATUS, status);
  n = tlv32(message, n, TLV_OFFENDING_MESSAGE_ID, offending->message_id, true);
  (void)send_control(g, egress->id, offending->source, message, n);
}

/**
 * @brief Send control error.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] ingress Value supplied through `ingress`.
 * @param[in] destination Value supplied through `destination`.
 * @param[in] transaction Value supplied through `transaction`.
 * @param[in] offending_message Value supplied through `offending_message`.
 * @param[in] failure Value supplied through `failure`.
 */
static void send_control_error(corelib_gateway_context_t *g, corelib_link_id_t ingress, uint16_t destination, uint32_t transaction, uint32_t offending_message, corelib_status_t failure) {
  uint8_t message[32];
  uint16_t control_status = CORELIB_CONTROL_MALFORMED;
  size_t n;
  if (g->device->session_state == CORELIB_SESSION_INACTIVE) {
    return;
  }
  if (failure == CORELIB_UNSUPPORTED) {
    control_status = CORELIB_CONTROL_UNSUPPORTED;
  } else if (failure == CORELIB_CAPACITY_EXCEEDED) {
    control_status = CORELIB_CONTROL_RESOURCE_LIMIT;
  } else if (failure == CORELIB_INVALID_STATE) {
    control_status = CORELIB_CONTROL_CONFLICT;
  } else {
    /* Retain the malformed status for all other validation failures. */
  }
  n = ctl_header(message, CONTROL_CONTROL_ERROR, transaction);
  n = tlv16(message, n, TLV_STATUS, control_status);
  n = tlv32(message, n, TLV_OFFENDING_MESSAGE_ID, offending_message, true);
  (void)send_control(g, ingress, destination, message, n);
}

/**
 * @brief Process gateway control.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] ingress Value supplied through `ingress`.
 * @param[in] source Value supplied through `source`.
 * @param[in] bytes Encoded byte buffer used by the operation.
 * @param[in] size Number of valid bytes.
 * @param[in] frame_session Value supplied through `frame_session`.
 * @return Operation status.
 */
static corelib_status_t process_gateway_control(corelib_gateway_context_t *g, corelib_link_id_t ingress, uint16_t source, const uint8_t *bytes, size_t size, uint32_t frame_session) {
  control_fields_t f;
  uint8_t opcode;
  uint32_t transaction;
  if (!decode_control(bytes, size, &opcode, &transaction, &f)) {
    return CORELIB_INVALID_FRAME;
  }
  if (opcode == 1u || opcode == 8u || opcode == CONTROL_SESSION_END) {
    if (opcode == 1u && source != CORELIB_ROOT_ADDRESS) {
      return CORELIB_INVALID_FRAME;
    }
    if (opcode == CONTROL_SESSION_END) {
      size_t i;
      if (source != CORELIB_ROOT_ADDRESS) {
        return CORELIB_INVALID_FRAME;
      }
      for (i = 0; i < g->config.storage.routes.capacity; ++i) {
        const gateway_route_t *route = route_at(g, i);
        if (route->used && route->state == CORELIB_ROUTE_READY &&
            route->depth == 1u) {
          const gateway_link_t *link = find_link(g, route->next_link);
          if (link != NULL && link->available) {
            (void)send_control_as_root(g, link->id, route->address, bytes, size);
          }
        }
      }
    }
    const corelib_status_t status = corelib_process_control_message(
        g->device, bytes, size, frame_session);
    if (status == CORELIB_OK && opcode == CONTROL_SESSION_END) {
      clear_gateway(g, true);
    }
    return status;
  }
  if (opcode == CONTROL_ROUTE_ERROR || opcode == CONTROL_CONTROL_ERROR) {
    if ((opcode == CONTROL_ROUTE_ERROR && transaction != 0u) ||
        !required(&f, TLV_STATUS, 2) ||
        !required(&f, TLV_OFFENDING_MESSAGE_ID, 4) ||
        !only_fields(&f, (uint16_t)(((uint16_t)1u << TLV_STATUS) |
                                    ((uint16_t)1u << 8u) |
                                    ((uint16_t)1u << TLV_OFFENDING_MESSAGE_ID))) ||
        (f.value[8] != NULL && f.critical[8])) {
      return CORELIB_INVALID_FRAME;
    }
    return CORELIB_OK;
  }
  if (opcode == CONTROL_DISCOVER) {
    size_t i;
    bool selected = false;
    const gateway_link_t *source_link = find_link(g, ingress);
    if (source_link == NULL || source_link->role != CORELIB_LINK_UPSTREAM ||
        source != CORELIB_ROOT_ADDRESS ||
        transaction == 0u || !required(&f, TLV_DISCOVERY_TOKEN, 16u) ||
        !only_fields(&f, (uint16_t)(((uint16_t)1u << TLV_LINK_ID) |
                                    ((uint16_t)1u << TLV_LINK_PROFILE_ID) |
                                    ((uint16_t)1u << TLV_DISCOVERY_TOKEN))) ||
        (f.value[TLV_LINK_ID] != NULL &&
         (f.length[TLV_LINK_ID] != 2u || f.critical[TLV_LINK_ID])) ||
        (f.value[TLV_LINK_PROFILE_ID] != NULL &&
         (f.length[TLV_LINK_PROFILE_ID] != 4u || f.critical[TLV_LINK_PROFILE_ID]))) {
      return CORELIB_INVALID_FRAME;
    }
    for (i = 0; i < g->config.storage.links.capacity; ++i) {
      gateway_link_t *link = link_at(g, i);
      gateway_discovery_t *round = NULL;
      size_t j;
      if (!link->used || !link->available || link->role != CORELIB_LINK_DOWNSTREAM) {
        continue;
      }
      if (f.value[TLV_LINK_ID] != NULL &&
          (f.length[TLV_LINK_ID] != 2u || u16(f.value[TLV_LINK_ID]) != link->id)) {
        continue;
      }
      if (f.value[TLV_LINK_PROFILE_ID] != NULL &&
          (f.length[TLV_LINK_PROFILE_ID] != 4u ||
           u32(f.value[TLV_LINK_PROFILE_ID]) != link->profile)) {
        continue;
      }
      for (j = 0; j < g->config.storage.discoveries.capacity; ++j) {
        if (!discovery_at(g, j)->used) {
          round = discovery_at(g, j);
          break;
        }
      }
      if (round == NULL) {
        return CORELIB_CAPACITY_EXCEEDED;
      }
      (void)memset(round, 0, sizeof(*round));
      round->used = true;
      round->link = link->id;
      round->callback_pending = true;
      (void)memcpy(round->token, f.value[TLV_DISCOVERY_TOKEN], 16);
      round->deadline = g->now + g->config.discovery_timeout_ms;
      selected = true;
      if (g->config.callbacks.discover != NULL) {
        const bool entered = g->in_call;
        corelib_send_result_t result;
        g->in_call = true;
        result = g->config.callbacks.discover(
            g->application_config.callbacks.user, link->id, link->transport,
            link->profile, round->token);
        g->in_call = entered;
        if (result == CORELIB_SEND_FAILED) {
          round->used = false;
        } else if (result == CORELIB_SEND_ACCEPTED) {
          round->callback_pending = false;
        } else {
          /* Leave a busy callback pending for a later tick. */
        }
      }
    }
    (void)ingress;
    return selected ? CORELIB_OK : CORELIB_NOT_FOUND;
  }
  if (opcode == CONTROL_ADDRESS_ASSIGN) {
    size_t i;
    gateway_assignment_t *pending = NULL;
    gateway_candidate_t *candidate = NULL;
    gateway_link_t *link;
    const gateway_link_t *source_link = find_link(g, ingress);
    if (source_link == NULL || source_link->role != CORELIB_LINK_UPSTREAM ||
        source != CORELIB_ROOT_ADDRESS ||
        transaction == 0u || !required(&f, TLV_NODE_UUID, 16u) ||
        !required(&f, TLV_NODE_ADDRESS, 2) ||
        !required(&f, TLV_PARENT_ADDRESS, 2) ||
        !required(&f, TLV_LINK_ID, 2) ||
        !required(&f, TLV_HEARTBEAT_INTERVAL, 4) ||
        !only_fields(&f, (uint16_t)(((uint16_t)1u << TLV_NODE_UUID) |
                                    ((uint16_t)1u << TLV_NODE_ADDRESS) |
                                    ((uint16_t)1u << TLV_PARENT_ADDRESS) |
                                    ((uint16_t)1u << TLV_LINK_ID) |
                                    ((uint16_t)1u << TLV_HEARTBEAT_INTERVAL)))) {
      return CORELIB_INVALID_FRAME;
    }
    for (i = 0; i < g->config.storage.candidates.capacity; ++i) {
      gateway_candidate_t *c = candidate_at(g, i);
      if (c->used && memcmp(c->uuid, f.value[TLV_NODE_UUID], 16) == 0 &&
          c->link == u16(f.value[TLV_LINK_ID])) {
        candidate = c;
        break;
      }
    }
    if (candidate == NULL) {
      return CORELIB_NOT_FOUND;
    }
    if (find_route_uuid(g, candidate->uuid) != NULL ||
        find_route_address(g, u16(f.value[TLV_NODE_ADDRESS])) != NULL) {
      return CORELIB_INVALID_STATE;
    }
    for (i = 0; i < g->config.storage.assignments.capacity; ++i) {
      if (!assignment_at(g, i)->used) {
        pending = assignment_at(g, i);
        break;
      }
    }
    if (pending == NULL) {
      return CORELIB_CAPACITY_EXCEEDED;
    }
    link = find_link(g, candidate->link);
    if (link == NULL || !link->available) {
      return CORELIB_NOT_FOUND;
    }
    (void)memset(pending, 0, sizeof(*pending));
    pending->used = true;
    pending->callback_pending = true;
    (void)memcpy(pending->value.node_uuid, candidate->uuid, 16);
    pending->value.session_id = g->device->session_id;
    pending->value.transaction_id = transaction;
    pending->value.node_address = u16(f.value[TLV_NODE_ADDRESS]);
    pending->value.parent_address = u16(f.value[TLV_PARENT_ADDRESS]);
    pending->value.link_id = candidate->link;
    pending->value.heartbeat_interval_ms = u32(f.value[TLV_HEARTBEAT_INTERVAL]);
    pending->capabilities = candidate->capabilities;
    pending->deadline = g->now + g->config.assignment_timeout_ms;
    if (g->config.callbacks.bootstrap_assign == NULL) {
      return CORELIB_UNSUPPORTED;
    }
    {
      const bool entered = g->in_call;
      corelib_send_result_t callback_result;
      g->in_call = true;
      callback_result = g->config.callbacks.bootstrap_assign(
          g->application_config.callbacks.user, link->transport, &pending->value);
      g->in_call = entered;
      if (callback_result == CORELIB_SEND_FAILED) {
        pending->used = false;
        return CORELIB_INVALID_STATE;
      }
      if (callback_result == CORELIB_SEND_ACCEPTED) {
        pending->callback_pending = false;
      }
    }
    return CORELIB_OK;
  }
  if (opcode == CONTROL_NODE_READY) {
    gateway_route_t *route;
    const gateway_route_t *parent;
    uint8_t depth;
    const gateway_link_t *source_link = find_link(g, ingress);
    if (source_link == NULL || source_link->role != CORELIB_LINK_DOWNSTREAM ||
        !required(&f, TLV_NODE_UUID, 16) || !required(&f, TLV_NODE_ADDRESS, 2) ||
        !required(&f, TLV_PARENT_ADDRESS, 2) || !required(&f, TLV_CAPABILITIES, 4) ||
        !required(&f, TLV_STATUS, 2u) || u16(f.value[TLV_STATUS]) != 0u ||
        !only_fields(&f, (uint16_t)(((uint16_t)1u << TLV_NODE_UUID) |
                                    ((uint16_t)1u << TLV_NODE_ADDRESS) |
                                    ((uint16_t)1u << TLV_PARENT_ADDRESS) |
                                    ((uint16_t)1u << TLV_CAPABILITIES) |
                                    ((uint16_t)1u << TLV_STATUS)))) {
      return CORELIB_INVALID_FRAME;
    }
    route = find_route_uuid(g, f.value[TLV_NODE_UUID]);
    if (route != NULL) {
      if (route->address == u16(f.value[TLV_NODE_ADDRESS]) &&
          route->parent == u16(f.value[TLV_PARENT_ADDRESS]) && route->next_link == ingress) {
        return CORELIB_OK;
      }
      return CORELIB_INVALID_STATE;
    }
    if (find_route_address(g, u16(f.value[TLV_NODE_ADDRESS])) != NULL) {
      return CORELIB_INVALID_STATE;
    }
    parent = find_route_address(g, u16(f.value[TLV_PARENT_ADDRESS]));
    if (parent == NULL && u16(f.value[TLV_PARENT_ADDRESS]) != g->device->local_address) {
      return CORELIB_NOT_FOUND;
    }
    depth = (uint8_t)(parent == NULL ? 1u : (uint8_t)(parent->depth + 1u));
    if (depth > 8u) {
      return CORELIB_INVALID_STATE;
    }
    route = allocate_route(g);
    if (route == NULL) {
      return CORELIB_CAPACITY_EXCEEDED;
    }
    (void)memset(route, 0, sizeof(*route));
    route->used = true;
    (void)memcpy(route->uuid, f.value[TLV_NODE_UUID], 16);
    route->address = u16(f.value[TLV_NODE_ADDRESS]);
    route->parent = u16(f.value[TLV_PARENT_ADDRESS]);
    route->capabilities = u32(f.value[TLV_CAPABILITIES]);
    route->next_link = ingress;
    route->state = CORELIB_ROUTE_READY;
    route->depth = depth;
    topology(g, route, true);
    return relay_ready(g, route, transaction);
  }
  if (opcode == CONTROL_NODE_REMOVED) {
    const gateway_route_t *route;
    const gateway_link_t *source_link = find_link(g, ingress);
    if (source_link == NULL || source_link->role != CORELIB_LINK_DOWNSTREAM ||
        !required(&f, TLV_NODE_UUID, 16) || !required(&f, TLV_NODE_ADDRESS, 2) ||
        !required(&f, TLV_PARENT_ADDRESS, 2) || !required(&f, TLV_STATUS, 2) ||
        !only_fields(&f, (uint16_t)(((uint16_t)1u << TLV_NODE_UUID) |
                                    ((uint16_t)1u << TLV_NODE_ADDRESS) |
                                    ((uint16_t)1u << TLV_PARENT_ADDRESS) |
                                    ((uint16_t)1u << TLV_STATUS) | ((uint16_t)1u << 8)))) {
      return CORELIB_INVALID_FRAME;
    }
    route = find_route_uuid(g, f.value[TLV_NODE_UUID]);
    if (route == NULL) {
      return CORELIB_OK;
    }
    return corelib_gateway_report_node_lost(g, f.value[TLV_NODE_UUID]);
  }
  return CORELIB_UNSUPPORTED;
}

/**
 * @brief Accept control fragment.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] ingress Value supplied through `ingress`.
 * @param[in] frame PFP frame used by the operation.
 * @return Operation status.
 */
static corelib_status_t accept_control_fragment(corelib_gateway_context_t *g, corelib_link_id_t ingress, const corelib_pfp_frame_t *frame) {
  gateway_control_assembly_t *assembly = NULL;
  size_t slot = 0;
  size_t i;
  size_t offset;
  size_t chunk;
  uint8_t *message;
  uint8_t *received;
  for (i = 0; i < g->config.storage.control_reassembly_slots; ++i) {
    gateway_control_assembly_t *a = &g->assemblies[i];
    if (a->used && a->session == frame->session_id &&
        a->message_id == frame->message_id && a->source == frame->source &&
        a->destination == frame->destination) {
      assembly = a;
      slot = i;
      break;
    }
    if (!a->used && assembly == NULL) {
      assembly = a;
      slot = i;
    }
  }
  if (assembly == NULL || frame->message_length > g->config.storage.maximum_control_message_size) {
    return CORELIB_CAPACITY_EXCEEDED;
  }
  message = gateway_byte_at(g->config.storage.control_reassembly.message,
                            slot * g->config.storage.maximum_control_message_size);
  received = gateway_byte_at(g->config.storage.control_reassembly.received, slot * 255u);
  if (!assembly->used) {
    (void)memset(assembly, 0, sizeof(*assembly));
    assembly->used = true;
    assembly->session = frame->session_id;
    assembly->message_id = frame->message_id;
    assembly->source = frame->source;
    assembly->destination = frame->destination;
    assembly->length = frame->message_length;
    assembly->count = frame->frame_count;
    assembly->priority = frame->priority;
    assembly->hop = frame->hop_limit;
    assembly->started = g->now;
    (void)memset(received, 0, 255);
  } else if (assembly->length != frame->message_length ||
             assembly->count != frame->frame_count || assembly->priority != frame->priority ||
             assembly->hop != frame->hop_limit) {
    assembly->used = false;
    return CORELIB_INVALID_FRAME;
  } else {
    /* Continue receiving a fragment for the matching assembly. */
  }
  offset = ((size_t)frame->frame_index - 1u) * 40u;
  chunk = assembly->length - offset;
  if (chunk > 40u) {
    chunk = 40u;
  }
  if (received[frame->frame_index - 1u] != 0u) {
    if (memcmp(&message[offset], frame->payload, chunk) != 0) {
      assembly->used = false;
      return CORELIB_INVALID_FRAME;
    }
    return CORELIB_OK;
  }
  (void)memcpy(&message[offset], frame->payload, chunk);
  received[frame->frame_index - 1u] = 1u;
  for (i = 0; i < assembly->count; ++i) {
    if (!received[i]) {
      return CORELIB_OK;
    }
  }
  {
    const size_t completed_length = assembly->length;
    const uint32_t completed_session = assembly->session;
    assembly->used = false;
    const corelib_status_t status = process_gateway_control(
        g, ingress, assembly->source, message, completed_length,
        completed_session);
    if (status != CORELIB_OK) {
      const uint32_t transaction = completed_length >= 8u ? u32(&message[4]) : 0u;
      send_control_error(g, ingress, assembly->source, transaction, assembly->message_id, status);
    }
    return status;
  }
}

/**
 * @brief Clear gateway.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] keep_links Value supplied through `keep_links`.
 */
static void clear_gateway(corelib_gateway_context_t *g, bool keep_links) {
  size_t i;
  int depth;
  for (depth = 8; depth >= 0; --depth) {
    for (;;) {
      gateway_route_t *selected = NULL;
      for (i = 0; i < g->config.storage.routes.capacity; ++i) {
        gateway_route_t *route = route_at(g, i);
        if (route->used && route->state == CORELIB_ROUTE_READY &&
            route->depth == (uint8_t)depth &&
            (selected == NULL || route->address < selected->address)) {
          selected = route;
        }
      }
      if (selected == NULL) {
        break;
      }
      topology(g, selected, false);
      selected->used = false;
    }
  }
  (void)memset(g->assemblies, 0, sizeof(g->assemblies));
  for (i = 0; i < g->config.storage.routes.capacity; ++i) {
    (void)memset(route_at(g, i), 0, sizeof(gateway_route_t));
  }
  for (i = 0; i < g->config.storage.discoveries.capacity; ++i) {
    (void)memset(discovery_at(g, i), 0, sizeof(gateway_discovery_t));
  }
  for (i = 0; i < g->config.storage.candidates.capacity; ++i) {
    (void)memset(candidate_at(g, i), 0, sizeof(gateway_candidate_t));
  }
  for (i = 0; i < g->config.storage.assignments.capacity; ++i) {
    (void)memset(assignment_at(g, i), 0, sizeof(gateway_assignment_t));
  }
  for (i = 0; i < g->config.storage.forwarding.capacity; ++i) {
    (void)memset(forward_at(g, i), 0, sizeof(gateway_forward_t));
  }
  if (!keep_links) {
    for (i = 0; i < g->config.storage.links.capacity; ++i) {
      (void)memset(link_at(g, i), 0, sizeof(gateway_link_t));
    }
  }
}

/**
 * @brief Valid store.
 * @param[in] s Value supplied through `s`.
 * @return True when the condition or operation succeeds; otherwise false.
 */
static bool valid_store(const corelib_entry_storage_t *s) {
  return s->entries != NULL && s->capacity != 0u &&
         s->entry_size >= CORELIB_GATEWAY_ENTRY_STORAGE_SIZE &&
         (uintptr_t)s->entries % alignof(max_align_t) == 0 &&
         s->entry_size % alignof(max_align_t) == 0;
}

size_t corelib_gateway_context_size(void) {
  return sizeof(corelib_gateway_context_t);
}
size_t corelib_gateway_context_alignment(void) {
  return alignof(corelib_gateway_context_t);
}
size_t corelib_gateway_entry_size(void) {
  return CORELIB_GATEWAY_ENTRY_STORAGE_SIZE;
}

corelib_status_t corelib_gateway_init(void *memory, size_t memory_size, const corelib_gateway_config_t *config, corelib_gateway_context_t **out) {
  corelib_gateway_context_t *g;
  corelib_config_t local;
  if (memory == NULL || config == NULL || out == NULL ||
      memory_size < sizeof(corelib_gateway_context_t) ||
      !gateway_pointer_is_aligned(memory, corelib_gateway_context_alignment()) ||
      config->device.callbacks.send_frame == NULL ||
      config->storage.device_context_memory == NULL ||
      !valid_store(&config->storage.links) || !valid_store(&config->storage.routes) ||
      !valid_store(&config->storage.discoveries) || !valid_store(&config->storage.candidates) ||
      !valid_store(&config->storage.assignments) || !valid_store(&config->storage.forwarding) ||
      config->storage.control_reassembly.message == NULL ||
      config->storage.control_reassembly.received == NULL ||
      config->storage.control_reassembly_slots == 0u ||
      config->storage.control_reassembly_slots > GATEWAY_MAX_CONTROL_SLOTS ||
      config->storage.maximum_control_message_size < 64u ||
      config->discovery_timeout_ms < 10u || config->discovery_timeout_ms > 60000u ||
      config->assignment_timeout_ms < 10u || config->assignment_timeout_ms > 60000u) {
    return CORELIB_INVALID_ARGUMENT;
  }
  if (config->candidate_retention_timeout_ms < 10u ||
      config->candidate_retention_timeout_ms > 60000u) {
    return CORELIB_INVALID_ARGUMENT;
  }
  g = memory;
  (void)memset(g, 0, sizeof(*g));
  g->config = *config;
  g->application_config = config->device;
  local = config->device;
  local.capabilities |= CORELIB_CAPABILITY_GATEWAY;
  local.callbacks.send_frame = local_send;
  local.callbacks.transaction = local_transaction;
  local.callbacks.session_changed = local_session;
  local.callbacks.node_changed = local_node;
  local.callbacks.diagnostic = local_diagnostic;
  local.callbacks.user = g;
  /* The endpoint initializer normally rejects the gateway bit; it is internal here. */
  local.capabilities &= ~CORELIB_CAPABILITY_GATEWAY;
  if (corelib_init(g->config.storage.device_context_memory,
                   g->config.storage.device_context_memory_size,
                   &local, &g->device) != CORELIB_OK) {
    return CORELIB_INVALID_ARGUMENT;
  }
  g->device->config.capabilities |= CORELIB_CAPABILITY_GATEWAY;
  g->signature = GATEWAY_SIGNATURE;
  g->upstream_peer_address = CORELIB_ROOT_ADDRESS;
  clear_gateway(g, false);
  *out = g;
  return CORELIB_OK;
}

corelib_status_t corelib_gateway_add_link(corelib_gateway_context_t *g, const corelib_link_config_t *value) {
  size_t i;
  gateway_link_t *slot = NULL;
  if (g != NULL && g->in_call) {
    return CORELIB_REENTRANT;
  }
  if (g == NULL || g->signature != GATEWAY_SIGNATURE || value == NULL ||
      value->link_id == 0u || (value->role != CORELIB_LINK_UPSTREAM && value->role != CORELIB_LINK_DOWNSTREAM) ||
      (value->role == CORELIB_LINK_UPSTREAM && value->profile_id != 0u) ||
      (value->role == CORELIB_LINK_DOWNSTREAM && value->profile_id == 0u)) {
    return CORELIB_INVALID_ARGUMENT;
  }
  if (find_link(g, value->link_id) != NULL ||
      (value->role == CORELIB_LINK_UPSTREAM && upstream(g) != NULL)) {
    return CORELIB_INVALID_STATE;
  }
  for (i = 0; i < g->config.storage.links.capacity; ++i) {
    if (!link_at(g, i)->used) {
      slot = link_at(g, i);
      break;
    }
  }
  if (slot == NULL) {
    return CORELIB_CAPACITY_EXCEEDED;
  }
  (void)memset(slot, 0, sizeof(*slot));
  slot->used = true;
  slot->available = value->available;
  slot->id = value->link_id;
  slot->profile = value->profile_id;
  slot->role = value->role;
  slot->transport = value->transport_context;
  if (slot->role == CORELIB_LINK_UPSTREAM) {
    return corelib_add_link(g->device, slot->id, g);
  }
  return CORELIB_OK;
}

/**
 * @brief Remove routes for link.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] link Gateway link used by the operation.
 */
static void remove_routes_for_link(corelib_gateway_context_t *g, corelib_link_id_t link) {
  int depth;
  for (depth = 8; depth >= 0; --depth) {
    for (;;) {
      gateway_route_t *selected = NULL;
      size_t i;
      for (i = 0; i < g->config.storage.routes.capacity; ++i) {
        gateway_route_t *route = route_at(g, i);
        if (route->used && route->next_link == link &&
            route->depth == (uint8_t)depth &&
            (selected == NULL || route->address < selected->address)) {
          selected = route;
        }
      }
      if (selected == NULL) {
        break;
      }
      topology(g, selected, false);
      emit_removed(g, selected, CORELIB_CONTROL_NO_ROUTE);
      selected->used = false;
    }
  }
}

/**
 * @brief Clear link work.
 * @param[in,out] g Gateway context used by the operation.
 * @param[in] link Gateway link used by the operation.
 */
static void clear_link_work(corelib_gateway_context_t *g, corelib_link_id_t link) {
  size_t i;
  for (i = 0; i < g->config.storage.discoveries.capacity; ++i) {
    if (discovery_at(g, i)->used && discovery_at(g, i)->link == link) {
      discovery_at(g, i)->used = false;
    }
  }
  for (i = 0; i < g->config.storage.candidates.capacity; ++i) {
    if (candidate_at(g, i)->used && candidate_at(g, i)->link == link) {
      candidate_at(g, i)->used = false;
    }
  }
  for (i = 0; i < g->config.storage.assignments.capacity; ++i) {
    if (assignment_at(g, i)->used && assignment_at(g, i)->value.link_id == link) {
      assignment_at(g, i)->used = false;
    }
  }
  for (i = 0; i < g->config.storage.forwarding.capacity; ++i) {
    if (forward_at(g, i)->used && forward_at(g, i)->link == link) {
      forward_at(g, i)->used = false;
    }
  }
}

corelib_status_t corelib_gateway_set_link_available(corelib_gateway_context_t *g, corelib_link_id_t id, bool available) {
  gateway_link_t *link;
  if (g != NULL && g->in_call) {
    return CORELIB_REENTRANT;
  }
  if (g == NULL || g->signature != GATEWAY_SIGNATURE) {
    return CORELIB_INVALID_ARGUMENT;
  }
  link = find_link(g, id);
  if (link == NULL) {
    return CORELIB_NOT_FOUND;
  }
  if (link->available == available) {
    return CORELIB_OK;
  }
  link->available = available;
  if (!available && link->role == CORELIB_LINK_UPSTREAM) {
    (void)corelib_reset(g->device);
    clear_gateway(g, true);
  } else if (!available) {
    clear_link_work(g, id);
    remove_routes_for_link(g, id);
    (void)flush(g);
  } else {
    /* Making a downstream link available does not alter its routing state. */
  }
  return CORELIB_OK;
}

corelib_status_t corelib_gateway_remove_link(corelib_gateway_context_t *g, corelib_link_id_t id) {
  gateway_link_t *link;
  corelib_status_t status;
  if (g != NULL && g->in_call) {
    return CORELIB_REENTRANT;
  }
  if (g == NULL || g->signature != GATEWAY_SIGNATURE) {
    return CORELIB_INVALID_ARGUMENT;
  }
  link = find_link(g, id);
  if (link == NULL) {
    return CORELIB_NOT_FOUND;
  }
  status = corelib_gateway_set_link_available(g, id, false);
  if (link->role == CORELIB_LINK_UPSTREAM) {
    (void)corelib_remove_link(g->device, id);
  }
  (void)memset(link, 0, sizeof(*link));
  return status;
}

corelib_status_t corelib_gateway_receive_frame(corelib_gateway_context_t *g, corelib_link_id_t ingress, const uint8_t bytes[64], uint64_t now) {
  corelib_pfp_frame_t frame;
  const gateway_link_t *link;
  uint16_t local;
  if (g != NULL && g->in_call) {
    return CORELIB_REENTRANT;
  }
  if (g == NULL || g->signature != GATEWAY_SIGNATURE || bytes == NULL || now < g->now) {
    return CORELIB_INVALID_ARGUMENT;
  }
  link = find_link(g, ingress);
  if (link == NULL || !link->available) {
    return CORELIB_NOT_FOUND;
  }
  g->now = now;
  if (corelib_pfp_decode(bytes, &frame) != CORELIB_OK) {
    return CORELIB_INVALID_FRAME;
  }
  local = g->device->local_address;
  if (frame.type == CORELIB_PFP_PROBE_REQUEST) {
    if (link->role != CORELIB_LINK_UPSTREAM) {
      return CORELIB_INVALID_FRAME;
    }
    return corelib_receive_frame(g->device, ingress, bytes, now);
  }
  if (g->device->session_state != CORELIB_SESSION_INACTIVE &&
      frame.session_id != g->device->session_id) {
    return CORELIB_INVALID_STATE;
  }
  if (link->role == CORELIB_LINK_DOWNSTREAM) {
    const gateway_route_t *source = find_route_address(g, frame.source);
    if (source == NULL || source->state != CORELIB_ROUTE_READY ||
        source->next_link != ingress) {
      return CORELIB_INVALID_FRAME;
    }
  }
  if (frame.destination == local ||
      (link->role == CORELIB_LINK_UPSTREAM && local == 0u &&
       frame.destination == CORELIB_DIRECT_NODE_ADDRESS)) {
    if (frame.type == CORELIB_PFP_CONTROL) {
      return accept_control_fragment(g, ingress, &frame);
    }
    return corelib_receive_frame(g->device, ingress, bytes, now);
  }
  if (g->device->session_state == CORELIB_SESSION_INACTIVE ||
      frame.destination == 0u ||
      frame.destination == 0xffffu) {
    return CORELIB_INVALID_STATE;
  }
  {
    const gateway_link_t *egress = NULL;
    if (frame.destination == CORELIB_ROOT_ADDRESS) {
      egress = upstream(g);
    } else {
      const gateway_route_t *route = find_route_address(g, frame.destination);
      if (route != NULL && route->state == CORELIB_ROUTE_READY) {
        egress = find_link(g, route->next_link);
      }
    }
    if (egress == NULL || !egress->available || egress->id == ingress) {
      send_route_error(g, &frame, 5);
      return CORELIB_NOT_FOUND;
    }
    if (frame.hop_limit <= 1u) {
      send_route_error(g, &frame, 6);
      return CORELIB_EXPIRED;
    }
    {
      uint8_t forwarded[64];
      frame.hop_limit--;
      if (corelib_pfp_encode(&frame, forwarded) != CORELIB_OK) {
        return CORELIB_INVALID_FRAME;
      }
      return queue_bytes(g, egress->id, forwarded, ingress);
    }
  }
}

corelib_status_t corelib_gateway_accept_bootstrap_assignment(corelib_gateway_context_t *g, const corelib_bootstrap_assignment_t *assignment, uint64_t monotonic_ms) {
  corelib_status_t status;
  if (g != NULL && g->in_call) {
    return CORELIB_REENTRANT;
  }
  if (g == NULL || g->signature != GATEWAY_SIGNATURE || assignment == NULL) {
    return CORELIB_INVALID_ARGUMENT;
  }
  status = corelib_accept_bootstrap_assignment(g->device, assignment,
                                               monotonic_ms);
  if (status == CORELIB_OK) {
    g->now = monotonic_ms;
    g->upstream_peer_address = assignment->parent_address;
  }
  return status;
}

corelib_status_t corelib_gateway_report_candidate(corelib_gateway_context_t *g, const corelib_candidate_t *value) {
  const gateway_discovery_t *round = NULL;
  gateway_candidate_t *slot = NULL;
  const gateway_link_t *up;
  size_t i;
  uint8_t message[80];
  size_t n;
  if (g != NULL && g->in_call) {
    return CORELIB_REENTRANT;
  }
  if (g == NULL || g->signature != GATEWAY_SIGNATURE || value == NULL) {
    return CORELIB_INVALID_ARGUMENT;
  }
  for (i = 0; i < g->config.storage.discoveries.capacity; ++i) {
    const gateway_discovery_t *r = discovery_at(g, i);
    if (r->used && r->link == value->link_id && memcmp(r->token, value->discovery_token, 16) == 0) {
      round = r;
      break;
    }
  }
  if (round == NULL) {
    return CORELIB_NOT_FOUND;
  }
  for (i = 0; i < g->config.storage.candidates.capacity; ++i) {
    gateway_candidate_t *c = candidate_at(g, i);
    if (c->used && memcmp(c->uuid, value->node_uuid, 16) == 0) {
      return CORELIB_INVALID_STATE;
    }
    if (!c->used && slot == NULL) {
      slot = c;
    }
  }
  if (slot == NULL) {
    return CORELIB_CAPACITY_EXCEEDED;
  }
  (void)memset(slot, 0, sizeof(*slot));
  slot->used = true;
  slot->link = value->link_id;
  slot->capabilities = value->capabilities;
  (void)memcpy(slot->uuid, value->node_uuid, 16);
  (void)memcpy(slot->token, value->discovery_token, 16);
  slot->deadline = g->now + g->config.candidate_retention_timeout_ms;
  n = ctl_header(message, CONTROL_NODE_FOUND, 0);
  n = tlv(message, n, TLV_NODE_UUID, true, slot->uuid, 16);
  n = tlv16(message, n, TLV_PARENT_ADDRESS, g->device->local_address);
  n = tlv16(message, n, TLV_LINK_ID, slot->link);
  n = tlv32(message, n, TLV_LINK_PROFILE_ID, find_link(g, slot->link)->profile, true);
  n = tlv32(message, n, TLV_CAPABILITIES, slot->capabilities, true);
  n = tlv(message, n, TLV_DISCOVERY_TOKEN, true, slot->token, 16);
  up = upstream(g);
  return up == NULL ? CORELIB_INVALID_STATE
                    : send_control(g, up->id, upstream_peer(g), message, n);
}

corelib_status_t corelib_gateway_complete_discovery(corelib_gateway_context_t *g, corelib_link_id_t link, const uint8_t token[16], corelib_status_t result) {
  size_t i;
  if (g != NULL && g->in_call) {
    return CORELIB_REENTRANT;
  }
  if (g == NULL || g->signature != GATEWAY_SIGNATURE || token == NULL) {
    return CORELIB_INVALID_ARGUMENT;
  }
  for (i = 0; i < g->config.storage.discoveries.capacity; ++i) {
    gateway_discovery_t *r = discovery_at(g, i);
    if (r->used && r->link == link && memcmp(r->token, token, 16) == 0) {
      r->used = false;
      return result;
    }
  }
  return CORELIB_NOT_FOUND;
}

corelib_status_t corelib_gateway_complete_assignment(corelib_gateway_context_t *g, uint32_t transaction, const uint8_t uuid[16], corelib_control_status_t result) {
  gateway_assignment_t *a = NULL;
  gateway_route_t *route;
  const gateway_link_t *up;
  uint8_t ack[64];
  uint8_t ready[64];
  size_t n;
  size_t ready_n;
  size_t i;
  if (g != NULL && g->in_call) {
    return CORELIB_REENTRANT;
  }
  if (g == NULL || g->signature != GATEWAY_SIGNATURE || transaction == 0u ||
      uuid == NULL || result > CORELIB_CONTROL_RESOURCE_LIMIT) {
    return CORELIB_INVALID_ARGUMENT;
  }
  for (i = 0; i < g->config.storage.assignments.capacity; ++i) {
    if (assignment_at(g, i)->used &&
        assignment_at(g, i)->value.transaction_id == transaction &&
        memcmp(assignment_at(g, i)->value.node_uuid, uuid, 16) == 0) {
      a = assignment_at(g, i);
      break;
    }
  }
  if (a == NULL) {
    return CORELIB_NOT_FOUND;
  }
  up = upstream(g);
  n = ctl_header(ack, CONTROL_ADDRESS_ACK, transaction);
  n = tlv(ack, n, TLV_NODE_UUID, true, a->value.node_uuid, 16);
  n = tlv16(ack, n, TLV_NODE_ADDRESS, a->value.node_address);
  n = tlv16(ack, n, TLV_PARENT_ADDRESS, a->value.parent_address);
  n = tlv16(ack, n, TLV_STATUS, (uint16_t)result);
  if (up == NULL) {
    return CORELIB_INVALID_STATE;
  }
  if (result != CORELIB_CONTROL_SUCCESS) {
    const corelib_status_t status = send_control(
        g, up->id, upstream_peer(g), ack, n);
    if (status == CORELIB_OK) {
      for (i = 0; i < g->config.storage.candidates.capacity; ++i) {
        if (candidate_at(g, i)->used &&
            memcmp(candidate_at(g, i)->uuid, a->value.node_uuid, 16) == 0) {
          candidate_at(g, i)->used = false;
        }
      }
      a->used = false;
    }
    return status;
  }
  route = allocate_route(g);
  if (route == NULL || free_forward_slots(g) < 3u) {
    return CORELIB_CAPACITY_EXCEEDED;
  }
  (void)memset(route, 0, sizeof(*route));
  route->used = true;
  (void)memcpy(route->uuid, a->value.node_uuid, 16);
  route->capabilities = a->capabilities;
  route->address = a->value.node_address;
  route->parent = a->value.parent_address;
  route->next_link = a->value.link_id;
  route->depth = 1;
  route->state = CORELIB_ROUTE_PROVISIONAL;
  ready_n = ctl_header(ready, CONTROL_NODE_READY, transaction);
  ready_n = tlv(ready, ready_n, TLV_NODE_UUID, true, route->uuid, 16);
  ready_n = tlv16(ready, ready_n, TLV_NODE_ADDRESS, route->address);
  ready_n = tlv16(ready, ready_n, TLV_PARENT_ADDRESS, route->parent);
  ready_n = tlv32(ready, ready_n, TLV_CAPABILITIES, route->capabilities, true);
  ready_n = tlv16(ready, ready_n, TLV_STATUS, 0);
  {
    const uint32_t work = next_nonzero(g->next_message);
    corelib_status_t status = enqueue_control(
        g, up->id, upstream_peer(g), g->device->local_address, ack, n, work);
    if (status == CORELIB_OK) {
      status = enqueue_control(g, up->id, upstream_peer(g),
                               g->device->local_address, ready, ready_n, work);
    }
    if (status != CORELIB_OK) {
      size_t slot;
      route->used = false;
      for (slot = 0; slot < g->config.storage.forwarding.capacity; ++slot) {
        if (forward_at(g, slot)->used && forward_at(g, slot)->work == work) {
          forward_at(g, slot)->used = false;
        }
      }
      return status;
    }
  }
  route->state = CORELIB_ROUTE_READY;
  topology(g, route, true);
  for (i = 0; i < g->config.storage.candidates.capacity; ++i) {
    if (candidate_at(g, i)->used &&
        memcmp(candidate_at(g, i)->uuid, a->value.node_uuid, 16) == 0) {
      candidate_at(g, i)->used = false;
    }
  }
  a->used = false;
  {
    const corelib_status_t status = flush(g);
    return status == CORELIB_BUSY ? CORELIB_OK : status;
  }
}

corelib_status_t corelib_gateway_report_node_lost(corelib_gateway_context_t *g, const uint8_t uuid[16]) {
  const gateway_route_t *route;
  if (g != NULL && g->in_call) {
    return CORELIB_REENTRANT;
  }
  if (g == NULL || g->signature != GATEWAY_SIGNATURE || uuid == NULL) {
    return CORELIB_INVALID_ARGUMENT;
  }
  route = find_route_uuid(g, uuid);
  if (route == NULL) {
    return CORELIB_NOT_FOUND;
  }
  {
    const uint16_t lost = route->address;
    int depth;
    for (depth = 8; depth >= 0; --depth) {
      for (;;) {
        gateway_route_t *selected = NULL;
        size_t i;
        for (i = 0; i < g->config.storage.routes.capacity; ++i) {
          gateway_route_t *candidate = route_at(g, i);
          uint16_t parent;
          bool affected = candidate->used && candidate->address == lost;
          if (!candidate->used || candidate->depth != (uint8_t)depth) {
            continue;
          }
          parent = candidate->parent;
          while (!affected && parent != g->device->local_address && parent != 0u) {
            const gateway_route_t *ancestor = find_route_address(g, parent);
            if (parent == lost) {
              affected = true;
            }
            if (ancestor == NULL) {
              break;
            }
            parent = ancestor->parent;
          }
          if (affected && (selected == NULL || candidate->address < selected->address)) {
            selected = candidate;
          }
        }
        if (selected == NULL) {
          break;
        }
        topology(g, selected, false);
        emit_removed(g, selected, CORELIB_CONTROL_NO_ROUTE);
        selected->used = false;
      }
    }
  }
  return CORELIB_OK;
}

corelib_status_t corelib_gateway_respond(corelib_gateway_context_t *g, const corelib_transaction_id_t *request, corelib_transaction_result_t result, const uint8_t *data, size_t data_size) {
  if (g != NULL && g->in_call) {
    return CORELIB_REENTRANT;
  }
  if (g == NULL || g->signature != GATEWAY_SIGNATURE) {
    return CORELIB_INVALID_ARGUMENT;
  }
  return corelib_respond(g->device, request, result, data, data_size);
}

corelib_status_t corelib_gateway_publish(corelib_gateway_context_t *g, bool common, uint32_t share_id, const uint8_t *data, size_t data_size) {
  if (g != NULL && g->in_call) {
    return CORELIB_REENTRANT;
  }
  if (g == NULL || g->signature != GATEWAY_SIGNATURE) {
    return CORELIB_INVALID_ARGUMENT;
  }
  return corelib_publish(g->device, common, share_id, data, data_size);
}

corelib_status_t corelib_gateway_tick(corelib_gateway_context_t *g, uint64_t now) {
  size_t i;
  if (g != NULL && g->in_call) {
    return CORELIB_REENTRANT;
  }
  if (g == NULL || g->signature != GATEWAY_SIGNATURE || now < g->now) {
    return CORELIB_INVALID_ARGUMENT;
  }
  g->now = now;
  for (i = 0; i < g->config.storage.control_reassembly_slots; ++i) {
    if (g->assemblies[i].used && now - g->assemblies[i].started >= 1000u) {
      g->assemblies[i].used = false;
    }
  }
  for (i = 0; i < g->config.storage.discoveries.capacity; ++i) {
    if (discovery_at(g, i)->used && now >= discovery_at(g, i)->deadline) {
      discovery_at(g, i)->used = false;
      gateway_diag(g, CORELIB_DIAGNOSTIC_REQUEST_EXPIRED, CORELIB_EXPIRED);
    } else if (discovery_at(g, i)->used && discovery_at(g, i)->callback_pending &&
               g->config.callbacks.discover != NULL) {
      gateway_discovery_t *round = discovery_at(g, i);
      gateway_link_t *link = find_link(g, round->link);
      if (link == NULL || !link->available) {
        round->used = false;
      } else {
        const bool entered = g->in_call;
        corelib_send_result_t callback_result;
        g->in_call = true;
        callback_result = g->config.callbacks.discover(
            g->application_config.callbacks.user, link->id, link->transport,
            link->profile, round->token);
        g->in_call = entered;
        if (callback_result == CORELIB_SEND_ACCEPTED) {
          round->callback_pending = false;
        } else if (callback_result == CORELIB_SEND_FAILED) {
          round->used = false;
        } else {
          /* Leave a busy callback pending for a later tick. */
        }
      }
    } else {
      /* No discovery state transition is due on this tick. */
    }
  }
  for (i = 0; i < g->config.storage.candidates.capacity; ++i) {
    if (candidate_at(g, i)->used && now >= candidate_at(g, i)->deadline) {
      candidate_at(g, i)->used = false;
    }
  }
  for (i = 0; i < g->config.storage.assignments.capacity; ++i) {
    if (assignment_at(g, i)->used && now >= assignment_at(g, i)->deadline) {
      gateway_assignment_t *pending = assignment_at(g, i);
      const uint32_t transaction = pending->value.transaction_id;
      uint8_t uuid[16];
      (void)memcpy(uuid, pending->value.node_uuid, 16);
      (void)corelib_gateway_complete_assignment(
          g, transaction, uuid, CORELIB_CONTROL_SESSION_REJECTED);
      gateway_diag(g, CORELIB_DIAGNOSTIC_REQUEST_EXPIRED, CORELIB_EXPIRED);
    } else if (assignment_at(g, i)->used && assignment_at(g, i)->callback_pending &&
               g->config.callbacks.bootstrap_assign != NULL) {
      gateway_assignment_t *pending = assignment_at(g, i);
      gateway_link_t *link = find_link(g, pending->value.link_id);
      if (link == NULL || !link->available) {
        pending->used = false;
      } else {
        const bool entered = g->in_call;
        corelib_send_result_t callback_result;
        g->in_call = true;
        callback_result = g->config.callbacks.bootstrap_assign(
            g->application_config.callbacks.user, link->transport, &pending->value);
        g->in_call = entered;
        if (callback_result == CORELIB_SEND_ACCEPTED) {
          pending->callback_pending = false;
        } else if (callback_result == CORELIB_SEND_FAILED) {
          pending->used = false;
        } else {
          /* Leave a busy callback pending for a later tick. */
        }
      }
    } else {
      /* No assignment state transition is due on this tick. */
    }
  }
  {
    corelib_status_t status = corelib_tick(g->device, now);
    if (status != CORELIB_OK && status != CORELIB_BUSY) {
      return status;
    }
  }
  {
    corelib_status_t status = flush(g);
    return status == CORELIB_BUSY ? CORELIB_OK : status;
  }
}

corelib_status_t corelib_gateway_reset(corelib_gateway_context_t *g) {
  if (g != NULL && g->in_call) {
    return CORELIB_REENTRANT;
  }
  if (g == NULL || g->signature != GATEWAY_SIGNATURE) {
    return CORELIB_INVALID_ARGUMENT;
  }
  (void)corelib_reset(g->device);
  clear_gateway(g, true);
  return CORELIB_OK;
}

corelib_status_t corelib_gateway_usage(const corelib_gateway_context_t *cg, corelib_gateway_usage_t *usage) {
  size_t i;
  if (cg == NULL || cg->signature != GATEWAY_SIGNATURE || usage == NULL) {
    return CORELIB_INVALID_ARGUMENT;
  }
  (void)memset(usage, 0, sizeof(*usage));
  for (i = 0; i < cg->config.storage.links.capacity; ++i) {
    const gateway_link_t *link = (const gateway_link_t *)entry_at_const(&cg->config.storage.links, i);
    if (link->used) {
      usage->links++;
    }
  }
  for (i = 0; i < cg->config.storage.routes.capacity; ++i) {
    const gateway_route_t *route = (const gateway_route_t *)entry_at_const(&cg->config.storage.routes, i);
    if (route->used) {
      usage->routes++;
    }
  }
  for (i = 0; i < cg->config.storage.discoveries.capacity; ++i) {
    const gateway_discovery_t *discovery = (const gateway_discovery_t *)entry_at_const(&cg->config.storage.discoveries, i);
    if (discovery->used) {
      usage->discoveries++;
    }
  }
  for (i = 0; i < cg->config.storage.candidates.capacity; ++i) {
    const gateway_candidate_t *candidate = (const gateway_candidate_t *)entry_at_const(&cg->config.storage.candidates, i);
    if (candidate->used) {
      usage->candidates++;
    }
  }
  for (i = 0; i < cg->config.storage.assignments.capacity; ++i) {
    const gateway_assignment_t *assignment = (const gateway_assignment_t *)entry_at_const(&cg->config.storage.assignments, i);
    if (assignment->used) {
      usage->assignments++;
    }
  }
  for (i = 0; i < cg->config.storage.forwarding.capacity; ++i) {
    const gateway_forward_t *forward = (const gateway_forward_t *)entry_at_const(&cg->config.storage.forwarding, i);
    if (forward->used) {
      usage->queued_frames++;
    }
  }
  for (i = 0; i < cg->config.storage.control_reassembly_slots; ++i) {
    if (cg->assemblies[i].used) {
      usage->active_control_reassemblies++;
    }
  }
  return CORELIB_OK;
}

corelib_status_t corelib_gateway_limits(const corelib_gateway_context_t *g, corelib_gateway_limits_t *limits) {
  if (g == NULL || g->signature != GATEWAY_SIGNATURE || limits == NULL) {
    return CORELIB_INVALID_ARGUMENT;
  }
  limits->links = g->config.storage.links.capacity;
  limits->routes = g->config.storage.routes.capacity;
  limits->discoveries = g->config.storage.discoveries.capacity;
  limits->candidates = g->config.storage.candidates.capacity;
  limits->assignments = g->config.storage.assignments.capacity;
  limits->queued_frames = g->config.storage.forwarding.capacity;
  limits->control_reassembly_slots = g->config.storage.control_reassembly_slots;
  limits->maximum_control_message_size = g->config.storage.maximum_control_message_size;
  limits->discovery_timeout_ms = g->config.discovery_timeout_ms;
  limits->assignment_timeout_ms = g->config.assignment_timeout_ms;
  limits->candidate_retention_timeout_ms = g->config.candidate_retention_timeout_ms;
  return CORELIB_OK;
}
