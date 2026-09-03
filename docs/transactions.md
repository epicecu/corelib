# Transactions

Transaction Protocol v2 provides two application payload namespaces:

- **Common** contains schemas shared by a wider device class.
- **Share** identifies a device-specific schema with a `share_id`.

Corelib transports their encoded bytes but does not generate schemas, decode
payloads, enforce application permissions, or update device state.

## Requests and responses

A request callback supplies `corelib_transaction_t`. Its `data` is borrowed
and remains valid only until the callback returns. The embedded
`corelib_transaction_id_t` is a small retainable correlation value.

Copy the payload into application-owned storage when work must be deferred.
After returning from the callback, respond with the retained identity and one
of the protocol results: success, unsupported, busy, invalid request, or
internal error.

```c
static corelib_transaction_id_t pending_id;
static bool response_pending;

static void on_transaction(void *user, const corelib_transaction_t *value) {
  (void)user;
  pending_id = value->id;
  response_pending = true;
}

/* Called later from the context-owning loop. */
if (response_pending) {
  response_pending = false;
  (void)corelib_respond(device, &pending_id, CORELIB_RESULT_UNSUPPORTED,
                        NULL, 0u);
}
```

Responses must arrive before the configured application response timeout.
Expired identities cannot be reused.

## Publications

`corelib_publish()` sends an unsolicited Common or Share value. The encoded
payload is borrowed only for the duration of the call. Publication can require
several PFP frames, so sufficient outbound queue capacity must be available.

A successful call means Corelib accepted the complete transaction into its
bounded work queues. It does not mean the transport or remote consumer has
acknowledged the application value.
